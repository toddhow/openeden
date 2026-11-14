#ifndef OPENEDEN_GAME_SKYRIMSE_COMBAT_H
#define OPENEDEN_GAME_SKYRIMSE_COMBAT_H

// The Skyrim SE pack's side of the combat hooks (task 0168, milestone M-S5 stage S5-0, decision 0018,
// sdk/include/openeden/game/combat_rules.hpp). This is the SKELETON: it implements the interface with no behaviour. It
// refuses and counts what the core tells it, writes nothing, and has an empty slot table. Stage S5-2 gives the slot
// table its clips and S5-3 the melee hit; nothing in the engine calls it yet.
//
// No engine, renderer, Jolt, Qt or OSG type. Main thread only.

#include <openeden/game/combat_rules.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace game_skyrimse
{
    /// The table slot (attack, block, ...) x weapon animation type (WEAP DNAM +0, 0 to 9) -> clip file name. Empty in
    /// S5-0; S5-2 fills it from the clip names and RACE ATKE. The names it returns point into the table.
    class SlotTable
    {
    public:
        /// Number of (slot, weapon type) entries.
        [[nodiscard]] std::size_t size() const { return 0; }

        /// The clip for a slot and weapon animation type, or an empty view when there is none.
        [[nodiscard]] std::string_view clipFor(openeden::sdk::game::ClipSlot slot, std::uint8_t weaponAnimType) const
        {
            (void)slot;
            (void)weaponAnimType;
            return {};
        }
    };

    /// CombatRules with no behaviour: counts the calls it ignores.
    class SkyrimCombat final : public openeden::sdk::game::CombatRules
    {
    public:
        void onClipEvent(const openeden::sdk::game::ClipEvent& event) override;
        void onSweepResult(openeden::sdk::game::ActorHandle actor, std::uint32_t token,
            const openeden::sdk::game::SweepHit* hits, std::size_t count) override;
        void combatUpdate(float seconds, openeden::sdk::game::CombatRequestWriter& writer) override;

        [[nodiscard]] const SlotTable& slots() const { return mSlots; }
        [[nodiscard]] std::size_t updates() const { return mUpdates; }
        [[nodiscard]] std::size_t refusedEvents() const { return mRefusedEvents; }
        [[nodiscard]] std::size_t refusedSweepResults() const { return mRefusedSweepResults; }

    private:
        SlotTable mSlots;
        std::size_t mUpdates = 0;
        std::size_t mRefusedEvents = 0;
        std::size_t mRefusedSweepResults = 0;
    };
}

#endif
