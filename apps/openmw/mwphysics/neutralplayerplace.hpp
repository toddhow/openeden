#ifndef OPENMW_MWPHYSICS_NEUTRALPLAYERPLACE_H
#define OPENMW_MWPHYSICS_NEUTRALPLAYERPLACE_H

#include <openeden/game/player_place.hpp>

namespace MWPhysics
{
    /// Task 0211: tells whether the player's place changed since the last one handed on. A place that names nothing (an
    /// interior with no cell id, an exterior with no worldspace) is never handed on. Pure; main thread.
    class PlayerPlaceFilter
    {
    public:
        /// True when `place` is valid and differs from the last valid one that was accepted (it is remembered then).
        bool accept(const openeden::sdk::game::PlayerPlace& place);

        /// Forgets the last place: the next valid one is accepted again.
        void reset() { mHave = false; }

        [[nodiscard]] bool valid(const openeden::sdk::game::PlayerPlace& place) const;

    private:
        openeden::sdk::game::PlayerPlace mLast;
        bool mHave = false;
    };
}

#endif
