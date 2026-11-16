// Task 0222 (the player has a Skyrim body): the pack half. makePlayerAppearance builds the player's appearance from a
// request (race, sex, equipped armour and weapon) over hand-built records; no plugin bytes, no game data. A gated test
// at the end runs it on the installed Skyrim SE masters.
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/appearance.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/player_body.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using openeden::sdk::game::ActorAppearance;
    using openeden::sdk::game::ActorPartKind;

    constexpr FormId kRace = 0x13746;
    constexpr FormId kOtherRace = 0x11;
    constexpr FormId kSkin = 0x200;
    constexpr FormId kOutfit = kPlayerStartOutfit;
    constexpr FormId kTunic = 0x3C9FE;
    constexpr FormId kShoes = 0x3CA00;
    constexpr FormId kCuirass = 0x12E49;
    constexpr FormId kBoots = 0x12E4B;
    constexpr FormId kHelm = 0x12E4D;
    constexpr FormId kShield = 0x12EB6;
    constexpr FormId kShieldOtherRace = 0x12EB7;
    constexpr FormId kSkinBody = 0x500;
    constexpr FormId kSkinHands = 0x501;
    constexpr FormId kSkinFeet = 0x502;
    constexpr FormId kSword = 0x600;
    constexpr FormId kGreatsword = 0x601;
    constexpr FormId kBow = 0x602;
    constexpr FormId kNoModel = 0x603;
    constexpr FormId kHeadMale = 0x700;
    constexpr FormId kEyesMale = 0x701;
    constexpr FormId kHeadFemale = 0x710;
    constexpr FormId kHairFemale = 0x711;

    constexpr std::uint32_t kBody = 0x4;
    constexpr std::uint32_t kHands = 0x8;
    constexpr std::uint32_t kFeet = 0x80;
    constexpr std::uint32_t kHead = 0x1;
    constexpr std::uint32_t kShieldSlot = 0x200;

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

    ArmorExtra armor(FormId id, std::uint32_t slots, std::vector<FormId> armatures)
    {
        ArmorExtra a;
        a.mCommon.mId = id;
        a.mSlotMask = slots;
        a.mArmatures = std::move(armatures);
        return a;
    }

    WeaponLook weapon(FormId id, std::uint8_t type, std::string model)
    {
        WeaponLook w;
        w.mCommon.mId = id;
        w.mModel = std::move(model);
        w.mHasDnam = true;
        w.mHasData = true;
        w.mAnimType = type;
        w.mDamage = 10;
        return w;
    }

    HeadPartRecord headPart(FormId id, std::uint32_t type, std::string model)
    {
        HeadPartRecord h;
        h.mCommon.mId = id;
        h.mType = type;
        h.mModel = std::move(model);
        return h;
    }

    ActorIndex makeIndex()
    {
        ActorRecords r;
        RaceRecord race;
        race.mCommon.mId = kRace;
        race.mSkeleton[0] = "male\\skeleton.nif";
        race.mSkeleton[1] = "female\\skeleton_female.nif";
        race.mSkin = kSkin;
        race.mHeight = { 1.0f, 0.95f };
        race.mHeadParts = { { Sex::Male, 0, kHeadMale }, { Sex::Male, 1, kEyesMale }, { Sex::Female, 0, kHeadFemale },
            { Sex::Female, 3, kHairFemale } };
        r.mRaces.push_back(race);
        RaceRecord other = race;
        other.mCommon.mId = kOtherRace;
        r.mRaces.push_back(other);

        r.mArmatures.push_back(armature(kSkinBody, kRace, kBody, "skin_body.nif"));
        r.mArmatures.push_back(armature(kSkinHands, kRace, kHands, "skin_hands.nif"));
        r.mArmatures.push_back(armature(kSkinFeet, kRace, kFeet, "skin_feet.nif"));
        r.mArmors.push_back(armor(kSkin, kBody, { kSkinBody, kSkinHands, kSkinFeet }));

        r.mArmatures.push_back(armature(0x510, kRace, kBody, "tunic.nif"));
        r.mArmatures.push_back(armature(0x511, kRace, kFeet, "shoes.nif"));
        r.mArmors.push_back(armor(kTunic, kBody, { 0x510 }));
        r.mArmors.push_back(armor(kShoes, kFeet, { 0x511 }));
        OutfitRecord outfit;
        outfit.mCommon.mId = kOutfit;
        outfit.mItems = { kTunic, kShoes };
        r.mOutfits.push_back(outfit);

        r.mArmatures.push_back(armature(0x520, kRace, kBody, "cuirass.nif"));
        r.mArmatures.push_back(armature(0x521, kRace, kFeet, "boots.nif"));
        r.mArmatures.push_back(armature(0x522, kRace, kHead, "helm.nif"));
        r.mArmatures.push_back(armature(0x523, kRace, kShieldSlot, "shield.nif"));
        r.mArmatures.push_back(armature(0x524, kOtherRace, kShieldSlot, "othershield.nif"));
        r.mArmors.push_back(armor(kCuirass, kBody, { 0x520 }));
        r.mArmors.push_back(armor(kBoots, kFeet, { 0x521 }));
        r.mArmors.push_back(armor(kHelm, kHead, { 0x522 }));
        r.mArmors.push_back(armor(kShield, kShieldSlot, { 0x523 }));
        r.mArmors.push_back(armor(kShieldOtherRace, kShieldSlot, { 0x524 }));

        r.mWeapons.push_back(weapon(kSword, 1, "sword.nif"));
        r.mWeapons.push_back(weapon(kGreatsword, 5, "greatsword.nif"));
        r.mWeapons.push_back(weapon(kBow, 7, "bow.nif"));
        r.mWeapons.push_back(weapon(kNoModel, 1, ""));

        r.mHeadParts.push_back(headPart(kHeadMale, HeadPartType::Face, "head_m.nif"));
        r.mHeadParts.push_back(headPart(kEyesMale, HeadPartType::Eyes, "eyes_m.nif"));
        r.mHeadParts.push_back(headPart(kHeadFemale, HeadPartType::Face, "head_f.nif"));
        r.mHeadParts.push_back(headPart(kHairFemale, HeadPartType::Hair, "hair_f.nif"));
        ActorIndex index;
        index.add(r);
        return index;
    }

    std::vector<std::string> models(const ActorAppearance& a, ActorPartKind kind)
    {
        std::vector<std::string> out;
        for (const auto& part : a.parts)
            if (part.kind == kind)
                out.push_back(part.modelPath);
        return out;
    }

    bool has(const std::vector<std::string>& list, const std::string& value)
    {
        return std::find(list.begin(), list.end(), value) != list.end();
    }

    ActorAppearance make(const ActorIndex& index, const PlayerBodyRequest& request, const AppearanceLimits& limits = {})
    {
        ActorAppearance out;
        std::string error;
        EXPECT_TRUE(makePlayerAppearance(index, request, out, error, limits)) << error;
        return out;
    }

    TEST(SkyrimPlayerBody, theMaleStartsInTheRaceSkeletonSkinAndStartClothes)
    {
        const ActorIndex index = makeIndex();
        const ActorAppearance a = make(index, {});
        EXPECT_EQ(a.skeletonPath, "male\\skeleton.nif");
        EXPECT_FALSE(a.female);
        // the tunic covers the body and the shoes the feet: only the hands of the skin remain
        EXPECT_EQ(models(a, ActorPartKind::Skin), std::vector<std::string>{ "skin_hands.nif" });
        const auto worn = models(a, ActorPartKind::Armor);
        EXPECT_TRUE(has(worn, "tunic.nif"));
        EXPECT_TRUE(has(worn, "shoes.nif"));
        EXPECT_EQ(worn.size(), 2u);
    }

    TEST(SkyrimPlayerBody, theFemaleTakesTheFemaleSkeletonModelsAndHeadParts)
    {
        const ActorIndex index = makeIndex();
        PlayerBodyRequest request;
        request.mFemale = true;
        const ActorAppearance a = make(index, request);
        EXPECT_EQ(a.skeletonPath, "female\\skeleton_female.nif");
        EXPECT_TRUE(a.female);
        EXPECT_TRUE(has(models(a, ActorPartKind::Armor), "f_tunic.nif"));
        const auto heads = models(a, ActorPartKind::HeadPart);
        EXPECT_EQ(heads, (std::vector<std::string>{ "head_f.nif", "hair_f.nif" }));
    }

    TEST(SkyrimPlayerBody, theMaleHeadPartsAreTheRacesDefaultsForTheSexInOrder)
    {
        const ActorIndex index = makeIndex();
        const ActorAppearance a = make(index, {});
        EXPECT_EQ(models(a, ActorPartKind::HeadPart), (std::vector<std::string>{ "head_m.nif", "eyes_m.nif" }));
        for (const auto& part : a.parts)
            if (part.kind == ActorPartKind::HeadPart)
                EXPECT_NE(part.headPartType, 0u);
    }

    TEST(SkyrimPlayerBody, theHeightScaleIsTheRacesForTheSex)
    {
        const ActorIndex index = makeIndex();
        EXPECT_FLOAT_EQ(make(index, {}).heightScale, 1.0f);
        PlayerBodyRequest request;
        request.mFemale = true;
        EXPECT_FLOAT_EQ(make(index, request).heightScale, 0.95f);
    }

    TEST(SkyrimPlayerBody, anEquippedCuirassReplacesTheTunicAndKeepsTheShoes)
    {
        const ActorIndex index = makeIndex();
        PlayerBodyRequest request;
        request.mWornArmour = { kCuirass };
        const ActorAppearance a = make(index, request);
        const auto worn = models(a, ActorPartKind::Armor);
        EXPECT_TRUE(has(worn, "cuirass.nif"));
        EXPECT_FALSE(has(worn, "tunic.nif"));
        EXPECT_TRUE(has(worn, "shoes.nif"));
        EXPECT_EQ(models(a, ActorPartKind::Skin), std::vector<std::string>{ "skin_hands.nif" });
    }

    TEST(SkyrimPlayerBody, equippedBootsReplaceTheShoesAndAHelmetJoinsTheOutfit)
    {
        const ActorIndex index = makeIndex();
        PlayerBodyRequest request;
        request.mWornArmour = { kBoots, kHelm };
        const ActorAppearance a = make(index, request);
        const auto worn = models(a, ActorPartKind::Armor);
        EXPECT_TRUE(has(worn, "boots.nif"));
        EXPECT_TRUE(has(worn, "helm.nif"));
        EXPECT_TRUE(has(worn, "tunic.nif"));
        EXPECT_FALSE(has(worn, "shoes.nif"));
    }

    TEST(SkyrimPlayerBody, theOrderOfTheEquippedListDoesNotChangeTheResult)
    {
        const ActorIndex index = makeIndex();
        PlayerBodyRequest a, b;
        a.mWornArmour = { kCuirass, kBoots, kHelm };
        b.mWornArmour = { kHelm, kCuirass, kBoots };
        EXPECT_EQ(make(index, a), make(index, b));
    }

    TEST(SkyrimPlayerBody, theSameRequestGivesTheSameAppearance)
    {
        const ActorIndex index = makeIndex();
        PlayerBodyRequest request;
        request.mWornArmour = { kCuirass };
        request.mWeapon = kSword;
        EXPECT_EQ(make(index, request), make(index, request));
    }

    TEST(SkyrimPlayerBody, anArmourThatHasNoAddonForTheRaceIsLeftOut)
    {
        ActorRecords r;
        r.mArmors.push_back(armor(0x9000, kBody, { 0x524 })); // only an addon of the other race
        ActorIndex index = makeIndex();
        index.add(r);
        PlayerBodyRequest request;
        request.mWornArmour = { 0x9000 };
        const ActorAppearance a = make(index, request);
        EXPECT_TRUE(has(models(a, ActorPartKind::Armor), "tunic.nif")); // the outfit's body piece was not displaced
        EXPECT_FALSE(has(models(a, ActorPartKind::Armor), "othershield.nif"));
    }

    TEST(SkyrimPlayerBody, anEquippedSwordIsHeldAndNamedByTypeButNotAPart)
    {
        AppearanceLimits limits;
        limits.mHeldGear = true;
        const ActorIndex index = makeIndex();
        PlayerBodyRequest request;
        request.mWeapon = kSword;
        const ActorAppearance a = make(index, request, limits);
        EXPECT_EQ(a.heldWeaponModel, "sword.nif");
        EXPECT_EQ(a.heldWeaponType, 1);
        EXPECT_TRUE(a.heldShieldModel.empty());
    }

    TEST(SkyrimPlayerBody, theHeldGearIsNamedOnlyWhenHeldGearIsOn)
    {
        const ActorIndex index = makeIndex();
        PlayerBodyRequest request;
        request.mWeapon = kSword;
        request.mWornArmour = { kShield };
        const ActorAppearance a = make(index, request); // limits: held gear off
        EXPECT_TRUE(a.heldWeaponModel.empty());
        EXPECT_TRUE(a.heldShieldModel.empty());
        EXPECT_TRUE(has(models(a, ActorPartKind::Armor), "shield.nif")); // worn as an ordinary piece, as an NPC's
    }

    TEST(SkyrimPlayerBody, aBowAndAWeaponWithNoModelAreNotHeld)
    {
        AppearanceLimits limits;
        limits.mHeldGear = true;
        const ActorIndex index = makeIndex();
        for (const FormId id : { kBow, kNoModel, FormId(0xDEAD) })
        {
            PlayerBodyRequest request;
            request.mWeapon = id;
            const ActorAppearance a = make(index, request, limits);
            EXPECT_TRUE(a.heldWeaponModel.empty()) << std::hex << id;
            EXPECT_EQ(a.heldWeaponType, 0);
        }
    }

    TEST(SkyrimPlayerBody, anEquippedShieldIsHeldAndLeavesThePartList)
    {
        AppearanceLimits limits;
        limits.mHeldGear = true;
        const ActorIndex index = makeIndex();
        PlayerBodyRequest request;
        request.mWornArmour = { kShield, kCuirass };
        request.mWeapon = kSword;
        const ActorAppearance a = make(index, request, limits);
        EXPECT_EQ(a.heldShieldModel, "shield.nif");
        EXPECT_FALSE(has(models(a, ActorPartKind::Armor), "shield.nif"));
        EXPECT_TRUE(has(models(a, ActorPartKind::Armor), "cuirass.nif"));
    }

    TEST(SkyrimPlayerBody, aTwoHandedWeaponLeavesNoShield)
    {
        AppearanceLimits limits;
        limits.mHeldGear = true;
        const ActorIndex index = makeIndex();
        PlayerBodyRequest request;
        request.mWornArmour = { kShield };
        request.mWeapon = kGreatsword;
        const ActorAppearance a = make(index, request, limits);
        EXPECT_EQ(a.heldWeaponModel, "greatsword.nif");
        EXPECT_EQ(a.heldWeaponType, 5);
        EXPECT_TRUE(a.heldShieldModel.empty());
        EXPECT_FALSE(has(models(a, ActorPartKind::Armor), "shield.nif"));
    }

    TEST(SkyrimPlayerBody, aShieldWithNoAddonForTheRaceIsNotHeld)
    {
        AppearanceLimits limits;
        limits.mHeldGear = true;
        const ActorIndex index = makeIndex();
        PlayerBodyRequest request;
        request.mWornArmour = { kShieldOtherRace };
        const ActorAppearance a = make(index, request, limits);
        EXPECT_TRUE(a.heldShieldModel.empty());
    }

    TEST(SkyrimPlayerBody, anUnknownRaceIsRefusedWithAReason)
    {
        const ActorIndex index = makeIndex();
        PlayerBodyRequest request;
        request.mRace = 0x1234;
        ActorAppearance out;
        std::string error;
        EXPECT_FALSE(makePlayerAppearance(index, request, out, error));
        EXPECT_FALSE(error.empty());
        EXPECT_TRUE(out.parts.empty());
    }

    TEST(SkyrimPlayerBody, anUnknownStartOutfitJustMeansNoStartClothes)
    {
        const ActorIndex index = makeIndex();
        PlayerBodyRequest request;
        request.mStartOutfit = 0x7777;
        const ActorAppearance a = make(index, request);
        EXPECT_TRUE(models(a, ActorPartKind::Armor).empty());
        EXPECT_EQ(models(a, ActorPartKind::Skin).size(), 3u); // the whole skin shows
    }

    TEST(SkyrimPlayerBody, thePartCountIsCapped)
    {
        const ActorIndex index = makeIndex();
        AppearanceLimits limits;
        limits.mMaxParts = 3;
        const ActorAppearance a = make(index, {}, limits);
        EXPECT_LE(a.parts.size(), 3u);
    }

    // ---- the installed data (gated) ---------------------------------------------------------------------------------

    TEST(SkyrimPlayerBodyRealData, bothSexesHaveASkeletonSkinStartClothesAndAHead)
    {
        namespace fs = std::filesystem;
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || *env == '\0' || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data(env);
        const std::vector<std::string> names{ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm",
            "Dragonborn.esm" };
        for (const std::string& n : names)
            if (!fs::exists(data / n))
                GTEST_SKIP() << "the five official masters are not all in the Data folder";
        LoadOrder order;
        std::string err;
        for (const std::string& n : names)
            ASSERT_TRUE(order.add(n, false, err)) << err;
        ActorIndex index;
        for (const std::string& n : names)
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / n, error);
            ASSERT_NE(plugin, nullptr) << error.describe();
            ActorRecords actors;
            EXPECT_TRUE(parseActorRecords(*plugin, order, ActorLimits(), actors));
            index.add(actors);
        }
        for (const bool female : { false, true })
        {
            PlayerBodyRequest request;
            request.mFemale = female;
            ActorAppearance a;
            std::string error;
            ASSERT_TRUE(makePlayerAppearance(index, request, a, error)) << error;
            EXPECT_FALSE(a.skeletonPath.empty());
            EXPECT_EQ(a.female, female);
            EXPECT_GE(models(a, ActorPartKind::Skin).size(), 1u); // the hands, at least
            EXPECT_EQ(models(a, ActorPartKind::Armor).size(), 2u); // the prisoner tunic and shoes
            const auto heads = models(a, ActorPartKind::HeadPart);
            EXPECT_GE(heads.size(), 3u) << "head, eyes and more";
            std::cout << "SKYRIMSE-PLAYERBODY " << (female ? "female" : "male") << " parts " << a.parts.size()
                      << " heightScale " << a.heightScale << "\n";
        }
    }
}
