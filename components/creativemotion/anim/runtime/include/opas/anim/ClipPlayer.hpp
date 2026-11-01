#pragma once

// Plays one bound clip (task 0020, stage C3c): advances clip time by
// speed * dt, reports root motion and the events crossed, and samples the
// pose. Time is passed in by the caller; nothing here reads a clock.
//
// Ownership: the player shares ownership of the binding it plays
// (std::shared_ptr, because one binding serves every character with the same
// skeleton and must live as long as any player uses it). Copyable value.

#include "opas/anim/ClipBinding.hpp"

#include <cstddef>
#include <memory>
#include <span>

namespace opas::anim
{

    struct PlayOptions
    {
        TimeMode mode = TimeMode::Loop;
        double speed = 1.0; // clip seconds per second; negative or non-finite counts as 0
        double startTimeS = 0.0; // wrapped (Loop) or clamped (Clamp) into the clip
    };

    // An event crossed during an advance. Points into the clip of the binding the
    // player was playing; valid while that binding lives (the player keeps it
    // alive until the next play() or stop()).
    struct FiredEvent
    {
        const ClipEvent* event = nullptr;
    };

    struct PlayerStep
    {
        RootMotionDelta rootMotion;
        std::size_t eventsFired = 0; // written to the events span, in order
        std::size_t eventsDropped = 0; // crossed but beyond the span's size
    };

    class ClipPlayer
    {
    public:
        // Starts playing from options.startTimeS. A null binding stops. Allocates
        // nothing.
        void play(std::shared_ptr<const ClipBinding> binding, const PlayOptions& options = {});
        void stop();
        [[nodiscard]] bool playing() const { return binding_ != nullptr; }

        void setSpeed(double speed); // negative or non-finite counts as 0
        [[nodiscard]] double speed() const { return speed_; }
        [[nodiscard]] TimeMode mode() const { return mode_; }
        // Clip time: in [0, durationS) with Loop, [0, durationS] with Clamp.
        [[nodiscard]] double timeS() const { return timeS_; }
        // Clamp only: the clip has reached durationS.
        [[nodiscard]] bool atEnd() const;
        [[nodiscard]] const ClipBinding* binding() const { return binding_.get(); }

        // Advances clip time by speed * dtS (dtS negative or non-finite counts as
        // 0) and returns the root motion over that clip-time span
        // (ClipBinding::rootMotion) and the events crossed, written in order to
        // `events` up to its size (the rest counted as dropped).
        //
        // Events: each advance covers the clip-time span [a, b) from the old time
        // to the new; an event at time t fires when t is in that span, so each
        // event fires once per pass over its time, an event at startTimeS fires on
        // the first advance that moves, and nothing fires while speed is 0. With
        // Loop a span that wraps continues from 0, and an advance longer than one
        // cycle fires every event once per cycle crossed (in time order; events at
        // equal times in clip order); an event at exactly durationS fires at each
        // wrap, before the events at 0. With Clamp the advance that reaches
        // durationS also fires events at durationS; after that (or when started
        // at durationS) nothing fires and root motion is zero. Not playing: returns an empty step.
        // Allocates nothing; the cost does not grow with the number of cycles
        // crossed beyond the events written.
        PlayerStep advance(double dtS, std::span<FiredEvent> events);

        // Samples the current time into localPose (ClipBinding::sample). Does
        // nothing when not playing.
        void sample(std::span<math::Transform> localPose) const;

    private:
        std::shared_ptr<const ClipBinding> binding_;
        TimeMode mode_ = TimeMode::Loop;
        double speed_ = 1.0;
        double timeS_ = 0.0;
    };

    // The clip-time advance ClipPlayer::advance makes, for a clip time the caller
    // keeps (a blend state keeps several clips in step by one phase, task 0020
    // C3d): advances timeS (in [0, durationS) with Loop, [0, durationS] with
    // Clamp) by deltaS clip seconds and returns the root motion and the events
    // crossed exactly as ClipPlayer::advance describes. deltaS <= 0 or
    // non-finite, or Clamp already at the end: an empty step and timeS unchanged.
    // Allocates nothing.
    PlayerStep advanceClipTime(
        const ClipBinding& binding, TimeMode mode, double& timeS, double deltaS, std::span<FiredEvent> events);

} // namespace opas::anim
