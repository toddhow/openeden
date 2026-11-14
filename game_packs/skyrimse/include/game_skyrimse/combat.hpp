#ifndef OPENEDEN_GAME_SKYRIMSE_COMBAT_H
#define OPENEDEN_GAME_SKYRIMSE_COMBAT_H

// The Skyrim SE pack's side of the combat hooks (task 0168 stage S5-0, task 0170 stage S5-2; decision 0018,
// sdk/include/openeden/game/combat_rules.hpp).
//
// S5-2 gives the slot table its clips for the one-handed sword (the other weapon types are empty: a later stage) and
// adds one test rule: with the pack setting `combat test swing = <editor id>` the named actors swing once every few
// seconds by playClip. There are no formulas, no hit, no damage yet (S5-1, S5-3). Events are counted and, for the test,
// remembered so that a trace can show them.
//
// No engine, renderer, Jolt, Qt or OSG type. Main thread only.

#include <openeden/game/actor_rules.hpp>
#include <openeden/game/combat_rules.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse
{
    /// Weapon animation types (WEAP DNAM +0).
    inline constexpr std::uint8_t kWeaponAnimHandToHand = 0;
    inline constexpr std::uint8_t kWeaponAnim1HSword = 1;

    inline constexpr std::string_view kCombatTestSwingKey = "combat test swing";
    inline constexpr std::size_t kMaxCombatTestSwingName = 128;

    /// Seconds before the first test swing and between two (the 1HM_AttackRight clip lasts 1.3 s).
    inline constexpr float kTestSwingFirstDelay = 3.f;
    inline constexpr float kTestSwingPeriod = 4.f;

    /// The table slot (attack, block, ...) x weapon animation type (WEAP DNAM +0, 0 to 9) -> clip file name.
    /// Default-constructed it is empty (the S5-0 skeleton); `humanoid()` is the shipped table. The names it returns point
    /// into static storage.
    class SlotTable
    {
    public:
        struct Entry
        {
            openeden::sdk::game::ClipSlot mSlot;
            std::uint8_t mWeaponAnimType;
            std::string_view mClip; ///< file name under meshes/actors/character/animations, no extension
        };

        SlotTable() = default;

        /// The humanoid table: one-handed sword (animation type 1) from the clips of Skyrim - Animations.bsa. The names
        /// were read from the archive listing (1hm_* files in the humanoid folder, which is not per sex) and the events
        /// from `animationdata/defaultmale.txt` (task 0170). RACE ATKE is not read (it names the event that starts an
        /// attack, which the clips' own sidecars already carry).
        [[nodiscard]] static SlotTable humanoid();

        /// Number of (slot, weapon type) entries.
        [[nodiscard]] std::size_t size() const { return mCount; }

        /// The clip for a slot and weapon animation type, or an empty view when there is none.
        [[nodiscard]] std::string_view clipFor(openeden::sdk::game::ClipSlot slot, std::uint8_t weaponAnimType) const
        {
            for (std::size_t i = 0; i < mCount; ++i)
                if (mEntries[i].mSlot == slot && mEntries[i].mWeaponAnimType == weaponAnimType)
                    return mEntries[i].mClip;
            return {};
        }

    private:
        const Entry* mEntries = nullptr;
        std::size_t mCount = 0;
    };

    /// CombatRules: the clip slots and, for the test, a periodic swing.
    class SkyrimCombat final : public openeden::sdk::game::CombatRules
    {
    public:
        SkyrimCombat()
            : mSlots(SlotTable::humanoid())
        {
        }

        void onClipEvent(const openeden::sdk::game::ClipEvent& event) override;
        void onSweepResult(openeden::sdk::game::CombatSubject subject, std::uint32_t token,
            const openeden::sdk::game::SweepHit* hits, std::size_t count) override;
        void combatUpdate(float seconds, openeden::sdk::game::CombatRequestWriter& writer) override;
        void onClipFinished(
            openeden::sdk::game::CombatSubject subject, std::uint32_t token, openeden::sdk::game::ClipEnd how) override;

        /// Names the editor id of the test swingers (case-insensitive); empty switches the rule off. A value longer than
        /// kMaxCombatTestSwingName is refused (false, the rule stays off).
        bool setTestSwing(std::string_view editorId);
        [[nodiscard]] const std::string& testSwing() const { return mTestSwing; }

        /// The actor hooks forwarded by the cell source (the combat rules need the handles of the actors).
        void onActorAdded(const openeden::sdk::game::ActorInfo& info);
        void onActorRemoved(openeden::sdk::game::ActorHandle handle);

        [[nodiscard]] const SlotTable& slots() const { return mSlots; }
        [[nodiscard]] std::size_t updates() const { return mUpdates; }
        [[nodiscard]] std::size_t refusedEvents() const { return mRefusedEvents; }
        [[nodiscard]] std::size_t refusedSweepResults() const { return mRefusedSweepResults; }
        [[nodiscard]] std::size_t swingers() const { return mSwingers.size(); }
        [[nodiscard]] std::size_t swingsRequested() const { return mSwingsRequested; }
        [[nodiscard]] std::size_t eventsSeen() const { return mEventsSeen; }
        [[nodiscard]] std::size_t clipsFinished() const { return mClipsFinished; }
        [[nodiscard]] std::size_t clipsCompleted() const { return mClipsCompleted; }

    private:
        SlotTable mSlots;
        std::string mTestSwing; ///< lower case
        std::vector<openeden::sdk::game::ActorHandle> mSwingers;
        float mClock = 0.f;
        float mNextSwing = kTestSwingFirstDelay;
        std::uint32_t mNextToken = 1;
        std::size_t mUpdates = 0;
        std::size_t mRefusedEvents = 0;
        std::size_t mRefusedSweepResults = 0;
        std::size_t mSwingsRequested = 0;
        std::size_t mEventsSeen = 0;
        std::size_t mClipsFinished = 0;
        std::size_t mClipsCompleted = 0;
    };
}

#endif
