// Task 0245 (the player's first-person view): the pack half. makeFirstPersonAppearance builds the player's first-person
// arms from a request over hand-built records: the first-person skeleton, the ARMA first-person models (MOD4 male, MOD5
// female) of the race's skin, the start clothes and the equipped armour, and the held weapon and shield. No plugin
// bytes, no game data. A gated test at the end runs it on the installed Skyrim SE masters.
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
    constexpr FormId kTunic = 0x3C9FE;
    constexpr FormId kShoes = 0x3CA00;
    constexpr FormId kCuirass = 0x12E49; // has a first-person model
    constexpr FormId kBareCuirass = 0x12E50; // no first-person model
    constexpr FormId kGauntlets = 0x12E4B;
    constexpr FormId kShield = 0x12EB6;
    constexpr FormId kSword = 0x600;
    constexpr FormId kGreatsword = 0x601;
    constexpr FormId kBow = 0x602;

    constexpr std::uint32_t kBody = 0x4;
    constexpr std::uint32_t kHands = 0x8;
    constexpr std::uint32_t kFeet = 0x80;
    constexpr std::uint32_t kShieldSlot = 0x200;

    ArmatureRecord armature(FormId id, FormId race, std::uint32_t slots, const std::string& thirdPerson,
        const std::string& firstPerson)
    {
        ArmatureRecord a;
        a.mCommon.mId = id;
        a.mRace = race;
        a.mSlotMask = slots;
        a.mMaleModel = thirdPerson;
        a.mFemaleModel = "f_" + thirdPerson;
        if (!firstPerson.empty())
        {
            a.mMaleFirstPerson = firstPerson;
            a.mFemaleFirstPerson = "f_" + firstPerson;
        }
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

    WeaponLook weapon(FormId id, std::uint8_t type, const std::string& model)
    {
        WeaponLook w;
        w.mCommon.mId = id;
        w.mModel = model;
        w.mHasDnam = true;
        w.mHasData = true;
        w.mAnimType = type;
        w.mDamage = 10;
        return w;
    }

    ActorIndex makeIndex(bool withFirstPersonSkin = true)
    {
        ActorRecords r;
        RaceRecord race;
        race.mCommon.mId = kRace;
        race.mSkeleton[0] = "male/skeleton.nif";
        race.mSkeleton[1] = "female/skeleton_female.nif";
        race.mSkin = kSkin;
        race.mHeight = { 1.0f, 0.95f };
        r.mRaces.push_back(race);
        RaceRecord other = race;
        other.mCommon.mId = kOtherRace;
        r.mRaces.push_back(other);

        // the skin: a body (arms) and hands with first-person models, feet with none (nothing of feet is seen)
        r.mArmatures.push_back(armature(0x500, kRace, kBody, "skin_body.nif", withFirstPersonSkin ? "fp_body.nif" : ""));
        r.mArmatures.push_back(
            armature(0x501, kRace, kHands, "skin_hands.nif", withFirstPersonSkin ? "fp_hands.nif" : ""));
        r.mArmatures.push_back(armature(0x502, kRace, kFeet, "skin_feet.nif", ""));
        r.mArmors.push_back(armor(kSkin, kBody, { 0x500, 0x501, 0x502 }));

        // the start clothes: a tunic with first-person sleeves, shoes with none
        r.mArmatures.push_back(armature(0x510, kRace, kBody, "tunic.nif", "fp_tunic.nif"));
        r.mArmatures.push_back(armature(0x511, kRace, kFeet, "shoes.nif", ""));
        r.mArmors.push_back(armor(kTunic, kBody, { 0x510 }));
        r.mArmors.push_back(armor(kShoes, kFeet, { 0x511 }));
        OutfitRecord outfit;
        outfit.mCommon.mId = kPlayerStartOutfit;
        outfit.mItems = { kTunic, kShoes };
        r.mOutfits.push_back(outfit);

        r.mArmatures.push_back(armature(0x520, kRace, kBody, "cuirass.nif", "fp_cuirass.nif"));
        r.mArmatures.push_back(armature(0x521, kRace, kBody, "barecuirass.nif", ""));
        r.mArmatures.push_back(armature(0x522, kRace, kHands, "gauntlets.nif", "fp_gauntlets.nif"));
        r.mArmatures.push_back(armature(0x523, kRace, kShieldSlot, "shield.nif", ""));
        r.mArmors.push_back(armor(kCuirass, kBody, { 0x520 }));
        r.mArmors.push_back(armor(kBareCuirass, kBody, { 0x521 }));
        r.mArmors.push_back(armor(kGauntlets, kHands, { 0x522 }));
        r.mArmors.push_back(armor(kShield, kShieldSlot, { 0x523 }));

        r.mWeapons.push_back(weapon(kSword, 1, "sword.nif"));
        r.mWeapons.push_back(weapon(kGreatsword, 5, "greatsword.nif"));
        r.mWeapons.push_back(weapon(kBow, 7, "bow.nif"));
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
        EXPECT_TRUE(makeFirstPersonAppearance(index, request, out, error, limits)) << error;
        return out;
    }

    TEST(SkyrimFirstPerson, theSkeletonIsTheFirstPersonOneForBothSexes)
    {
        const ActorIndex index = makeIndex();
        for (const bool female : { false, true })
        {
            PlayerBodyRequest request;
            request.mFemale = female;
            const ActorAppearance a = make(index, request);
            EXPECT_EQ(a.skeletonPath, std::string(kFirstPersonSkeleton));
            EXPECT_EQ(a.female, female);
        }
    }

    TEST(SkyrimFirstPerson, theStartClothesReplaceTheSkinThatTheirSlotCoversAndTheHandsRemain)
    {
        const ActorIndex index = makeIndex();
        const ActorAppearance a = make(index, {});
        // the tunic's sleeves cover the skin's arms (body slot); the hands stay; the shoes have no first-person model
        EXPECT_EQ(models(a, ActorPartKind::Skin), std::vector<std::string>{ "fp_hands.nif" });
        EXPECT_EQ(models(a, ActorPartKind::Armor), std::vector<std::string>{ "fp_tunic.nif" });
    }

    TEST(SkyrimFirstPerson, theFemaleTakesTheFemaleFirstPersonModels)
    {
        const ActorIndex index = makeIndex();
        PlayerBodyRequest request;
        request.mFemale = true;
        const ActorAppearance a = make(index, request);
        EXPECT_EQ(models(a, ActorPartKind::Skin), std::vector<std::string>{ "f_fp_hands.nif" });
        EXPECT_EQ(models(a, ActorPartKind::Armor), std::vector<std::string>{ "f_fp_tunic.nif" });
    }

    TEST(SkyrimFirstPerson, noModelOfTheThirdPersonBodyAppears)
    {
        const ActorIndex index = makeIndex();
        const ActorAppearance a = make(index, {});
        for (const auto& part : a.parts)
        {
            EXPECT_EQ(part.modelPath.rfind("fp_", 0), 0u) << part.modelPath;
            EXPECT_NE(part.kind, ActorPartKind::HeadPart);
            EXPECT_NE(part.kind, ActorPartKind::Face);
        }
    }

    TEST(SkyrimFirstPerson, anEquippedPieceWithAFirstPersonModelCoversTheStartClothesAndTheSkinOfItsSlot)
    {
        const ActorIndex index = makeIndex();
        PlayerBodyRequest request;
        request.mWornArmour = { kCuirass, kGauntlets };
        const ActorAppearance a = make(index, request);
        // cuirass over the body slot (tunic and body skin gone), gauntlets over the hands (hand skin gone)
        EXPECT_TRUE(models(a, ActorPartKind::Skin).empty());
        const auto worn = models(a, ActorPartKind::Armor);
        EXPECT_TRUE(has(worn, "fp_cuirass.nif"));
        EXPECT_TRUE(has(worn, "fp_gauntlets.nif"));
        EXPECT_FALSE(has(worn, "fp_tunic.nif"));
        EXPECT_EQ(worn.size(), 2u);
    }

    TEST(SkyrimFirstPerson, anEquippedPieceWithNoFirstPersonModelCoversNothing)
    {
        const ActorIndex index = makeIndex();
        PlayerBodyRequest request;
        request.mWornArmour = { kBareCuirass };
        const ActorAppearance a = make(index, request);
        // nothing is drawn for it; the start tunic and the hands stay as they were
        EXPECT_EQ(models(a, ActorPartKind::Skin), std::vector<std::string>{ "fp_hands.nif" });
        EXPECT_EQ(models(a, ActorPartKind::Armor), std::vector<std::string>{ "fp_tunic.nif" });
    }

    TEST(SkyrimFirstPerson, theOrderOfTheEquippedListDoesNotChangeTheAppearance)
    {
        const ActorIndex index = makeIndex();
        PlayerBodyRequest a;
        a.mWornArmour = { kCuirass, kGauntlets };
        PlayerBodyRequest b;
        b.mWornArmour = { kGauntlets, kCuirass, kCuirass };
        EXPECT_EQ(make(index, a), make(index, b));
    }

    TEST(SkyrimFirstPerson, aRaceWhoseSkinHasNoFirstPersonModelsAndWearsNothingGivesNoArms)
    {
        const ActorIndex index = makeIndex(false);
        PlayerBodyRequest request;
        request.mStartOutfit = kNullFormId;
        ActorAppearance out;
        std::string error;
        EXPECT_FALSE(makeFirstPersonAppearance(index, request, out, error));
        EXPECT_FALSE(error.empty());
    }

    TEST(SkyrimFirstPerson, anUnknownRaceIsRefusedWithAReason)
    {
        const ActorIndex index = makeIndex();
        PlayerBodyRequest request;
        request.mRace = 0x999;
        ActorAppearance out;
        std::string error;
        EXPECT_FALSE(makeFirstPersonAppearance(index, request, out, error));
        EXPECT_NE(error.find("race"), std::string::npos);
    }

    TEST(SkyrimFirstPerson, aSwordAndAShieldAreHeldAsOnTheThirdPersonBody)
    {
        const ActorIndex index = makeIndex();
        PlayerBodyRequest request;
        request.mWeapon = kSword;
        request.mWornArmour = { kShield };
        AppearanceLimits limits;
        limits.mHeldGear = true;
        const ActorAppearance a = make(index, request, limits);
        EXPECT_EQ(a.heldWeaponModel, "sword.nif");
        EXPECT_EQ(a.heldWeaponType, 1);
        EXPECT_EQ(a.heldShieldModel, "shield.nif"); // the shield has no first-person model: the third-person one is held
    }

    TEST(SkyrimFirstPerson, aTwoHandedWeaponGivesNoShieldAndABowIsNotHeld)
    {
        const ActorIndex index = makeIndex();
        AppearanceLimits limits;
        limits.mHeldGear = true;
        PlayerBodyRequest request;
        request.mWeapon = kGreatsword;
        request.mWornArmour = { kShield };
        const ActorAppearance two = make(index, request, limits);
        EXPECT_EQ(two.heldWeaponModel, "greatsword.nif");
        EXPECT_TRUE(two.heldShieldModel.empty());
        request.mWeapon = kBow;
        const ActorAppearance bow = make(index, request, limits);
        EXPECT_TRUE(bow.heldWeaponModel.empty());
    }

    TEST(SkyrimFirstPerson, theSameRequestGivesTheSameAppearance)
    {
        const ActorIndex index = makeIndex();
        PlayerBodyRequest request;
        request.mWornArmour = { kCuirass };
        request.mWeapon = kSword;
        AppearanceLimits limits;
        limits.mHeldGear = true;
        EXPECT_EQ(make(index, request, limits), make(index, request, limits));
    }

    TEST(SkyrimFirstPersonRealData, bothSexesHaveFirstPersonArmsOnTheInstalledData)
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
            ASSERT_TRUE(makeFirstPersonAppearance(index, request, a, error)) << error;
            EXPECT_EQ(a.skeletonPath, std::string(kFirstPersonSkeleton));
            EXPECT_GE(a.parts.size(), 1u);
            std::cout << "SKYRIMSE-FIRSTPERSON " << (female ? "female" : "male") << " parts " << a.parts.size();
            for (const auto& part : a.parts)
                std::cout << " [" << (part.kind == ActorPartKind::Skin ? "skin " : "worn ") << part.modelPath << "]";
            std::cout << "\n";
        }
    }
}
