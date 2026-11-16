#ifndef OPENMW_MWWORLD_NEUTRALFADE_H
#define OPENMW_MWWORLD_NEUTRALFADE_H

#include <cstddef>
#include <cstdint>
#include <vector>

// Fades of neutral objects (task 0202): the pure schedule behind Papyrus' Enable(abFadeIn) and Disable(abFadeOut).
//
// A fade out keeps the object enabled and takes its alpha from where it is (1 for an object that is not fading) down to
// 0
// in a straight line over the given time; when it reaches 0 the caller disables the object and puts the alpha back to
// 1,
// so a later Enable shows it opaque. A fade in is the other way round: the caller enables the object at once and the
// alpha
// goes 0 to 1. A new fade of an object that is already fading replaces the old one and starts from its current alpha,
// so
// the picture never jumps. No clock: the caller gives the time that passed. Main thread only; nothing allocates after
// the
// first few fades (the storage grows by doubling and is reserved).

namespace MWWorld
{
    class NeutralFades
    {
    public:
        /// One fade's alpha after a step.
        struct Update
        {
            std::uint32_t mPlacement = 0;
            float mAlpha = 1.f;
            bool mFadeIn = false;
            /// The fade ended with this step: a fade out reached 0 (the caller disables the object and restores the
            /// alpha
            /// to 1), a fade in reached 1 (the caller drops the alpha override). The fade is gone from the schedule.
            bool mFinished = false;
        };

        /// At most this many objects fade at once; a start beyond it is refused (the caller then changes the state at
        /// once).
        static constexpr std::size_t kMaxFades = 1024;
        /// The longest fade, in seconds; a longer time is cut to it.
        static constexpr float kMaxSeconds = 60.f;

        /// Starts or replaces a fade. A time that is not a positive finite number is refused (false): no fade, the
        /// caller
        /// changes the state at once. False too when kMaxFades fades are running and this object is not one of them.
        [[nodiscard]] bool start(std::uint32_t placement, bool fadeIn, float seconds);

        /// Moves every fade on by `dt` seconds (a negative or non-finite dt moves nothing), appends one update per fade
        /// that was running, in the order the fades were started, and drops the finished ones.
        void advance(float dt, std::vector<Update>& out);

        /// Drops the fade of an object without a last update (the caller changes its state at once). False when it did
        /// not fade.
        bool cancel(std::uint32_t placement);

        [[nodiscard]] bool fading(std::uint32_t placement) const;
        [[nodiscard]] std::size_t size() const noexcept { return mFades.size(); }
        void clear() noexcept { mFades.clear(); }

    private:
        struct Fade
        {
            std::uint32_t mPlacement = 0;
            bool mFadeIn = false;
            float mFrom = 0.f;
            float mTo = 0.f;
            float mSeconds = 1.f;
            float mElapsed = 0.f;
        };
        std::vector<Fade> mFades;
    };
}

#endif
