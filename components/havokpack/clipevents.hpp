#ifndef OPENEDEN_COMPONENTS_HAVOKPACK_CLIPEVENTS_H
#define OPENEDEN_COMPONENTS_HAVOKPACK_CLIPEVENTS_H

// Task 0170 (combat stage S5-2): which sidecar events a playing clip passes in one frame. Pure, header only, standard
// library only. Formats layer.
//
// Times. The sidecar stores an event's time as PLAYBACK time (docs/research/0054-provenance.md F48): the animation time
// of the event is the stored time multiplied by the generator's playback speed. A clip plays at that speed, so its
// playback clock is wall time: the event fires when the clock passes its stored time, the animation is sampled at
// clock * speed, and the clip ends when the clock reaches duration / speed. A speed that is not positive and finite
// plays as 1.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace HavokPack
{
    struct ClipMetaEvent
    {
        std::string mName;
        float mTime = 0.f; ///< playback seconds, as stored in the sidecar
    };

    /// What combat needs to know about one clip: its length, its speed and its events. Immutable once shared.
    struct ClipMeta
    {
        float mDuration = 0.f; ///< animation seconds (the .hkx clip's)
        float mSpeed = 1.f; ///< the generator's playback speed
        std::vector<ClipMetaEvent> mEvents; ///< ascending by mTime (sortEvents)

        [[nodiscard]] float speed() const { return (std::isfinite(mSpeed) && mSpeed > 0.f) ? mSpeed : 1.f; }
        /// Playback seconds from the first to the last frame.
        [[nodiscard]] float playbackDuration() const
        {
            return (std::isfinite(mDuration) && mDuration > 0.f) ? mDuration / speed() : 0.f;
        }
        /// The animation time to sample at a playback time (not wrapped).
        [[nodiscard]] float animationTime(float playbackTime) const { return playbackTime * speed(); }

        /// Stable sort by time; events whose time is not finite or is negative are dropped.
        void sortEvents()
        {
            std::erase_if(mEvents, [](const ClipMetaEvent& e) { return !std::isfinite(e.mTime) || e.mTime < 0.f; });
            std::stable_sort(mEvents.begin(), mEvents.end(),
                [](const ClipMetaEvent& a, const ClipMetaEvent& b) { return a.mTime < b.mTime; });
        }
    };

    /// What the renderer reports about the one-shot clip an actor plays (task 0170, sync): the clip's meta once read,
    /// whether the decoded clip has begun to be sampled, and how far into the clip (playback seconds) it was at its
    /// last sample. The combat clock starts from this, so events and the drawn pose agree.
    struct ClipProgress
    {
        std::shared_ptr<const ClipMeta> mMeta;
        bool mStarted = false;
        float mPlaybackTime = 0.f;
    };

    /// Advances a clip clock from `previous` to `current` (playback seconds; start a clip with previous = -1 so that
    /// events at time 0 fire) and calls `fire(const ClipMetaEvent&, float clipTime)` for every event with
    /// previous < time <= current, in time order (equal times in list order), also when one step passes several
    /// events. `clipTime` is the event's own time, never the frame's. Without `loop` events past the end of the clip
    /// are not fired and the result is true once `current` reaches the end (the events AT the end do fire first). With
    /// `loop` the clock wraps; at most kMaxLoopsPerStep wraps are followed in one call (longer steps drop the rest);
    /// the clip clock handed to `fire` is then the time inside the wrapped loop; the result is always false.
    inline constexpr int kMaxLoopsPerStep = 8;

    template <class Fire>
    bool advanceClipEvents(const ClipMeta& meta, float previous, float current, bool loop, Fire&& fire)
    {
        const float length = meta.playbackDuration();
        if (!std::isfinite(previous) || !std::isfinite(current) || current < previous)
            return false;
        const auto fireRange = [&](float after, float upTo) {
            // first event with time > after (events are sorted)
            auto first = std::upper_bound(meta.mEvents.begin(), meta.mEvents.end(), after,
                [](float value, const ClipMetaEvent& e) { return value < e.mTime; });
            for (; first != meta.mEvents.end() && first->mTime <= upTo; ++first)
                fire(*first, first->mTime);
        };
        if (!loop || length <= 0.f)
        {
            fireRange(previous, std::min(current, length > 0.f ? length : current));
            return length <= 0.f || current >= length;
        }
        float from = previous;
        float to = current;
        for (int i = 0; i < kMaxLoopsPerStep; ++i)
        {
            if (to <= length)
            {
                fireRange(from, to);
                return false;
            }
            fireRange(from, length);
            from = -1.f; // the next loop starts again at its time 0
            to -= length;
        }
        return false;
    }
}

#endif
