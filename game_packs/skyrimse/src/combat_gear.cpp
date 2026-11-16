// Real weapons and shields in combat (task 0214): see combat_gear.hpp.
#include <game_skyrimse/combat_gear.hpp>

#include <algorithm>
#include <utility>

namespace game_skyrimse
{
    namespace
    {
        /// How many leveled lists deep an item may be, and how many items one NPC's search looks at.
        constexpr unsigned kMaxListDepth = 4;
        constexpr std::size_t kMaxItemsSearched = 1024;

        bool betterArmour(const ArmourPiece& a, const ArmourPiece& b)
        {
            if (a.mRating != b.mRating)
                return a.mRating > b.mRating;
            return a.mForm < b.mForm;
        }
    }

    std::uint8_t meleeClipClass(std::uint8_t weaponAnimType)
    {
        if (weaponAnimType == WeaponAnimType::TwoHandSword)
            return kClipClassTwoHandedSword;
        if (weaponAnimType == WeaponAnimType::TwoHandAxe)
            return kClipClassTwoHandedAxe;
        return kClipClassOneHanded;
    }

    bool isTwoHanded(std::uint8_t weaponAnimType)
    {
        return weaponAnimType == WeaponAnimType::TwoHandSword || weaponAnimType == WeaponAnimType::TwoHandAxe;
    }

    bool isFightingWeapon(const WeaponRecord& weapon)
    {
        return weapon.mHasData && weapon.mHasDnam && weapon.isMelee() && weapon.mAnimType != WeaponAnimType::HandToHand;
    }

    const WeaponRecord* chooseMeleeWeapon(const std::vector<const WeaponRecord*>& candidates)
    {
        const WeaponRecord* best = nullptr;
        for (const WeaponRecord* candidate : candidates)
        {
            if (candidate == nullptr || !isFightingWeapon(*candidate))
                continue;
            if (best == nullptr || candidate->mDamage > best->mDamage
                || (candidate->mDamage == best->mDamage && candidate->mCommon.mId < best->mCommon.mId))
                best = candidate;
        }
        return best;
    }

    bool isShield(const ArmourPiece& piece)
    {
        return (piece.mSlotMask & ArmourSlot::Shield) != 0;
    }

    const ArmourCandidate* chooseShield(const std::vector<ArmourCandidate>& candidates)
    {
        const ArmourCandidate* best = nullptr;
        for (const ArmourCandidate& candidate : candidates)
            if (isShield(candidate.mPiece) && (best == nullptr || betterArmour(candidate.mPiece, best->mPiece)))
                best = &candidate;
        return best;
    }

    std::vector<ArmourPiece> chooseExtraArmour(
        const std::vector<ArmourCandidate>& candidates, const std::vector<ArmourPiece>& worn)
    {
        std::uint32_t covered = 0;
        for (const ArmourPiece& piece : worn)
            covered |= piece.mSlotMask;
        std::vector<const ArmourPiece*> order;
        for (const ArmourCandidate& candidate : candidates)
            if (!isShield(candidate.mPiece) && candidate.mPiece.mSlotMask != 0 && candidate.mPiece.mRating > 0.f)
                order.push_back(&candidate.mPiece);
        std::sort(order.begin(), order.end(),
            [](const ArmourPiece* a, const ArmourPiece* b) { return betterArmour(*a, *b); });
        std::vector<ArmourPiece> out;
        for (const ArmourPiece* piece : order)
        {
            if ((piece->mSlotMask & covered) != 0)
                continue;
            covered |= piece->mSlotMask;
            out.push_back(*piece);
        }
        return out;
    }

    bool resolveFighterGear(const ActorIndex& actors, const CombatIndex& combat, FormId npc, std::uint32_t variation,
        std::uint16_t level, const std::vector<ArmourPiece>& worn, FighterGear& out)
    {
        out = {};
        ActorIndex::Resolved resolved;
        if (actors.resolve(npc, level, variation, resolved) != ActorIndex::ResolveStatus::Ok)
            return false;

        // The items to look at, with the leveled lists between them and a real item followed through.
        struct Pending
        {
            FormId mItem;
            unsigned mDepth;
            bool mFromOutfit;
        };
        std::vector<Pending> work;
        for (const InventoryEntry& entry : resolved.mInventory)
            if (entry.mCount > 0)
                work.push_back({ entry.mItem, 0, false });
        if (const OutfitRecord* outfit = actors.outfit(resolved.mDefaultOutfit))
            for (const FormId item : outfit->mItems)
                work.push_back({ item, 0, true });
        std::reverse(work.begin(), work.end()); // pop from the back in the listed order

        std::vector<const WeaponRecord*> weapons;
        std::vector<ArmourCandidate> shields; // from anywhere
        std::vector<ArmourCandidate> inventoryArmour; // from the inventory only
        std::size_t looked = 0;
        while (!work.empty() && looked++ < kMaxItemsSearched)
        {
            const Pending item = work.back();
            work.pop_back();
            if (const WeaponRecord* weapon = combat.weapon(item.mItem))
            {
                weapons.push_back(weapon);
            }
            else if (const ArmorExtra* armor = actors.armor(item.mItem))
            {
                ArmourCandidate candidate;
                candidate.mPiece = { static_cast<float>(armor->mRating) / 100.f, armor->mSlotMask, armor->mArmorType,
                    item.mItem, armor->mKeywords };
                candidate.mBlockMaterial = armor->mBlockMaterial;
                if (isShield(candidate.mPiece))
                    shields.push_back(candidate);
                else if (!item.mFromOutfit)
                    inventoryArmour.push_back(candidate);
            }
            else if (item.mDepth < kMaxListDepth)
            {
                const LeveledList* list = actors.leveledList(item.mItem);
                if (list != nullptr && (list->mFlags & LeveledFlags::UseAll) != 0)
                {
                    // A list that gives all its entries (a guard's sword AND shield): every one that the level allows.
                    // Chance none is not drawn: an NPC's gear is the same in every session.
                    for (const LeveledEntry& entry : list->mEntries)
                        if (entry.mLevel <= level)
                            work.push_back({ entry.mReference, item.mDepth + 1, item.mFromOutfit });
                }
                else if (const LeveledEntry* entry = actors.pickLeveled(item.mItem, level, variation))
                    work.push_back({ entry->mReference, item.mDepth + 1, item.mFromOutfit });
            }
        }

        out.mKnown = true;
        out.mWeapon = chooseMeleeWeapon(weapons);
        if (out.mWeapon == nullptr || !isTwoHanded(out.mWeapon->mAnimType))
        {
            if (const ArmourCandidate* shield = chooseShield(shields))
            {
                out.mHasShield = true;
                out.mShield = shield->mPiece;
                out.mShieldBlockMaterial = shield->mBlockMaterial;
            }
        }
        out.mExtraPieces = chooseExtraArmour(inventoryArmour, worn);
        return true;
    }
}
