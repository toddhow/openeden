#ifndef OPENEDEN_GAME_SKYRIMSE_POISON_H
#define OPENEDEN_GAME_SKYRIMSE_POISON_H

// Poisons on weapons (task 0229, milestone M-S7). A poison is an ALCH (or a potion made at an alchemy lab) whose first
// effect is hostile. The player applies one to the weapon he wields from the inventory window; the weapon then carries
// it for a number of landed hits (`CraftingSettings::mPoisonCharges`, ours: the data has no dose count, only the
// messages sPoisonAlreadyPoisonedMessage, sPoisonNoWeaponMessage and sPoisonBowConfirmMessage), and each landed hit
// applies its effects to the target through the magic runtime of task 0186 (combat_poison.cpp).
//
// This header is the pure part: which weapon carries which poison with how many charges, and the SpellRecord the
// combat rules apply. Nothing here allocates per frame; the table is bounded. Main thread only.

#include <game_skyrimse/crafting.hpp>
#include <game_skyrimse/perks.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace game_skyrimse::crafting
{
    /// The poison a weapon carries.
    struct WeaponPoison
    {
        FormId mPoison = kNullFormId; ///< the item the poison came from
        std::string mName;
        std::vector<EffectEntry> mEffects;
        std::int32_t mCharges = 0; ///< landed hits it still poisons
    };

    enum class CoatStatus : std::uint8_t
    {
        Ok,
        AlreadyPoisoned, ///< the weapon carries a poison already (Skyrim: sPoisonAlreadyPoisonedMessage)
        NoCharges, ///< the poison to apply has no charge
        BadArgument, ///< no weapon, no effects, or the table is full
    };

    /// Which weapon item carries which poison.
    class PoisonCoatings
    {
    public:
        static constexpr std::size_t kMax = 64;
        static constexpr std::size_t kMaxEffects = 8;

        CoatStatus apply(FormId weapon, WeaponPoison poison);
        [[nodiscard]] const WeaponPoison* find(FormId weapon) const;
        /// One charge used. Returns the charges left (the coating ends at 0), or -1 when the weapon carries none.
        int spend(FormId weapon);
        void clear() { mCoats.clear(); }
        [[nodiscard]] std::size_t size() const { return mCoats.size(); }

    private:
        struct Entry
        {
            FormId mWeapon = kNullFormId;
            WeaponPoison mPoison;
        };
        std::vector<Entry> mCoats;
    };

    /// The spell the combat rules apply on a hit: a touch, fire-and-forget spell of the poison's effects, named after
    /// the poison, with the poison's item as its id.
    [[nodiscard]] SpellRecord poisonSpell(const WeaponPoison& poison);
}

#endif
