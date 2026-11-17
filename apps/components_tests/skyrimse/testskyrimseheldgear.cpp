// Task 0220 (Skyrim actors show their gear): the pack half. (1) A "use all" leveled list in an outfit (a guard's whole
// set of armour is ONE outfit item) gives every entry the level allows; the first slice picked a single entry, which
// is why a Whiterun guard stood in the skin of its body with one piece of armour. (2) With `held gear` the appearance
// names the actor's melee weapon and shield by the rule of task 0214, and the shield leaves the part list. Hand-built
// records; no plugin bytes, no game data.
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/appearance.hpp>
#include <game_skyrimse/combat_gear.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using openeden::sdk::game::ActorAppearance;
    using openeden::sdk::game::ActorPartKind;

    constexpr FormId kRace = 0x10;
    constexpr FormId kOtherRace = 0x11;
    constexpr FormId kNpc = 0x100;
    constexpr FormId kOutfit = 0x300;
    constexpr FormId kSkin = 0x200;
    constexpr FormId kCuirass = 0x400;
    constexpr FormId kBoots = 0x401;
    constexpr FormId kHelm = 0x402;
    constexpr FormId kShieldA = 0x410; // rating 1.0
    constexpr FormId kShieldB = 0x411; // rating 2.0
    constexpr FormId kSkinArma = 0x500;
    constexpr FormId kCuirassArma = 0x501;
    constexpr FormId kBootsArma = 0x502;
    constexpr FormId kHelmArma = 0x503;
    constexpr FormId kShieldAArma = 0x510;
    constexpr FormId kShieldBArma = 0x511;
    constexpr FormId kShieldBOtherRaceOnly = 0x512;
    constexpr FormId kSword = 0x600;
    constexpr FormId kGreatsword = 0x601;
    constexpr FormId kBow = 0x602;
    constexpr FormId kFists = 0x603;
    constexpr FormId kSword2 = 0x604;
    constexpr FormId kList = 0x700;
    constexpr FormId kInner = 0x701;

    constexpr std::uint32_t kSlotBody = 0x4;
    constexpr std::uint32_t kSlotFeet = 0x80;
    constexpr std::uint32_t kSlotHead = 0x1;
    constexpr std::uint32_t kSlotShield = 0x200;

    ArmatureRecord armature(FormId id, FormId race, std::uint32_t slots, std::string model)
    {
        ArmatureRecord a;
        a.mCommon.mId = id;
        a.mRace = race;
        a.mSlotMask = slots;
        a.mMaleModel = model;
        a.mFemaleModel = "f_" + model;
        return a;
    }

    ArmorExtra armor(FormId id, std::uint32_t slots, std::vector<FormId> armatures, std::uint32_t rating = 0)
    {
        ArmorExtra a;
        a.mCommon.mId = id;
        a.mSlotMask = slots;
        a.mArmatures = std::move(armatures);
        a.mRating = rating;
        return a;
    }

    WeaponLook weapon(FormId id, std::uint8_t type, std::uint16_t damage, std::string model)
    {
        WeaponLook w;
        w.mCommon.mId = id;
        w.mModel = std::move(model);
        w.mHasDnam = true;
        w.mHasData = true;
        w.mAnimType = type;
        w.mDamage = damage;
        return w;
    }

    LeveledList list(FormId id, std::uint8_t flags, std::vector<LeveledEntry> entries)
    {
        LeveledList l;
        l.mCommon.mId = id;
        l.mFlags = flags;
        l.mEntries = std::move(entries);
        return l;
    }

    // One race with a skin, three armour pieces, two shields (one fitting only another race), weapons.
    struct World
    {
        ActorRecords mRecords;
        std::vector<LeveledList> mLists;
        ActorNpc mNpc;

        World()
        {
            RaceRecord race;
            race.mCommon.mId = kRace;
            race.mSkeleton[0] = "actors\\character\\character assets\\skeleton.nif";
            race.mSkeleton[1] = "actors\\character\\character assets female\\skeleton_female.nif";
            race.mSkin = kSkin;
            mRecords.mRaces.push_back(race);

            mRecords.mArmatures.push_back(armature(kSkinArma, kRace, kSlotBody, "skin.nif"));
            mRecords.mArmatures.push_back(armature(kCuirassArma, kRace, kSlotBody, "cuirass.nif"));
            mRecords.mArmatures.push_back(armature(kBootsArma, kRace, kSlotFeet, "boots.nif"));
            mRecords.mArmatures.push_back(armature(kHelmArma, kRace, kSlotHead, "helm.nif"));
            mRecords.mArmatures.push_back(armature(kShieldAArma, kRace, kSlotShield, "shielda.nif"));
            mRecords.mArmatures.push_back(armature(kShieldBArma, kRace, kSlotShield, "shieldb.nif"));
            mRecords.mArmatures.push_back(armature(kShieldBOtherRaceOnly, kOtherRace, kSlotShield, "shieldx.nif"));
            mRecords.mArmors.push_back(armor(kSkin, kSlotBody, { kSkinArma }));
            mRecords.mArmors.push_back(armor(kCuirass, kSlotBody, { kCuirassArma }, 500));
            mRecords.mArmors.push_back(armor(kBoots, kSlotFeet, { kBootsArma }, 100));
            mRecords.mArmors.push_back(armor(kHelm, kSlotHead, { kHelmArma }, 100));
            mRecords.mArmors.push_back(armor(kShieldA, kSlotShield, { kShieldAArma }, 100));
            mRecords.mArmors.push_back(armor(kShieldB, kSlotShield, { kShieldBArma }, 200));

            mRecords.mWeapons.push_back(weapon(kSword, 1, 7, "weapons\\sword.nif"));
            mRecords.mWeapons.push_back(weapon(kSword2, 1, 7, "weapons\\sword2.nif"));
            mRecords.mWeapons.push_back(weapon(kGreatsword, 5, 21, "weapons\\greatsword.nif"));
            mRecords.mWeapons.push_back(weapon(kBow, 7, 30, "weapons\\bow.nif"));
            mRecords.mWeapons.push_back(weapon(kFists, 0, 40, "weapons\\fists.nif"));

            mNpc.mCommon.mId = kNpc;
            mNpc.mRace = kRace;
            mNpc.mHasConfig = true;
            mNpc.mDefaultOutfit = kOutfit;
        }

        void outfit(std::vector<FormId> items)
        {
            OutfitRecord o;
            o.mCommon.mId = kOutfit;
            o.mItems = std::move(items);
            mRecords.mOutfits.push_back(std::move(o));
        }

        ActorIndex index()
        {
            ActorRecords records = mRecords;
            records.mNpcs.push_back(mNpc);
            ActorIndex out;
            out.add(records);
            out.addLeveledLists(mLists);
            return out;
        }
    };

    std::vector<std::string> models(const ActorAppearance& a, ActorPartKind kind)
    {
        std::vector<std::string> out;
        for (const auto& p : a.parts)
            if (p.kind == kind)
                out.push_back(p.modelPath);
        return out;
    }

    bool has(const std::vector<std::string>& v, const std::string& s)
    {
        return std::find(v.begin(), v.end(), s) != v.end();
    }

    ActorAppearance describe(ActorIndex& index, bool held)
    {
        ActorAppearance a;
        std::string error;
        AppearanceLimits limits;
        limits.mHeldGear = held;
        EXPECT_TRUE(makeActorAppearance(index, kNpc, 1, a, error, limits)) << error;
        return a;
    }
}

