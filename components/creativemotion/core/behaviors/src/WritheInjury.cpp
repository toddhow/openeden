#include "opas/behaviors/WritheInjury.hpp"

#include <cmath>
#include <numbers>

namespace opas::behaviors {

WritheInjuryController::WritheInjuryController(WritheTuning tuning)
    : tuning_(tuning)
{}

void WritheInjuryController::setInjured(arbiter::BodyPart part, bool injured)
{
    const auto idx = static_cast<std::size_t>(part);
    if (idx < injured_.size()) {
        injured_[idx] = injured;
    }
}

bool WritheInjuryController::isInjured(arbiter::BodyPart part) const
{
    const auto idx = static_cast<std::size_t>(part);
    return (idx < injured_.size()) ? injured_[idx] : false;
}

void WritheInjuryController::clearInjuries()
{
    injured_.fill(false);
}

double WritheInjuryController::partStrictness(arbiter::BodyPart part,
                                            arbiter::ArbiterState state) const
{
    if (isInjured(part)) {
        return tuning_.injuredStrictness;
    }

    switch (state) {
    case arbiter::ArbiterState::Fallen:
    case arbiter::ArbiterState::Down:
    case arbiter::ArbiterState::Falling:
        return tuning_.defaultFallenStrictness;
    case arbiter::ArbiterState::Standing:
    case arbiter::ArbiterState::Animated:
    case arbiter::ArbiterState::GettingUp:
        return 1.0;
    case arbiter::ArbiterState::Leaning:
        return 0.95;
    case arbiter::ArbiterState::Stepping:
        return 0.90;
    case arbiter::ArbiterState::Stumbling:
        return 0.50;
    case arbiter::ArbiterState::Disturbed:
        return 0.20;
    }
    return 1.0;
}

math::Quat WritheInjuryController::computeSpineWritheOffset(double timeSeconds,
                                                            std::uint32_t seed) const
{
    if (tuning_.breathAmplitudeRad <= 0.0 && tuning_.shiftAmplitudeRad <= 0.0) {
        return math::Quat{};
    }

    const double phaseSeed = static_cast<double>(seed % 1000) * 0.0062831853;
    const double breathAngle =
        tuning_.breathAmplitudeRad *
        std::sin(2.0 * std::numbers::pi * tuning_.breathFrequencyHz * timeSeconds);
    const double shiftAngle =
        (timeSeconds > 0.0)
            ? (tuning_.shiftAmplitudeRad *
               std::sin(2.0 * std::numbers::pi * tuning_.shiftFrequencyHz * timeSeconds + phaseSeed))
            : 0.0;

    math::Vec3 axis{1.0, 0.2 * std::sin(phaseSeed), 0.1 * std::cos(phaseSeed)};
    axis = math::normalized(axis);
    const double totalAngle = breathAngle + shiftAngle;
    return math::fromAxisAngle(axis, totalAngle);
}

math::Quat WritheInjuryController::computeLimbWritheOffset(arbiter::BodyPart part,
                                                           double timeSeconds,
                                                           std::uint32_t seed) const
{
    const auto partIdx = static_cast<std::uint32_t>(part);
    const double partPhase = static_cast<double>(partIdx) * 1.04719755;
    const double phaseSeed = static_cast<double>((seed + partIdx * 101) % 1000) * 0.0062831853;
    const double ampScale = isInjured(part) ? 0.35 : 1.0;

    const double breathAngle =
        tuning_.breathAmplitudeRad * ampScale *
        std::sin(2.0 * std::numbers::pi * tuning_.breathFrequencyHz * timeSeconds + partPhase);
    const double shiftAngle =
        (timeSeconds > 0.0)
            ? (tuning_.shiftAmplitudeRad * ampScale *
               std::sin(2.0 * std::numbers::pi * tuning_.shiftFrequencyHz * timeSeconds + phaseSeed))
            : 0.0;

    math::Vec3 axis{std::cos(partPhase + phaseSeed), std::sin(partPhase), 0.3 * std::cos(phaseSeed)};
    if (math::length(axis) < 1e-6) {
        axis = math::Vec3{1.0, 0.0, 0.0};
    } else {
        axis = math::normalized(axis);
    }
    const double totalAngle = breathAngle + shiftAngle;
    return math::fromAxisAngle(axis, totalAngle);
}

void WritheInjuryController::update(double dtSeconds, arbiter::ArbiterState /*state*/)
{
    elapsedS_ += dtSeconds;
}

void WritheInjuryController::reset()
{
    elapsedS_ = 0.0;
}

} // namespace opas::behaviors
