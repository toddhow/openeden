// Task 0225 (a first character creation for a Skyrim session): the pure model of the choices. Hand-built records, no
// plugin bytes; a gated test at the end runs the rules on the installed Skyrim SE masters.
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/character.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/player_body.hpp>
#include <game_skyrimse/progression.hpp>
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
    using namespace game_skyrimse::character;
    using openeden::sdk::game::ActorAppearance;
    using openeden::sdk::game::ActorPartKind;

    constexpr FormId kNord = 0x13746;
    constexpr FormId kArgonian = 0x13740;
    constexpr FormId kOrc = 0x13747; // not playable in the test data
    constexpr FormId kElder = 0x13750; // playable flag but no skeleton
    constexpr FormId kNordList = 0x900;
    constexpr FormId kArgonianList = 0x901;

    constexpr FormId kHairM1 = 0x1001;
    constexpr FormId kHairM2 = 0x1002;
    constexpr FormId kHairF1 = 0x1003;
    constexpr FormId kHairExtra = 0x1004; // an extra part: never an option
    constexpr FormId kHairNoPlay = 0x1005;
    constexpr FormId kHairArg = 0x1006;
    constexpr FormId kHairNoList = 0x1007; // no valid-race list at all
    constexpr FormId kEyesM1 = 0x1010;
    constexpr FormId kEyesM2 = 0x1011;
    constexpr FormId kEyesF1 = 0x1012;
    constexpr FormId kEyesArg = 0x1013;
    constexpr FormId kBeardM1 = 0x1020;
    constexpr FormId kHairlineM1 = 0x1030; // the extra part of kHairM1
    constexpr FormId kFace = 0x1040;
    constexpr FormId kHairOutsideList = 0x1050; // the Nord's default hair that its list does not name

    HeadPartRecord part(FormId id, std::uint32_t type, std::uint8_t flags, FormId races, std::string editor)
    {
        HeadPartRecord h;
        h.mCommon.mId = id;
        h.mCommon.mEditorId = std::move(editor);
        h.mType = type;
        h.mFlags = flags;
        h.mValidRaces = races;
        h.mModel = "parts\\" + h.mCommon.mEditorId + ".nif";
        return h;
    }

    RaceRecord race(FormId id, const char* editor, const char* name, bool playable, bool skeleton = true)
    {
        RaceRecord r;
        r.mCommon.mId = id;
        r.mCommon.mEditorId = editor;
        r.mName.mPresent = true;
        r.mName.mText = name;
        r.mFlags = playable ? 1u : 0u;
        if (skeleton)
        {
            r.mSkeleton[0] = "male\\skeleton.nif";
            r.mSkeleton[1] = "female\\skeleton_female.nif";
        }
        return r;
    }

    ActorIndex makeIndex()
    {
        ActorRecords r;
        RaceRecord nord = race(kNord, "NordRace", "Nord", true);
        nord.mDescription.mPresent = true;
        nord.mDescription.mText = "Hardy and tough.";
        nord.mSkillBoosts = { { 7, 10 }, { 6, 5 } };
        nord.mSpells = { 0xA0, 0xA1 };
        nord.mHeadParts = { { Sex::Male, 0, kHairM1 }, { Sex::Male, 1, kEyesM1 }, { Sex::Male, 2, kFace },
            { Sex::Female, 0, kHairF1 }, { Sex::Female, 1, kEyesF1 }, { Sex::Female, 2, kFace } };
        r.mRaces.push_back(nord);
        RaceRecord arg = race(kArgonian, "ArgonianRace", "Argonian", true);
        arg.mSkillBoosts = { { 14, 10 } };
        arg.mHeadParts = { { Sex::Male, 0, kHairArg }, { Sex::Male, 1, kEyesArg }, { Sex::Female, 0, kHairArg },
            { Sex::Female, 1, kEyesArg } };
        r.mRaces.push_back(arg);
        r.mRaces.push_back(race(kOrc, "OrcRace", "Orc", false));
        r.mRaces.push_back(race(kElder, "ElderRace", "Elder", true, false));

        ActorFormList nordList;
        nordList.mCommon.mId = kNordList;
        nordList.mForms = { kNord, 0x77 };
        ActorFormList argList;
        argList.mCommon.mId = kArgonianList;
        argList.mForms = { kArgonian };
        r.mFormLists = { nordList, argList };

        // flags: 1 playable, 2 male, 4 female, 8 extra part
        r.mHeadParts.push_back(part(kHairM1, 3, 3, kNordList, "HairMaleNord01"));
        r.mHeadParts.back().mExtraParts = { kHairlineM1 };
        r.mHeadParts.push_back(part(kHairM2, 3, 3, kNordList, "HairMaleNord02"));
        r.mHeadParts.push_back(part(kHairF1, 3, 5, kNordList, "HairFemaleNord01"));
        r.mHeadParts.push_back(part(kHairExtra, 3, 3 | 8, kNordList, "HairMaleNordExtra"));
        r.mHeadParts.push_back(part(kHairNoPlay, 3, 2, kNordList, "HairMaleNordCut"));
        r.mHeadParts.push_back(part(kHairArg, 3, 7, kArgonianList, "HairArgonian01"));
        r.mHeadParts.push_back(part(kHairNoList, 3, 3, kNullFormId, "HairNoRaces"));
        r.mHeadParts.push_back(part(kEyesM1, 2, 3, kNordList, "EyesMaleNord01"));
        r.mHeadParts.push_back(part(kEyesM2, 2, 3, kNordList, "EyesMaleNord02"));
        r.mHeadParts.push_back(part(kEyesF1, 2, 5, kNordList, "EyesFemaleNord01"));
        r.mHeadParts.push_back(part(kEyesArg, 2, 7, kArgonianList, "EyesArgonian01"));
        r.mHeadParts.push_back(part(kBeardM1, 4, 3, kNordList, "BeardNord01"));
        r.mHeadParts.push_back(part(kHairlineM1, 3, 3 | 8, kNordList, "HairLineNord01"));
        r.mHeadParts.push_back(part(kFace, 1, 7, kNordList, "FaceNord"));
        ActorIndex index;
        index.add(r);
        return index;
    }

    TextFn plain()
    {
        return [](const LString& text, std::uint32_t, StringTableKind) { return text.mText; };
    }

    // ---- names ---------------------------------------------------------------------------------------------------

    TEST(SkyrimCharacterParts, theThreeKindsHaveTheirHeadPartTypesAndNames)
    {
        EXPECT_EQ(headPartType(Part::Hair), 3u);
        EXPECT_EQ(headPartType(Part::Beard), 4u);
        EXPECT_EQ(headPartType(Part::Eyes), 2u);
        EXPECT_EQ(partName(Part::Hair), "hair");
        EXPECT_EQ(partName(Part::Beard), "beard");
        EXPECT_EQ(partName(Part::Eyes), "eyes");
        Part p = Part::Hair;
        EXPECT_TRUE(partFromName("eyes", p));
        EXPECT_EQ(p, Part::Eyes);
        EXPECT_TRUE(partFromName("beard", p));
        EXPECT_EQ(p, Part::Beard);
        EXPECT_FALSE(partFromName("tail", p));
        EXPECT_FALSE(partFromName("", p));
    }

    // ---- races ---------------------------------------------------------------------------------------------------

    TEST(SkyrimCharacterRaces, onlyPlayableRacesWithASkeletonAreOfferedSortedByName)
    {
        const ActorIndex index = makeIndex();
        const std::vector<RaceOption> races = playableRaces(index, plain());
        ASSERT_EQ(races.size(), 2u);
        EXPECT_EQ(races[0].mName, "Argonian");
        EXPECT_EQ(races[1].mName, "Nord");
        EXPECT_EQ(races[1].mId, kNord);
        EXPECT_EQ(races[1].mEditorId, "NordRace");
        EXPECT_EQ(races[1].mDescription, "Hardy and tough.");
        ASSERT_EQ(races[1].mBoosts.size(), 2u);
        EXPECT_EQ(races[1].mBoosts[0].mActorValue, 7u);
        EXPECT_EQ(races[1].mBoosts[0].mBoost, 10);
        EXPECT_EQ(races[1].mAbilities, (std::vector<FormId>{ 0xA0, 0xA1 }));
    }

    TEST(SkyrimCharacterRaces, theTextFunctionResolvesLocalisedNamesWithTheRecordsPlugin)
    {
        ActorRecords r;
        RaceRecord nord = race(kNord, "NordRace", "", true);
        nord.mName.mLocalized = true;
        nord.mName.mId = 77;
        nord.mDescription.mPresent = true;
        nord.mDescription.mLocalized = true;
        nord.mDescription.mId = 78;
        nord.mPlugin = 3;
        r.mRaces.push_back(nord);
        ActorIndex index;
        index.add(r);
        std::uint32_t seenPlugin = 99;
        const TextFn text = [&](const LString& s, std::uint32_t plugin, StringTableKind kind) -> std::string {
            seenPlugin = plugin;
            if (!s.mLocalized)
                return s.mText;
            return (kind == StringTableKind::Strings ? "name" : "desc") + std::to_string(s.mId);
        };
        const std::vector<RaceOption> races = playableRaces(index, text);
        ASSERT_EQ(races.size(), 1u);
        EXPECT_EQ(seenPlugin, 3u);
        EXPECT_EQ(races[0].mName, "name77");
        EXPECT_FALSE(races[0].mDescription.empty());
    }

    TEST(SkyrimCharacterRaces, aRaceWithNoNameShowsItsEditorId)
    {
        ActorRecords r;
        RaceRecord nord = race(kNord, "NordRace", "", true);
        nord.mName.mPresent = false;
        r.mRaces.push_back(nord);
        ActorIndex index;
        index.add(r);
        const std::vector<RaceOption> races = playableRaces(index, plain());
        ASSERT_EQ(races.size(), 1u);
        EXPECT_EQ(races[0].mName, "NordRace");
    }

    TEST(SkyrimCharacterRaces, anEmptyIndexOffersNothing)
    {
        const ActorIndex index;
        EXPECT_TRUE(playableRaces(index, plain()).empty());
    }

    TEST(SkyrimCharacterRaces, theListIsCappedAndDeterministic)
    {
        ActorRecords r;
        for (FormId id = 0x2000; id < 0x2000 + kMaxRaces + 10; ++id)
        {
            RaceRecord x = race(id, "R", "Same", true);
            r.mRaces.push_back(x);
        }
        ActorIndex index;
        index.add(r);
        const auto a = playableRaces(index, plain());
        const auto b = playableRaces(index, plain());
        ASSERT_EQ(a.size(), kMaxRaces);
        for (std::size_t i = 0; i < a.size(); ++i)
            EXPECT_EQ(a[i].mId, b[i].mId);
        EXPECT_LT(a.front().mId, a.back().mId) << "equal names are ordered by form id";
    }

    TEST(SkyrimCharacterRaces, boostsAreDescribedWithTheSkillNames)
    {
        EXPECT_EQ(describeBoosts({ { 7, 10 }, { 6, 5 } }), "Two-Handed +10, One-Handed +5");
        EXPECT_EQ(describeBoosts({}), "");
        EXPECT_EQ(describeBoosts({ { 99, 5 } }), "") << "an actor value that is not a skill is left out";
    }

    // ---- head parts ----------------------------------------------------------------------------------------------

    TEST(SkyrimCharacterOptions, aMaleNordsHairsAreTheValidPlayableMaleOnesAfterNone)
    {
        const ActorIndex index = makeIndex();
        const auto hair = partOptions(index, plain(), kNord, false, Part::Hair);
        ASSERT_EQ(hair.size(), 3u);
        EXPECT_EQ(hair[0].mId, kNullFormId);
        EXPECT_EQ(hair[0].mName, "None");
        EXPECT_EQ(hair[1].mId, kHairM1);
        EXPECT_EQ(hair[2].mId, kHairM2);
    }

    TEST(SkyrimCharacterOptions, anExtraPartAnUnplayablePartAnotherRacesPartAndOneWithNoRaceListAreNeverOffered)
    {
        const ActorIndex index = makeIndex();
        const auto hair = partOptions(index, plain(), kNord, false, Part::Hair);
        for (const PartOption& o : hair)
        {
            EXPECT_NE(o.mId, kHairExtra);
            EXPECT_NE(o.mId, kHairlineM1);
            EXPECT_NE(o.mId, kHairNoPlay);
            EXPECT_NE(o.mId, kHairArg);
            EXPECT_NE(o.mId, kHairNoList);
            EXPECT_NE(o.mId, kHairF1);
        }
    }

    TEST(SkyrimCharacterOptions, aFemaleGetsHerOwnPartsAndTheArgonianHisRaceOnes)
    {
        const ActorIndex index = makeIndex();
        const auto hair = partOptions(index, plain(), kNord, true, Part::Hair);
        ASSERT_EQ(hair.size(), 2u);
        EXPECT_EQ(hair[1].mId, kHairF1);
        const auto eyes = partOptions(index, plain(), kNord, true, Part::Eyes);
        ASSERT_EQ(eyes.size(), 1u) << "eyes have no None";
        EXPECT_EQ(eyes[0].mId, kEyesF1);
        const auto arg = partOptions(index, plain(), kArgonian, false, Part::Hair);
        ASSERT_EQ(arg.size(), 2u);
        EXPECT_EQ(arg[1].mId, kHairArg);
    }

    TEST(SkyrimCharacterOptions, eyesAreSortedByEditorIdAndFacialHairCanBeNone)
    {
        const ActorIndex index = makeIndex();
        const auto eyes = partOptions(index, plain(), kNord, false, Part::Eyes);
        ASSERT_EQ(eyes.size(), 2u);
        EXPECT_EQ(eyes[0].mId, kEyesM1);
        EXPECT_EQ(eyes[1].mId, kEyesM2);
        const auto beard = partOptions(index, plain(), kNord, false, Part::Beard);
        ASSERT_EQ(beard.size(), 2u);
        EXPECT_EQ(beard[0].mId, kNullFormId);
        EXPECT_EQ(beard[1].mId, kBeardM1);
        const auto femaleBeard = partOptions(index, plain(), kNord, true, Part::Beard);
        ASSERT_EQ(femaleBeard.size(), 1u) << "only None";
    }

    TEST(SkyrimCharacterOptions, theRacesOwnDefaultIsAlwaysAnOptionEvenWhenItsListDoesNotNameIt)
    {
        ActorRecords r;
        RaceRecord nord = race(kNord, "NordRace", "Nord", true);
        nord.mHeadParts = { { Sex::Male, 0, kHairOutsideList } };
        r.mRaces.push_back(nord);
        ActorFormList list;
        list.mCommon.mId = kNordList;
        list.mForms = { kNord };
        r.mFormLists.push_back(list);
        r.mHeadParts.push_back(part(kHairM1, 3, 3, kNordList, "HairMaleNord01"));
        r.mHeadParts.push_back(part(kHairOutsideList, 3, 3, kNullFormId, "HairDefaultOutside"));
        ActorIndex index;
        index.add(r);
        const auto hair = partOptions(index, plain(), kNord, false, Part::Hair);
        const bool found
            = std::any_of(hair.begin(), hair.end(), [](const PartOption& o) { return o.mId == kHairOutsideList; });
        EXPECT_TRUE(found);
    }

    TEST(SkyrimCharacterOptions, anUnknownRaceOffersNothing)
    {
        const ActorIndex index = makeIndex();
        EXPECT_TRUE(partOptions(index, plain(), 0xDEAD, false, Part::Hair).empty());
    }

    TEST(SkyrimCharacterOptions, theNameIsTheRecordsNameElseItsEditorId)
    {
        ActorRecords r;
        RaceRecord nord = race(kNord, "NordRace", "Nord", true);
        r.mRaces.push_back(nord);
        ActorFormList list;
        list.mCommon.mId = kNordList;
        list.mForms = { kNord };
        r.mFormLists.push_back(list);
        HeadPartRecord named = part(kHairM1, 3, 3, kNordList, "HairA");
        named.mName.mPresent = true;
        named.mName.mText = "Braided";
        r.mHeadParts.push_back(named);
        r.mHeadParts.push_back(part(kHairM2, 3, 3, kNordList, "HairB"));
        ActorIndex index;
        index.add(r);
        const auto hair = partOptions(index, plain(), kNord, false, Part::Hair);
        ASSERT_EQ(hair.size(), 3u);
        EXPECT_EQ(hair[1].mName, "Braided");
        EXPECT_EQ(hair[2].mName, "HairB");
    }

    // ---- defaults and validity -----------------------------------------------------------------------------------

    TEST(SkyrimCharacterChoice, theDefaultsAreTheRacesOwnPartsForTheSex)
    {
        const ActorIndex index = makeIndex();
        EXPECT_EQ(defaultPart(index, kNord, false, Part::Hair), kHairM1);
        EXPECT_EQ(defaultPart(index, kNord, true, Part::Hair), kHairF1);
        EXPECT_EQ(defaultPart(index, kNord, false, Part::Eyes), kEyesM1);
        EXPECT_EQ(defaultPart(index, kNord, false, Part::Beard), kNullFormId);
        const Choice male = defaultChoice(index, kNord, false);
        EXPECT_EQ(male.mRace, kNord);
        EXPECT_FALSE(male.mFemale);
        EXPECT_EQ(male.mParts[static_cast<std::size_t>(Part::Hair)], kHairM1);
        EXPECT_EQ(male.mParts[static_cast<std::size_t>(Part::Eyes)], kEyesM1);
        EXPECT_EQ(male.mParts[static_cast<std::size_t>(Part::Beard)], kNullFormId);
        EXPECT_TRUE(male.mName.empty());
        const Choice female = defaultChoice(index, kNord, true);
        EXPECT_TRUE(female.mFemale);
        EXPECT_EQ(female.mParts[static_cast<std::size_t>(Part::Hair)], kHairF1);
    }

    TEST(SkyrimCharacterChoice, aChoiceIsValidWhenTheRaceIsPlayableAndEveryPartIsAnOption)
    {
        const ActorIndex index = makeIndex();
        std::string why;
        Choice c = defaultChoice(index, kNord, false);
        EXPECT_TRUE(validChoice(index, c, why)) << why;
        c.mParts[static_cast<std::size_t>(Part::Hair)] = kHairM2;
        EXPECT_TRUE(validChoice(index, c, why)) << why;
        c.mParts[static_cast<std::size_t>(Part::Hair)] = kNullFormId;
        EXPECT_TRUE(validChoice(index, c, why)) << "no hair is allowed: " << why;
    }

    TEST(SkyrimCharacterChoice, anUnplayableRaceAMissingSkeletonOrAWrongPartIsRefusedWithAReason)
    {
        const ActorIndex index = makeIndex();
        std::string why;
        Choice c = defaultChoice(index, kNord, false);
        c.mRace = kOrc;
        EXPECT_FALSE(validChoice(index, c, why));
        EXPECT_FALSE(why.empty());
        c.mRace = kElder;
        why.clear();
        EXPECT_FALSE(validChoice(index, c, why));
        c.mRace = 0xDEAD;
        EXPECT_FALSE(validChoice(index, c, why));
        c = defaultChoice(index, kNord, false);
        c.mParts[static_cast<std::size_t>(Part::Hair)] = kHairF1; // a woman's hair on a man
        why.clear();
        EXPECT_FALSE(validChoice(index, c, why));
        EXPECT_FALSE(why.empty());
        c = defaultChoice(index, kNord, false);
        c.mParts[static_cast<std::size_t>(Part::Eyes)] = kNullFormId; // eyes cannot be none
        EXPECT_FALSE(validChoice(index, c, why));
        c = defaultChoice(index, kNord, false);
        c.mName = std::string(kMaxNameBytes + 1, 'a');
        EXPECT_FALSE(validChoice(index, c, why));
    }

    // ---- names ---------------------------------------------------------------------------------------------------

    TEST(SkyrimCharacterName, aNameIsTrimmedPrintableAsciiAndShort)
    {
        EXPECT_EQ(sanitiseName("  Ria  "), "Ria");
        EXPECT_EQ(sanitiseName("Ri\ta\n"), "Ria");
        EXPECT_EQ(sanitiseName("R\xC3\xA9"
                               "a"),
            "Ra");
        EXPECT_EQ(sanitiseName(""), "");
        EXPECT_EQ(sanitiseName("   "), "");
        EXPECT_EQ(sanitiseName(std::string(100, 'x')).size(), kMaxNameBytes);
        EXPECT_EQ(sanitiseName("Two  Words"), "Two  Words");
    }

    // ---- the body request ----------------------------------------------------------------------------------------

    TEST(SkyrimCharacterBody, applyChoicePutsTheRaceSexAndThreeHeadChoicesInTheRequest)
    {
        Choice c;
        c.mRace = kArgonian;
        c.mFemale = true;
        c.mParts[static_cast<std::size_t>(Part::Hair)] = kHairArg;
        c.mParts[static_cast<std::size_t>(Part::Beard)] = kNullFormId;
        c.mParts[static_cast<std::size_t>(Part::Eyes)] = 0x55;
        PlayerBodyRequest request;
        request.mWeapon = 0x600;
        applyChoice(c, request);
        EXPECT_EQ(request.mRace, kArgonian);
        EXPECT_TRUE(request.mFemale);
        EXPECT_EQ(request.mWeapon, 0x600u) << "what he holds is not touched";
        ASSERT_EQ(request.mHeadChoice.size(), 3u);
        EXPECT_EQ(request.mHeadChoice[0], (std::pair<std::uint32_t, FormId>{ 3, kHairArg }));
        EXPECT_EQ(request.mHeadChoice[1], (std::pair<std::uint32_t, FormId>{ 4, kNullFormId }));
        EXPECT_EQ(request.mHeadChoice[2], (std::pair<std::uint32_t, FormId>{ 2, 0x55 }));
    }

    std::vector<std::string> headModels(const ActorAppearance& a)
    {
        std::vector<std::string> out;
        for (const auto& p : a.parts)
            if (p.kind == ActorPartKind::HeadPart)
                out.push_back(p.modelPath);
        return out;
    }

    TEST(SkyrimCharacterBody, aChosenHairReplacesTheDefaultHairAndItsExtraPartsAndKeepsTheOtherDefaults)
    {
        const ActorIndex index = makeIndex();
        PlayerBodyRequest base;
        ActorAppearance def;
        std::string error;
        ASSERT_TRUE(makePlayerAppearance(index, base, def, error)) << error;
        const auto defaults = headModels(def);
        const auto has = [](const std::vector<std::string>& v, const std::string& m) {
            return std::find(v.begin(), v.end(), m) != v.end();
        };
        ASSERT_TRUE(has(defaults, "parts\\HairMaleNord01.nif"));
        ASSERT_TRUE(has(defaults, "parts\\EyesMaleNord01.nif"));

        PlayerBodyRequest request;
        request.mHeadChoice = { { 3, kHairM2 } };
        ActorAppearance a;
        ASSERT_TRUE(makePlayerAppearance(index, request, a, error)) << error;
        const auto heads = headModels(a);
        EXPECT_TRUE(has(heads, "parts\\HairMaleNord02.nif"));
        EXPECT_FALSE(has(heads, "parts\\HairMaleNord01.nif"));
        EXPECT_FALSE(has(heads, "parts\\HairLineNord01.nif")) << "the extra part of the replaced hair goes with it";
        EXPECT_TRUE(has(heads, "parts\\EyesMaleNord01.nif")) << "eyes were not chosen: the default stays";
        EXPECT_TRUE(has(heads, "parts\\FaceNord.nif"));
    }

    TEST(SkyrimCharacterBody, noneRemovesTheDefaultAndABeardIsAdded)
    {
        const ActorIndex index = makeIndex();
        PlayerBodyRequest request;
        request.mHeadChoice = { { 3, kNullFormId }, { 4, kBeardM1 } };
        ActorAppearance a;
        std::string error;
        ASSERT_TRUE(makePlayerAppearance(index, request, a, error)) << error;
        const auto heads = headModels(a);
        const auto has = [&](const std::string& m) { return std::find(heads.begin(), heads.end(), m) != heads.end(); };
        EXPECT_FALSE(has("parts\\HairMaleNord01.nif"));
        EXPECT_FALSE(has("parts\\HairLineNord01.nif"));
        EXPECT_TRUE(has("parts\\BeardNord01.nif"));
    }

    TEST(SkyrimCharacterBody, aRaceChoiceChangesTheSkeletonAndTheHeadsOfThatRace)
    {
        const ActorIndex index = makeIndex();
        Choice c = defaultChoice(index, kArgonian, true);
        PlayerBodyRequest request;
        applyChoice(c, request);
        ActorAppearance a;
        std::string error;
        ASSERT_TRUE(makePlayerAppearance(index, request, a, error)) << error;
        EXPECT_TRUE(a.female);
        EXPECT_EQ(a.skeletonPath, "female\\skeleton_female.nif");
    }

    // ---- the save blob -------------------------------------------------------------------------------------------

    TEST(SkyrimCharacterSave, aChoiceRoundTripsAndTheBytesAreCanonical)
    {
        Choice c;
        c.mRace = kArgonian;
        c.mFemale = true;
        c.mParts = { kHairArg, kNullFormId, 0x55 };
        c.mName = "Ria";
        const std::vector<std::uint8_t> bytes = encodeChoice(c);
        ASSERT_FALSE(bytes.empty());
        Choice back;
        std::string error;
        ASSERT_TRUE(decodeChoice(bytes, back, error)) << error;
        EXPECT_EQ(back, c);
        EXPECT_EQ(encodeChoice(back), bytes);
    }

    TEST(SkyrimCharacterSave, everyTruncationAndATrailingByteIsRefusedAndLeavesTheOutputAlone)
    {
        Choice c;
        c.mName = "Ria";
        std::vector<std::uint8_t> bytes = encodeChoice(c);
        ASSERT_FALSE(bytes.empty());
        Choice sentinel;
        sentinel.mName = "keep";
        for (std::size_t n = 0; n < bytes.size(); ++n)
        {
            Choice out = sentinel;
            std::string error;
            EXPECT_FALSE(decodeChoice(std::span<const std::uint8_t>(bytes.data(), n), out, error)) << n;
            EXPECT_FALSE(error.empty());
            EXPECT_EQ(out, sentinel);
        }
        bytes.push_back(0);
        Choice out = sentinel;
        std::string error;
        EXPECT_FALSE(decodeChoice(bytes, out, error));
        EXPECT_EQ(out, sentinel);
    }

    TEST(SkyrimCharacterSave, aNewerVersionAndAnOverlongNameAreRefused)
    {
        Choice c;
        std::vector<std::uint8_t> bytes = encodeChoice(c);
        ASSERT_FALSE(bytes.empty());
        bytes[0] = 9;
        Choice out;
        std::string error;
        EXPECT_FALSE(decodeChoice(bytes, out, error));
        EXPECT_NE(error.find("version"), std::string::npos);

        c.mName = std::string(kMaxNameBytes, 'a');
        std::vector<std::uint8_t> ok = encodeChoice(c);
        // grow the name's length field past the cap and append the bytes
        Choice big;
        big.mName = std::string(kMaxNameBytes + 1, 'a');
        const std::vector<std::uint8_t> tooLong = encodeChoice(big);
        Choice sink;
        EXPECT_FALSE(decodeChoice(tooLong, sink, error)) << "the encoder keeps the name, the decoder refuses it";
        EXPECT_TRUE(decodeChoice(ok, sink, error)) << error;
    }

    // ---- the working choice --------------------------------------------------------------------------------------

    TEST(SkyrimCharacterCreator, startsAsTheNordManWhenThereIsOneAndOffersTheRaces)
    {
        const ActorIndex index = makeIndex();
        Creator creator(index, plain());
        ASSERT_TRUE(creator.ok());
        ASSERT_EQ(creator.races().size(), 2u);
        ASSERT_NE(creator.race(), nullptr);
        EXPECT_EQ(creator.race()->mId, kNord);
        EXPECT_FALSE(creator.female());
        EXPECT_EQ(creator.choice().mParts[static_cast<std::size_t>(Part::Hair)], kHairM1);
        EXPECT_EQ(creator.options(Part::Hair).size(), 3u);
        EXPECT_EQ(creator.options(Part::Hair)[creator.partIndex(Part::Hair)].mId, kHairM1);
    }

    TEST(SkyrimCharacterCreator, anIndexWithNoPlayableRaceIsNotOk)
    {
        const ActorIndex index;
        Creator creator(index, plain());
        EXPECT_FALSE(creator.ok());
        EXPECT_FALSE(creator.selectRace(0));
    }

    TEST(SkyrimCharacterCreator, aRaceChangeGoesBackToThatRacesDefaultsAndKeepsTheSexAndName)
    {
        const ActorIndex index = makeIndex();
        Creator creator(index, plain());
        creator.setName("Ria");
        creator.stepPart(Part::Hair, 1);
        const std::uint64_t before = creator.revision();
        ASSERT_TRUE(creator.selectRace(0)); // Argonian
        EXPECT_GT(creator.revision(), before);
        EXPECT_EQ(creator.choice().mRace, kArgonian);
        EXPECT_EQ(creator.choice().mParts[static_cast<std::size_t>(Part::Hair)], kHairArg);
        EXPECT_EQ(creator.choice().mName, "Ria");
        EXPECT_FALSE(creator.selectRace(5));
        EXPECT_EQ(creator.raceIndex(), 0u);
    }

    TEST(SkyrimCharacterCreator, theSexChangeGoesBackToTheRacesDefaultsForTheSex)
    {
        const ActorIndex index = makeIndex();
        Creator creator(index, plain());
        creator.stepPart(Part::Hair, 1);
        creator.setFemale(true);
        EXPECT_TRUE(creator.female());
        EXPECT_EQ(creator.choice().mParts[static_cast<std::size_t>(Part::Hair)], kHairF1);
        EXPECT_EQ(creator.options(Part::Hair).size(), 2u);
        const std::uint64_t r = creator.revision();
        creator.setFemale(true); // no change
        EXPECT_EQ(creator.revision(), r);
    }

    TEST(SkyrimCharacterCreator, aPartStepsThroughItsOptionsAndWrapsInBothDirections)
    {
        const ActorIndex index = makeIndex();
        Creator creator(index, plain());
        const std::size_t hair = static_cast<std::size_t>(Part::Hair);
        // options: None, M1, M2; the default is M1 (index 1)
        EXPECT_EQ(creator.partIndex(Part::Hair), 1u);
        creator.stepPart(Part::Hair, 1);
        EXPECT_EQ(creator.choice().mParts[hair], kHairM2);
        creator.stepPart(Part::Hair, 1);
        EXPECT_EQ(creator.choice().mParts[hair], kNullFormId) << "wrapped to None";
        creator.stepPart(Part::Hair, -1);
        EXPECT_EQ(creator.choice().mParts[hair], kHairM2);
        creator.stepPart(Part::Hair, -2);
        EXPECT_EQ(creator.choice().mParts[hair], kNullFormId + 0) << "two back from M2 is None";
        creator.stepPart(Part::Hair, 3000);
        EXPECT_EQ(creator.partIndex(Part::Hair), 0u) << "3000 is a multiple of three places";
        const std::uint64_t r = creator.revision();
        creator.stepPart(Part::Eyes, 0);
        EXPECT_EQ(creator.revision(), r) << "a step of nothing changes nothing";
    }

    TEST(SkyrimCharacterCreator, aNameIsSanitisedAndLoadTakesOnlyAValidChoice)
    {
        const ActorIndex index = makeIndex();
        Creator creator(index, plain());
        creator.setName("  Ri\ta  ");
        EXPECT_EQ(creator.choice().mName, "Ria");
        Choice c = defaultChoice(index, kArgonian, true);
        c.mName = "Saxhleel";
        std::string why;
        ASSERT_TRUE(creator.load(c, why)) << why;
        EXPECT_EQ(creator.choice(), c);
        EXPECT_EQ(creator.race()->mId, kArgonian);
        EXPECT_TRUE(creator.female());
        Choice bad = c;
        bad.mRace = kOrc;
        const Choice keep = creator.choice();
        EXPECT_FALSE(creator.load(bad, why));
        EXPECT_FALSE(why.empty());
        EXPECT_EQ(creator.choice(), keep);
    }

    // ---- the starting skills (written after the code: see the stage log) ------------------------------------------

    TEST(SkyrimCharacterSkills, aRacesBoostsRaiseTheSkillsOfTheStartAndClampAtTheTop)
    {
        progression::SkillTable table;
        progression::PlayerProgression p(table, nullptr);
        p.setStart(1, 15);
        const std::uint64_t before = p.revision();
        const std::vector<std::pair<std::size_t, std::uint8_t>> boosts{ { progression::Slot::TwoHanded, 10 },
            { progression::Slot::OneHanded, 5 }, { 99, 5 }, { progression::Slot::Block, 0 } };
        EXPECT_EQ(p.applyRacialBoosts(boosts), 2u) << "a bad slot and a boost of nothing change nothing";
        EXPECT_EQ(p.skill(progression::Slot::TwoHanded), 25);
        EXPECT_EQ(p.skill(progression::Slot::OneHanded), 20);
        EXPECT_EQ(p.skill(progression::Slot::Block), 15);
        EXPECT_GT(p.revision(), before);
        const std::vector<std::pair<std::size_t, std::uint8_t>> big{ { progression::Slot::Archery, 200 } };
        EXPECT_EQ(p.applyRacialBoosts(big), 1u);
        EXPECT_EQ(p.skill(progression::Slot::Archery), progression::kMaxSkill);
        const std::uint64_t again = p.revision();
        EXPECT_EQ(p.applyRacialBoosts(big), 0u) << "already at the top";
        EXPECT_EQ(p.revision(), again);
    }

    TEST(SkyrimCharacterSkills, theBoostsSurviveTheSaveBlob)
    {
        progression::SkillTable table;
        progression::PlayerProgression p(table, nullptr);
        p.setStart(1, 15);
        const std::vector<std::pair<std::size_t, std::uint8_t>> boosts{ { progression::Slot::Speechcraft, 5 } };
        (void)p.applyRacialBoosts(boosts);
        progression::PlayerProgression q(table, nullptr);
        std::string error;
        ASSERT_TRUE(q.restore(p.serialise(), error)) << error;
        EXPECT_EQ(q.skill(progression::Slot::Speechcraft), 20);
    }

    TEST(SkyrimCharacterSkills, aRaceOptionsBoostsMapToSkillSlotsByActorValue)
    {
        const ActorIndex index = makeIndex();
        const auto races = playableRaces(index, plain());
        ASSERT_EQ(races.size(), 2u);
        std::vector<std::pair<std::size_t, std::uint8_t>> slots;
        for (const RaceSkillBoost& b : races[1].mBoosts) // the Nord
            slots.emplace_back(b.mActorValue - progression::kFirstSkillValue, b.mBoost);
        ASSERT_EQ(slots.size(), 2u);
        EXPECT_EQ(slots[0].first, progression::Slot::TwoHanded);
        EXPECT_EQ(slots[1].first, progression::Slot::OneHanded);
    }

    // ---- the installed data (gated) ------------------------------------------------------------------------------

    TEST(SkyrimCharacterRealData, theTenPlayerRacesAndTheirPartsOnTheInstalledMasters)
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
        const auto races = playableRaces(index, TextFn());
        EXPECT_EQ(races.size(), 10u);
        const auto nordIt
            = std::find_if(races.begin(), races.end(), [](const RaceOption& r) { return r.mId == kNord; });
        ASSERT_NE(nordIt, races.end());
        EXPECT_EQ(describeBoosts(nordIt->mBoosts),
            "Two-Handed +10, One-Handed +5, Block +5, Smithing +5, Speech +5, Light Armor +5");
        EXPECT_GE(nordIt->mAbilities.size(), 2u);
        std::size_t inserted = 0;
        for (const RaceOption& race : races)
            for (const bool female : { false, true })
            {
                const Choice c = defaultChoice(index, race.mId, female);
                std::string why;
                EXPECT_TRUE(validChoice(index, c, why)) << race.mEditorId << " " << female << ": " << why;
                for (std::size_t p = 0; p < kPartCount; ++p)
                {
                    const auto options = partOptions(index, TextFn(), race.mId, female, static_cast<Part>(p));
                    EXPECT_FALSE(options.empty()) << race.mEditorId;
                }
                (void)inserted;
            }
        const auto count = [&](bool female, Part part) {
            auto options = partOptions(index, TextFn(), kNord, female, part);
            return options.size() - (part == Part::Eyes ? 0 : 1);
        };
        std::cout << "SKYRIMSE-CHARACTER nord male hair " << count(false, Part::Hair) << " beard "
                  << count(false, Part::Beard) << " eyes " << count(false, Part::Eyes) << "; female hair "
                  << count(true, Part::Hair) << " beard " << count(true, Part::Beard) << " eyes "
                  << count(true, Part::Eyes) << "\n";
        EXPECT_EQ(count(false, Part::Hair), 29u);
        EXPECT_EQ(count(false, Part::Beard), 46u);
        EXPECT_EQ(count(false, Part::Eyes), 22u);
        EXPECT_EQ(count(true, Part::Hair), 26u);
        EXPECT_EQ(count(true, Part::Beard), 0u);
        EXPECT_EQ(count(true, Part::Eyes), 20u);
    }
}