// ---- the outfit's use-all list -------------------------------------------------------------------------------------

TEST(SkyrimOutfitUseAll, a_use_all_list_in_an_outfit_gives_every_entry_the_level_allows)
{
    World w;
    w.mLists.push_back(list(kList, LeveledFlags::UseAll, { { 1, kCuirass, 1 }, { 1, kBoots, 1 }, { 1, kHelm, 1 } }));
    w.outfit({ kList });
    ActorIndex index = w.index();
    const auto worn = index.wornModels(kOutfit, kRace, Sex::Male, kAppearanceLevel, 1);
    ASSERT_EQ(worn.size(), 3u);
    EXPECT_EQ(worn[0].mModel, "cuirass.nif"); // the listed order
    EXPECT_EQ(worn[1].mModel, "boots.nif");
    EXPECT_EQ(worn[2].mModel, "helm.nif");
}

TEST(SkyrimOutfitUseAll, an_entry_above_the_level_is_left_out)
{
    World w;
    w.mLists.push_back(list(kList, LeveledFlags::UseAll, { { 1, kCuirass, 1 }, { kAppearanceLevel + 1, kBoots, 1 } }));
    w.outfit({ kList });
    const auto worn = w.index().wornModels(kOutfit, kRace, Sex::Male, kAppearanceLevel, 1);
    ASSERT_EQ(worn.size(), 1u);
    EXPECT_EQ(worn[0].mModel, "cuirass.nif");
}

