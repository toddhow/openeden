#include "opas/anim/AnimationPlayer.hpp"

#include "opas/anim/PoseBlend.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace opas::anim
{
    namespace
    {

        // Summed steps round (30 steps of 1/60 s give 0.49999999999999994), so a
        // fade within this of its end is over, as for the inertializer.
        constexpr double kEndToleranceS = 1e-9;

        double usableDt(double dtS)
        {
            return std::isfinite(dtS) && dtS > 0.0 ? dtS : 0.0;
        }

        RootMotionDelta blendRootMotion(const RootMotionDelta& from, const RootMotionDelta& to, double w)
        {
            return { from.translationM + (to.translationM - from.translationM) * w,
                from.yawRad + (to.yawRad - from.yawRad) * w };
        }

    } // namespace

    AnimationPlayer::AnimationPlayer(std::size_t boneCount, HelperSolver helpers)
        : helperSolver_(std::move(helpers))
        , inertializer_(boneCount)
        , pose_(boneCount)
        , previousPose_(boneCount)
        , incoming_(boneCount)
        , fadeFrom_(boneCount)
    {
    }

    void AnimationPlayer::setHelperSolver(HelperSolver solver)
    {
        helperSolver_ = std::move(solver);
        if (current_.playing())
        {
            helperSolver_.apply(pose_, timeS_, exertion_);
        }
    }

    void AnimationPlayer::setExertion(double exertion) noexcept
    {
        exertion_ = std::isfinite(exertion) ? std::max(0.0, exertion) : 0.0;
    }

    bool AnimationPlayer::play(std::shared_ptr<const ClipBinding> binding, const PlayOptions& options,
        const Transition& transition, std::string* error)
    {
        if (binding == nullptr || binding->targetBoneCount() != pose_.size())
        {
            if (error != nullptr)
            {
                *error = binding == nullptr ? "no clip binding"
                                            : "the binding's target has " + std::to_string(binding->targetBoneCount())
                        + " bones; the player has " + std::to_string(pose_.size());
            }
            return false;
        }
        const double duration
            = std::isfinite(transition.durationS) && transition.durationS > 0.0 ? transition.durationS : 0.0;
        if (!current_.playing() || duration == 0.0)
        {
            // Switch at once.
            current_.play(std::move(binding), options);
            current_.sample(pose_);
            timeS_ = current_.timeS();
            helperSolver_.apply(pose_, timeS_, exertion_);
            std::copy(pose_.begin(), pose_.end(), previousPose_.begin());
            inertializer_.clear();
            outgoing_.stop();
            fading_ = false;
            outgoingFrozen_ = false;
            return true;
        }
        if (transition.mode == TransitionMode::Inertialize)
        {
            current_.play(std::move(binding), options);
            current_.sample(incoming_);
            // The new clip one step before its start (fadeFrom_ is free outside
            // crossfades), so the offset's rate excludes the new clip's own motion.
            const ClipPlayer& next = current_;
            next.binding()->sample(next.timeS() - next.speed() * lastDtS_, next.mode(), fadeFrom_);
            inertializer_.start(previousPose_, pose_, fadeFrom_, incoming_, lastDtS_, duration);
            outgoing_.stop();
            fading_ = false;
            outgoingFrozen_ = false;
            return true; // pose_ stays the current output (offset at elapsed 0)
        }
        // Crossfade. During another transition the old clip alone no longer
        // gives the output, so the output is frozen and faded from (header).
        if (fading_ || inertializer_.active())
        {
            std::copy(pose_.begin(), pose_.end(), fadeFrom_.begin());
            outgoing_.stop();
            outgoingFrozen_ = true;
        }
        else
        {
            outgoing_ = current_;
            outgoingFrozen_ = false;
        }
        inertializer_.clear();
        current_.play(std::move(binding), options);
        fadeElapsedS_ = 0.0;
        fadeDurationS_ = duration;
        fading_ = true;
        return true;
    }

    PlayerStep AnimationPlayer::advance(double dtS, std::span<FiredEvent> events)
    {
        if (!current_.playing())
        {
            return {};
        }
        const double dt = usableDt(dtS);
        if (dt == 0.0)
        {
            // A paused step changes nothing, and keeps the last real step's pose
            // and dt for the velocity at a switch.
            return {};
        }
        std::copy(pose_.begin(), pose_.end(), previousPose_.begin());
        PlayerStep step = current_.advance(dt, events);
        current_.sample(incoming_);
        if (fading_)
        {
            fadeElapsedS_ += dt;
            const double w = fadeElapsedS_ >= fadeDurationS_ - kEndToleranceS ? 1.0 : fadeElapsedS_ / fadeDurationS_;
            RootMotionDelta fromMotion;
            if (!outgoingFrozen_)
            {
                fromMotion = outgoing_.advance(dt, {}).rootMotion; // its events are not fired
                outgoing_.sample(fadeFrom_);
            }
            step.rootMotion = blendRootMotion(fromMotion, step.rootMotion, w);
            blendPoses(fadeFrom_, incoming_, w, pose_);
            if (w >= 1.0)
            {
                fading_ = false;
                outgoingFrozen_ = false;
                outgoing_.stop();
            }
        }
        else
        {
            std::copy(incoming_.begin(), incoming_.end(), pose_.begin());
            inertializer_.advanceAndApply(dt, pose_);
        }
        timeS_ += dt;
        helperSolver_.apply(pose_, timeS_, exertion_);
        lastDtS_ = dt;
        return step;
    }

    bool AnimationPlayer::inTransition() const
    {
        return fading_ || inertializer_.active();
    }

} // namespace opas::anim
