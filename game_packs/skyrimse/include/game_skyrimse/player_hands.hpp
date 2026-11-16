#pragma once

// Task 0249 (the player's first-person hands, third slice): what the pack tells the core about the hands that is not a
// swing or a cast: the bow's draw and release, and a spell he is holding (a concentration spell, a ward that goes on).
// Pure rules; the state itself is read from SkyrimCombat::playerHands() (combat_hands.cpp) and reaches the core through
// openeden::sdk::game::PlayerBodyState (player_body.hpp 0.3).

#include <cstdint>
#include <string>
#include <string_view>

namespace game_skyrimse
{
    /// What the player does with a spell he holds: nothing; charging (pressed, the spell has not gone yet); holding (a
    /// concentration spell or a ward that is on). The numbers are those of PlayerBodyState::castHold.
    enum class HandsHold : std::uint8_t
    {
        None = 0,
        Charging = 1,
        Holding = 2,
    };

    /// The first-person loop clip (a name of `_1stperson/animations`, VERIFIED in Skyrim - Animations.bsa) the hands
    /// play during a hold; empty for None. `self` is a spell on the caster (delivery Self), `ward` the ward spell.
    [[nodiscard]] std::string_view handsHoldClip(HandsHold hold, bool self, bool ward);

    /// How long a hold has lasted, on the combat clock: `update` is called once per combat update with the hold now.
    class HandsHoldClock
    {
    public:
        void update(HandsHold hold, float now);
        [[nodiscard]] HandsHold hold() const { return mHold; }
        /// Seconds since the hold began (0 for None); never negative.
        [[nodiscard]] float seconds(float now) const;

    private:
        HandsHold mHold = HandsHold::None;
        float mSince = 0.f;
    };

    /// What SkyrimCombat::playerHands() answers: a snapshot, safe to read from the main thread.
    struct PlayerHands
    {
        std::uint32_t bowDrawToken = 0;
        std::uint32_t bowReleaseToken = 0;
        bool bowDrawing = false;
        std::string bowDrawClip;
        std::string bowReleaseClip;
        std::uint8_t castHold = 0;
        float castHoldSeconds = 0.f;
        std::string castHoldClip;
    };
}