TEST(SkyrimOutfitUseAll, a_list_without_the_flag_still_gives_one_entry)
{
    World w;
    w.mLists.push_back(list(kList, 0, { { 1, kCuirass, 1 }, { 1, kBoots, 1 }, { 1, kHelm, 1 } }));
    w.outfit({ kList });
    EXPECT_EQ(w.index().wornModels(kOutfit, kRace, Sex::Male, kAppearanceLevel, 1).size(), 1u);
}

TEST(SkyrimOutfitUseAll, a_use_all_list_inside_a_use_all_list_gives_all_of_both)
{
    World w;
    w.mLists.push_back(list(kInner, LeveledFlags::UseAll, { { 1, kBoots, 1 }, { 1, kHelm, 1 } }));
    w.mLists.push_back(list(kList, LeveledFlags::UseAll, { { 1, kCuirass, 1 }, { 1, kInner, 1 } }));
    w.outfit({ kList });
    const auto worn = w.index().wornModels(kOutfit, kRace, Sex::Male, kAppearanceLevel, 1);
    ASSERT_EQ(worn.size(), 3u);
    EXPECT_EQ(worn[0].mModel, "cuirass.nif");
    EXPECT_EQ(worn[1].mModel, "boots.nif");
    EXPECT_EQ(worn[2].mModel, "helm.nif");
}

TEST(SkyrimOutfitUseAll, a_guard_with_a_use_all_outfit_wears_all_of_it_in_its_appearance)
{
    World w;
    w.mLists.push_back(list(kList, LeveledFlags::UseAll, { { 1, kCuirass, 1 }, { 1, kBoots, 1 }, { 1, kHelm, 1 } }));
    w.outfit({ kList });
    ActorIndex index = w.index();
    const ActorAppearance a = describe(index, false);
    const auto armour = models(a, ActorPartKind::Armor);
    EXPECT_TRUE(has(armour, "cuirass.nif"));
    EXPECT_TRUE(has(armour, "boots.nif"));
    EXPECT_TRUE(has(armour, "helm.nif"));
    // The cuirass covers the body slot, so the skin body is not drawn under it.
    EXPECT_FALSE(has(models(a, ActorPartKind::Skin), "skin.nif"));
}

// ---- held gear -----------------------------------------------------------------------------------------------------

TEST(SkyrimHeldGear, off_the_appearance_names_nothing_and_keeps_the_shield_part)
{
    World w;
    w.outfit({ kCuirass, kShieldA });
    w.mNpc.mInventory.push_back({ kSword, 1 });
    ActorIndex index = w.index();
    const ActorAppearance a = describe(index, false);
    EXPECT_TRUE(a.heldWeaponModel.empty());
    EXPECT_EQ(a.heldWeaponType, 0);
    EXPECT_TRUE(a.heldShieldModel.empty());
    EXPECT_TRUE(has(models(a, ActorPartKind::Armor), "shielda.nif"));
}

