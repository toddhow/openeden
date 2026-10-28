#include "opas/reconcile/EventBridge.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <variant>

namespace opas::reconcile {

void EventBridge::reset() noexcept
{
    inboundQueue_.clear();
    outboundQueue_.clear();
    prevArbiterState_ = std::nullopt;
    prevStumbleActive_ = false;
    prevGetUpResting_ = false;
    prevGetUpBlending_ = false;
    prevGetUpCompleted_ = false;
    prevGrabHolding_ = false;
    prevCapsuleClamped_ = false;
    prevDied_ = false;
}

void EventBridge::dispatchInbound(sensors::ImpactSensor* impactSensor,
                                  behaviors::BalanceDriver* balanceDriver,
                                  arbiter::Arbiter* arbiter)
{
    while (auto ev = inboundQueue_.pop()) {
        std::visit([&](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, GameHitEvent>) {
                if (impactSensor) {
                    sensors::ImpactEvent impact;
                    impact.bodyPart = e.bodyPart;
                    impact.bodyIndex = e.bodyIndex;
                    impact.contactPointM = e.contactPointM;
                    impact.normal = math::Vec3{0.0, 0.0, 1.0};
                    impact.impulseNs = e.direction * e.impulseNs;
                    impact.energyJ = e.damage;
                    impactSensor->recordImpact(impact);
                }
                if (balanceDriver) {
                    balanceDriver->reportHit(e.bodyIndex);
                }
                if (arbiter) {
                    arbiter->disturbPart(e.bodyPart);
                }
            } else if constexpr (std::is_same_v<T, GameDamageEvent>) {
                if (arbiter) {
                    arbiter->disturbPart(e.bodyPart);
                    const double baseStrictness = arbiter->partStrictness(e.bodyPart);
                    const double newStrictness = std::max(0.0, baseStrictness * (1.0 - e.severity));
                    arbiter->setPartStrictness(e.bodyPart, newStrictness);
                }
            } else if constexpr (std::is_same_v<T, GameTriggerEvent>) {
                switch (e.type) {
                case TriggerType::Stagger:
                    if (balanceDriver) {
                        balanceDriver->triggerStumble(e.direction);
                    }
                    if (arbiter) {
                        arbiter->disturbPart(arbiter::BodyPart::Torso);
                    }
                    break;
                case TriggerType::Trip:
                case TriggerType::Knockdown:
                    if (arbiter) {
                        arbiter->disturbPart(arbiter::BodyPart::Torso);
                        arbiter->disturbPart(arbiter::BodyPart::LeftLeg);
                        arbiter->disturbPart(arbiter::BodyPart::RightLeg);
                    }
                    break;
                case TriggerType::Die:
                    if (arbiter) {
                        for (int p = 0; p < static_cast<int>(arbiter::BodyPart::Count); ++p) {
                            const auto part = static_cast<arbiter::BodyPart>(p);
                            arbiter->setPartStrictness(part, 0.0);
                        }
                    }
                    break;
                case TriggerType::Recover:
                    if (arbiter) {
                        for (int p = 0; p < static_cast<int>(arbiter::BodyPart::Count); ++p) {
                            const auto part = static_cast<arbiter::BodyPart>(p);
                            arbiter->setStrictnessOverride(part, std::nullopt);
                        }
                    }
                    break;
                case TriggerType::GetUp:
                    break;
                }
            }
        }, *ev);
    }
}

void EventBridge::updateOutbound(const arbiter::Arbiter& arbiter,
                                const behaviors::StumbleController* stumble,
                                const behaviors::GetUpController* getUp,
                                const CapsuleReconciler* capsule,
                                const behaviors::GrabController* grab)
{
    // 1. Arbiter State transitions
    const arbiter::ArbiterState curArbiterState = arbiter.overallState();
    if (prevArbiterState_.has_value()) {
        const arbiter::ArbiterState prev = *prevArbiterState_;
        if (prev != curArbiterState) {
            const bool wasUpright = (prev != arbiter::ArbiterState::Falling &&
                                     prev != arbiter::ArbiterState::Fallen &&
                                     prev != arbiter::ArbiterState::Down);
            const bool isFalling = (curArbiterState == arbiter::ArbiterState::Falling ||
                                    curArbiterState == arbiter::ArbiterState::Fallen ||
                                    curArbiterState == arbiter::ArbiterState::Down);
            if (wasUpright && isFalling) {
                outboundQueue_.push(StateEventOut::knockedDown());
            }
        }
    }
    prevArbiterState_ = curArbiterState;

    // Check for death via zeroed strictness across all parts
    bool allPartsZero = true;
    for (int p = 0; p < static_cast<int>(arbiter::BodyPart::Count); ++p) {
        if (arbiter.partStrictness(static_cast<arbiter::BodyPart>(p)) > 1e-4) {
            allPartsZero = false;
            break;
        }
    }
    if (allPartsZero && !prevDied_) {
        outboundQueue_.push(StateEventOut::died());
        prevDied_ = true;
    } else if (!allPartsZero) {
        prevDied_ = false;
    }

    // 2. StumbleController transitions
    if (stumble) {
        const bool curActive = stumble->isActive();
        if (!prevStumbleActive_ && curActive) {
            outboundQueue_.push(StateEventOut::staggerStarted(stumble->tuning().triggerImpulseNs,
                                                             stumble->perturbationDirection()));
        } else if (prevStumbleActive_ && !curActive) {
            outboundQueue_.push(StateEventOut::staggerRecovered());
        }
        prevStumbleActive_ = curActive;
    }

    // 3. GetUpController transitions
    if (getUp) {
        const bool curResting = getUp->isResting();
        const bool curBlending = getUp->isGettingUp();
        const bool curCompleted = getUp->isCompleted();

        if (!prevGetUpResting_ && curResting) {
            outboundQueue_.push(StateEventOut::settledResting(getUp->orientation(), getUp->restTimerS()));
        }
        prevGetUpResting_ = curResting;

        if (!prevGetUpBlending_ && curBlending) {
            outboundQueue_.push(StateEventOut::gettingUpStarted());
        }
        prevGetUpBlending_ = curBlending;

        if (!prevGetUpCompleted_ && curCompleted) {
            outboundQueue_.push(StateEventOut::recoveredUpright());
        }
        prevGetUpCompleted_ = curCompleted;
    }

    // 4. GrabController transitions
    if (grab) {
        const bool curHolding = grab->isHolding();
        if (!prevGrabHolding_ && curHolding) {
            math::Vec3 contactPos{};
            arbiter::BodyPart handSide = arbiter::BodyPart::RightArm;
            if (grab->activeTarget().has_value()) {
                contactPos = grab->activeTarget()->positionM;
                handSide = grab->activeTarget()->handPart;
            }
            outboundQueue_.push(StateEventOut::surfaceGrabbed(contactPos, handSide));
        }
        prevGrabHolding_ = curHolding;
    }

    // 5. CapsuleReconciler alerts
    if (capsule) {
        const bool curClamped = capsule->isClamped();
        if (!prevCapsuleClamped_ && curClamped) {
            outboundQueue_.push(StateEventOut::capsuleExcursionAlert(capsule->horizontalExcursion(), curClamped));
        }
        prevCapsuleClamped_ = curClamped;
    }
}

} // namespace opas::reconcile