TEST(SkyrimHeldGear, on_the_weapon_and_the_shield_are_named_and_the_shield_leaves_the_parts)
{
    World w;
    w.outfit({ kCuirass, kShieldA });
    w.mNpc.mInventory.push_back({ kSword, 1 });
    ActorIndex index = w.index();
    const ActorAppearance a = describe(index, true);
    EXPECT_EQ(a.heldWeaponModel, "weapons\\sword.nif");
    EXPECT_EQ(a.heldWeaponType, 1);
    EXPECT_EQ(a.heldShieldModel, "shielda.nif");
    const auto armour = models(a, ActorPartKind::Armor);
    EXPECT_FALSE(has(armour, "shielda.nif"));
    EXPECT_TRUE(has(armour, "cuirass.nif")); // other armour stays
}

TEST(SkyrimHeldGear, the_shield_model_is_the_womans_for_a_woman)
{
    World w;
    w.outfit({ kShieldA });
    w.mNpc.mFlags = NpcFlags::Female;
    ActorIndex index = w.index();
    EXPECT_EQ(describe(index, true).heldShieldModel, "f_shielda.nif");
}

TEST(SkyrimHeldGear, the_best_weapon_is_the_highest_damage_then_the_lowest_form_id)
{
    World w;
    w.outfit({});
    w.mNpc.mInventory = { { kSword2, 1 }, { kSword, 1 } };
    ActorIndex index = w.index();
    HeldGear held;
    ASSERT_TRUE(resolveHeldGear(index, kNpc, 1, kAppearanceLevel, held));
    EXPECT_EQ(held.mWeapon, kSword); // same damage as kSword2: the lower id
    w.mNpc.mInventory = { { kSword, 1 }, { kGreatsword, 1 } };
    ActorIndex index2 = w.index();
    ASSERT_TRUE(resolveHeldGear(index2, kNpc, 1, kAppearanceLevel, held));
    EXPECT_EQ(held.mWeapon, kGreatsword);
    EXPECT_EQ(held.mWeaponType, 5);
}

TEST(SkyrimHeldGear, a_bow_and_bare_hands_are_not_held)
{
    World w;
    w.outfit({});
    w.mNpc.mInventory = { { kBow, 1 }, { kFists, 1 } };
    ActorIndex index = w.index();
    HeldGear held;
    ASSERT_TRUE(resolveHeldGear(index, kNpc, 1, kAppearanceLevel, held));
    EXPECT_TRUE(held.mWeaponModel.empty());
    EXPECT_EQ(held.mWeapon, kNullFormId);
}

TEST(SkyrimHeldGear, the_best_shield_is_the_highest_rating)
{
    World w;
    w.outfit({ kShieldA, kShieldB });
    ActorIndex index = w.index();
    HeldGear held;
    ASSERT_TRUE(resolveHeldGear(index, kNpc, 1, kAppearanceLevel, held));
    EXPECT_EQ(held.mShield, kShieldB);
    EXPECT_EQ(held.mShieldModel, "shieldb.nif");
}

TEST(SkyrimHeldGear, a_two_handed_weapon_has_no_shield)
{
    World w;
    w.outfit({ kShieldA });
    w.mNpc.mInventory = { { kGreatsword, 1 } };
    ActorIndex index = w.index();
    const ActorAppearance a = describe(index, true);
    EXPECT_EQ(a.heldWeaponModel, "weapons\\greatsword.nif");
    EXPECT_TRUE(a.heldShieldModel.empty());
    EXPECT_TRUE(has(models(a, ActorPartKind::Armor), "shielda.nif")); // left as it was: nothing took it
}

TEST(SkyrimHeldGear, a_use_all_list_gives_the_sword_and_the_shield_of_a_guard)
{
    World w;
    w.mLists.push_back(list(kList, LeveledFlags::UseAll, { { 1, kSword, 1 }, { 1, kShieldA, 1 } }));
    w.outfit({ kCuirass });
    w.mNpc.mInventory = { { kList, 1 } };
    ActorIndex index = w.index();
    const ActorAppearance a = describe(index, true);
    EXPECT_EQ(a.heldWeaponModel, "weapons\\sword.nif");
    EXPECT_EQ(a.heldShieldModel, "shielda.nif");
}

TEST(SkyrimHeldGear, a_shield_with_no_addon_for_the_race_is_not_held)
{
    World w;
    w.mRecords.mArmors.push_back(armor(0x420, kSlotShield, { kShieldBOtherRaceOnly }, 300));
    w.outfit({ 0x420 });
    ActorIndex index = w.index();
    HeldGear held;
    ASSERT_TRUE(resolveHeldGear(index, kNpc, 1, kAppearanceLevel, held));
    EXPECT_TRUE(held.mShieldModel.empty());
    EXPECT_EQ(held.mShield, kNullFormId);
}

TEST(SkyrimHeldGear, an_unknown_npc_is_refused_and_a_naked_one_holds_nothing)
{
    World w;
    w.outfit({});
    ActorIndex index = w.index();
    HeldGear held;
    EXPECT_FALSE(resolveHeldGear(index, 0x999, 1, kAppearanceLevel, held));
    EXPECT_TRUE(resolveHeldGear(index, kNpc, 1, kAppearanceLevel, held));
    EXPECT_TRUE(held.mWeaponModel.empty());
    EXPECT_TRUE(held.mShieldModel.empty());
}

TEST(SkyrimHeldGear, a_weapon_without_a_model_is_not_held)
{
    World w;
    w.outfit({});
    w.mRecords.mWeapons.push_back(weapon(0x610, 1, 99, ""));
    w.mNpc.mInventory = { { 0x610, 1 }, { kSword, 1 } };
    ActorIndex index = w.index();
    HeldGear held;
    ASSERT_TRUE(resolveHeldGear(index, kNpc, 1, kAppearanceLevel, held));
    // The strongest weapon has no model: nothing is drawn (the rule picks by damage first, as task 0214 does).
    EXPECT_TRUE(held.mWeaponModel.empty());
}

// The selection is the one of task 0214 (combat_gear.hpp): the same weapon and shield for the same records.
TEST(SkyrimHeldGear, chooses_what_the_combat_gear_of_task_0214_chooses)
{
    World w;
    w.mLists.push_back(list(kList, LeveledFlags::UseAll, { { 1, kSword, 1 }, { 1, kShieldA, 1 }, { 1, kShieldB, 1 } }));
    w.outfit({ kCuirass, kList });
    w.mNpc.mInventory = { { kSword2, 1 }, { kBow, 1 } };
    ActorIndex index = w.index();

    CombatRecords combatRecords;
    for (const WeaponLook& look : w.mRecords.mWeapons)
    {
        WeaponRecord record;
        record.mCommon.mId = look.mCommon.mId;
        record.mHasData = look.mHasData;
        record.mHasDnam = look.mHasDnam;
        record.mAnimType = look.mAnimType;
        record.mDamage = look.mDamage;
        combatRecords.mWeapons.push_back(record);
    }
    CombatIndex combat;
    combat.add(combatRecords);
    FighterGear gear;
    ASSERT_TRUE(resolveFighterGear(index, combat, kNpc, 1, kAppearanceLevel, {}, gear));
    HeldGear held;
    ASSERT_TRUE(resolveHeldGear(index, kNpc, 1, kAppearanceLevel, held));
    ASSERT_NE(gear.mWeapon, nullptr);
    EXPECT_EQ(held.mWeapon, gear.mWeapon->mCommon.mId);
    EXPECT_EQ(gear.mHasShield, held.mShield != kNullFormId);
    EXPECT_EQ(held.mShield, gear.mShield.mForm);
}
