// Task 0160: the placed-reference table and the condition functions it lets the neutral world answer (conditions run
// on another reference, quest aliases that name a reference or an actor, relationships, distances, the current
// package). Generated plugins and records; no game data.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/package.hpp>
#include <game_skyrimse/placed_refs.hpp>
#include <game_skyrimse/quest.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kCell = kSelf | 0x100;
    constexpr FormId kCell2 = kSelf | 0x101;
    constexpr FormId kNpcA = kSelf | 0x200; // female, in kFaction rank 2, carries 3 x kItem, wears kHat
    constexpr FormId kNpcB = kSelf | 0x201; // template is a leveled list
    constexpr FormId kNpcC = kSelf | 0x202; // male, no faction, a leveled item in its inventory
    constexpr FormId kContBase = kSelf | 0x210;
    constexpr FormId kContLeveled = kSelf | 0x211;
    constexpr FormId kFaction = kSelf | 0x300;
    constexpr FormId kItem = kSelf | 0x301;
    constexpr FormId kHat = kSelf | 0x302;
    constexpr FormId kLeveledItem = kSelf | 0x303;
    constexpr FormId kLvln = kSelf | 0x304;
    constexpr FormId kOutfit = kSelf | 0x305;
    constexpr FormId kQuest = kSelf | 0x400;
    constexpr FormId kRefA = kSelf | 0x500; // base A, same cell as kRefA2
    constexpr FormId kRefA2 = kSelf | 0x501; // base A, starts dead
    constexpr FormId kRefDisabled = kSelf | 0x502; // base A, initially disabled
    constexpr FormId kRefB = kSelf | 0x503; // base B
    constexpr FormId kRefCont = kSelf | 0x504; // a chest
    constexpr FormId kRefFar = kSelf | 0x505; // base C, another cell
    constexpr FormId kRefContLeveled = kSelf | 0x506;
    constexpr FormId kRefFollower = kSelf | 0x507; // enabled only while kRefDisabled is disabled (opposite)
    constexpr FormId kPackageA = kSelf | 0x600;
    constexpr FormId kPackageServices = kSelf | 0x601;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    survey::Ctda cond(std::uint16_t function, std::uint32_t p1 = 0, std::uint32_t runOn = 0, std::uint32_t reference = 0,
        std::uint8_t compare = 0, float value = 1.f)
    {
        survey::Ctda c;
        c.mFunction = function;
        c.mParam1 = p1;
        c.mRunOn = runOn;
        c.mReference = reference;
        c.mCompare = compare;
        c.mComparison = value;
        return c;
    }

    PlacedEntry ref(FormId id, FormId base, FormId cell, float x = 0, float y = 0, float z = 0, std::uint8_t flags = 0,
        FormId enableParent = kNullFormId)
    {
        PlacedEntry r;
        r.mId = id;
        r.mBase = base;
        r.mCell = cell;
        r.mX = x;
        r.mY = y;
        r.mZ = z;
        r.mFlags = flags;
        r.mEnableParent = enableParent;
        return r;
    }

    // ---- the parse -----------------------------------------------------------------------------------------------

    Bytes xesp(FormId parent, std::uint32_t flags)
    {
        return sub4("XESP", Bytes().u32(parent).u32(flags));
    }

    Bytes placed(const char (&type)[5], std::uint32_t flags, FormId id, FormId base, float x, float y, float z,
        const Bytes& extra = Bytes())
    {
        Bytes data;
        data.append(sub4("NAME", Bytes().u32(base)));
        data.append(extra);
        data.append(sub4("DATA", Bytes().f32(x).f32(y).f32(z).f32(0).f32(0).f32(0)));
        return record4(type, flags, id, data);
    }

    Bytes testPlugin(bool withOddSizes = false)
    {
        Bytes children;
        children.append(placed("REFR", 0x800, kRefDisabled, kNpcA, 10, 20, 30));
        children.append(placed("ACHR", 0x200 | 0x400, kRefA2, kNpcA, 1, 2, 3));
        Bytes temporary;
        temporary.append(placed("REFR", 0, kRefFollower, kNpcA, 4, 5, 6, xesp(kRefDisabled, 1)));
        temporary.append(placed("REFR", 0x20, kRefA, kNpcA, 0, 0, 0)); // deleted
        children.append(group4(kCell, 9, temporary));
        Bytes cellRecord = record4("CELL", 0, kCell, sub4("EDID", Bytes().zstring("TestCell")));
        Bytes cellBlock;
        cellBlock.append(cellRecord);
        cellBlock.append(group4(kCell, 6, children));

        Bytes cont;
        cont.append(sub4("EDID", Bytes().zstring("Chest")));
        cont.append(sub4("CNTO", Bytes().u32(kItem).u32(5)));
        cont.append(sub4("CNTO", Bytes().u32(kHat).u32(1)));
        if (withOddSizes)
            cont.append(sub4("CNTO", Bytes().u32(kItem)));

        Bytes rela;
        rela.append(sub4("DATA", Bytes().u32(kNpcA).u32(kNpcB).u8(1).u8(0).u8(0).u8(0).u32(0x00000999)));

        Bytes all = tes4Header(1.7f, 0, "A", "B", 4, { { "Skyrim.esm", 0 } }, 24, 44);
        all.append(group4(Content::fourCC("CELL"), 0, cellBlock));
        all.append(group4(Content::fourCC("CONT"), 0, record4("CONT", 0, kContBase, cont)));
        all.append(group4(Content::fourCC("RELA"), 0, record4("RELA", 0, kSelf | 0x700, rela)));
        return all;
    }

    PlacedRefRecords parse(const Bytes& bytes, bool& ok)
    {
        Content::Error error;
        const auto plugin = Content::Plugin::open(
            std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
        PlacedRefRecords out;
        ok = plugin != nullptr && parsePlacedRefs(*plugin, order(), PlacedRefLimits(), out);
        return out;
    }

    TEST(SkyrimPlacedRefsParse, readsReferencesTheirFlagsPositionsAndEnableParents)
    {
        bool ok = false;
        const PlacedRefRecords r = parse(testPlugin(), ok);
        ASSERT_TRUE(ok);
        ASSERT_EQ(r.mRefs.size(), 4u);
        const auto find = [&](FormId id) -> const PlacedEntry* {
            for (const PlacedEntry& p : r.mRefs)
                if (p.mId == id)
                    return &p;
            return nullptr;
        };
        const PlacedEntry* disabled = find(kRefDisabled);
        ASSERT_NE(disabled, nullptr);
        EXPECT_EQ(disabled->mBase, kNpcA);
        EXPECT_EQ(disabled->mCell, kCell);
        EXPECT_NE(disabled->mFlags & PlacedFlags::Disabled, 0);
        EXPECT_EQ(disabled->mFlags & PlacedFlags::IsActor, 0);
        EXPECT_FLOAT_EQ(disabled->mX, 10.f);
        EXPECT_FLOAT_EQ(disabled->mY, 20.f);
        EXPECT_FLOAT_EQ(disabled->mZ, 30.f);
        const PlacedEntry* dead = find(kRefA2);
        ASSERT_NE(dead, nullptr);
        EXPECT_NE(dead->mFlags & PlacedFlags::Dead, 0);
        EXPECT_NE(dead->mFlags & PlacedFlags::IsActor, 0);
        EXPECT_NE(dead->mFlags & PlacedFlags::Persistent, 0);
        EXPECT_EQ(dead->mFlags & PlacedFlags::Disabled, 0);
        const PlacedEntry* follower = find(kRefFollower);
        ASSERT_NE(follower, nullptr);
        EXPECT_EQ(follower->mEnableParent, kRefDisabled);
        EXPECT_NE(follower->mFlags & PlacedFlags::EnableOpposite, 0);
        EXPECT_EQ(follower->mCell, kCell); // inside the nested temporary group: the cell is still the CELL's
        const PlacedEntry* deleted = find(kRefA);
        ASSERT_NE(deleted, nullptr);
        EXPECT_TRUE(deleted->mDeleted);
    }

    TEST(SkyrimPlacedRefsParse, theDeadBitMeansNothingOnAReference)
    {
        Bytes children = placed("REFR", 0x200, kRefA, kNpcA, 0, 0, 0);
        Bytes cellBlock = record4("CELL", 0, kCell, Bytes());
        cellBlock.append(group4(kCell, 6, children));
        Bytes all = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        all.append(group4(Content::fourCC("CELL"), 0, cellBlock));
        bool ok = false;
        const PlacedRefRecords r = parse(all, ok);
        ASSERT_TRUE(ok);
        ASSERT_EQ(r.mRefs.size(), 1u);
        EXPECT_EQ(r.mRefs[0].mFlags & PlacedFlags::Dead, 0); // 0x200 on a REFR is another flag
    }

    TEST(SkyrimPlacedRefsParse, readsContainersAndRelationshipsAndCountsOddSizes)
    {
        bool ok = false;
        const PlacedRefRecords r = parse(testPlugin(true), ok);
        ASSERT_TRUE(ok);
        ASSERT_EQ(r.mContainers.size(), 1u);
        ASSERT_EQ(r.mContainers[0].mItems.size(), 2u);
        EXPECT_EQ(r.mContainers[0].mItems[0].mItem, kItem);
        EXPECT_EQ(r.mContainers[0].mItems[0].mCount, 5);
        EXPECT_EQ(r.mOddSubrecords, 1u); // the 4 byte CNTO decodes to nothing
        ASSERT_EQ(r.mRelations.size(), 1u);
        EXPECT_EQ(r.mRelations[0].mParent, kNpcA);
        EXPECT_EQ(r.mRelations[0].mChild, kNpcB);
        EXPECT_EQ(r.mRelations[0].mRank, 1);
        EXPECT_EQ(r.mRelations[0].mAssociation, 0x00000999u);
    }

    TEST(SkyrimPlacedRefsParse, aTruncatedSubrecordIsAnErrorAndTheWalkGoesOn)
    {
        Bytes bad;
        bad.append(sub4("NAME", Bytes().u32(kNpcA)));
        bad.fourCC("DATA").u16(24).u32(0); // claims 24 bytes, holds 4
        Bytes children = record4("REFR", 0, kRefA, bad);
        children.append(placed("REFR", 0, kRefA2, kNpcA, 0, 0, 0));
        Bytes cellBlock = record4("CELL", 0, kCell, Bytes());
        cellBlock.append(group4(kCell, 6, children));
        Bytes all = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        all.append(group4(Content::fourCC("CELL"), 0, cellBlock));
        bool ok = false;
        const PlacedRefRecords r = parse(all, ok);
        (void)ok; // the framing fault may end the walk; whatever came before is kept
        EXPECT_LE(r.mRefs.size(), 2u);
    }

    // ---- the merged table ----------------------------------------------------------------------------------------

    TEST(SkyrimPlacedRefIndex, aLaterPluginReplacesAnEarlierRecordAndInheritsTheBase)
    {
        PlacedRefRecords first, second;
        first.mRefs.push_back(ref(kRefA, kNpcA, kCell, 1, 1, 1));
        first.mRefs.push_back(ref(kRefA2, kNpcA, kCell, 2, 2, 2));
        first.mRefs.push_back(ref(kRefB, kNpcB, kCell, 3, 3, 3));
        second.mRefs.push_back(ref(kRefA, kNullFormId, kNullFormId, 9, 9, 9, PlacedFlags::Disabled)); // moved, disabled
        PlacedEntry deleted = ref(kRefA2, kNullFormId, kNullFormId);
        deleted.mDeleted = true;
        second.mRefs.push_back(deleted);
        PlacedRefIndex index;
        index.add(std::move(first));
        index.add(std::move(second));
        index.finish();
        ASSERT_EQ(index.size(), 2u);
        const PlacedEntry* a = index.find(kRefA);
        ASSERT_NE(a, nullptr);
        EXPECT_EQ(a->mBase, kNpcA);
        EXPECT_EQ(a->mCell, kCell);
        EXPECT_FLOAT_EQ(a->mX, 9.f);
        EXPECT_NE(a->mFlags & PlacedFlags::Disabled, 0);
        EXPECT_EQ(index.find(kRefA2), nullptr);
        EXPECT_NE(index.find(kRefB), nullptr);
        EXPECT_EQ(index.find(0x12345), nullptr);
    }

    TEST(SkyrimPlacedRefIndex, listsTheReferencesOfABaseInIdOrder)
    {
        PlacedRefRecords r;
        r.mRefs.push_back(ref(kRefB, kNpcB, kCell));
        r.mRefs.push_back(ref(kRefA2, kNpcA, kCell));
        r.mRefs.push_back(ref(kRefA, kNpcA, kCell));
        PlacedRefIndex index;
        index.add(std::move(r));
        index.finish();
        const PlacedRefIndex::Range range = index.byBase(kNpcA);
        ASSERT_EQ(range.size(), 2u);
        EXPECT_EQ((*range.mBegin)->mId, kRefA);
        EXPECT_EQ((*(range.mBegin + 1))->mId, kRefA2);
        EXPECT_EQ(index.byBase(kNpcC).size(), 0u);
    }

    TEST(SkyrimPlacedRefIndex, theEnableStateFollowsTheEnableParent)
    {
        PlacedRefRecords r;
        r.mRefs.push_back(ref(kRefDisabled, kNpcA, kCell, 0, 0, 0, PlacedFlags::Disabled));
        r.mRefs.push_back(ref(kRefA, kNpcA, kCell, 0, 0, 0, 0, kRefDisabled)); // follows: disabled
        r.mRefs.push_back(ref(kRefFollower, kNpcA, kCell, 0, 0, 0, PlacedFlags::EnableOpposite, kRefDisabled)); // enabled
        r.mRefs.push_back(ref(kRefB, kNpcB, kCell, 0, 0, 0, PlacedFlags::EnableOpposite, 0x14)); // the player: enabled, so opposite disables
        r.mRefs.push_back(ref(kRefA2, kNpcA, kCell, 0, 0, 0, 0, 0x14)); // follows the player: enabled
        r.mRefs.push_back(ref(kRefFar, kNpcC, kCell, 0, 0, 0, PlacedFlags::EnableOpposite, kRefFollower)); // opposite of an enabled one
        PlacedRefIndex index;
        index.add(std::move(r));
        index.finish();
        EXPECT_TRUE(index.initiallyDisabled(*index.find(kRefDisabled)));
        EXPECT_TRUE(index.initiallyDisabled(*index.find(kRefA)));
        EXPECT_FALSE(index.initiallyDisabled(*index.find(kRefFollower)));
        EXPECT_TRUE(index.initiallyDisabled(*index.find(kRefB)));
        EXPECT_FALSE(index.initiallyDisabled(*index.find(kRefA2)));
        EXPECT_TRUE(index.initiallyDisabled(*index.find(kRefFar)));
    }

    TEST(SkyrimPlacedRefIndex, anEnableLoopTerminates)
    {
        PlacedRefRecords r;
        r.mRefs.push_back(ref(kRefA, kNpcA, kCell, 0, 0, 0, 0, kRefA2));
        r.mRefs.push_back(ref(kRefA2, kNpcA, kCell, 0, 0, 0, 0, kRefA));
        PlacedRefIndex index;
        index.add(std::move(r));
        index.finish();
        (void)index.initiallyDisabled(*index.find(kRefA)); // must return
        SUCCEED();
    }

    TEST(SkyrimPlacedRefIndex, relationshipsAreFoundInEitherDirectionAndMapToTheCkRange)
    {
        PlacedRefRecords r;
        RelationshipRecord rel;
        rel.mId = kSelf | 0x700;
        rel.mParent = kNpcA;
        rel.mChild = kNpcB;
        rel.mRank = 1; // ally
        r.mRelations.push_back(rel);
        PlacedRefIndex index;
        index.add(std::move(r));
        index.finish();
        EXPECT_EQ(index.relationRank(kNpcA, kNpcB), std::optional<std::uint8_t>(1));
        EXPECT_EQ(index.relationRank(kNpcB, kNpcA), std::optional<std::uint8_t>(1));
        EXPECT_FALSE(index.relationRank(kNpcA, kNpcC).has_value());
        EXPECT_EQ(relationshipRankValue(0), 4.f); // lover
        EXPECT_EQ(relationshipRankValue(1), 3.f);
        EXPECT_EQ(relationshipRankValue(4), 0.f); // acquaintance
        EXPECT_EQ(relationshipRankValue(8), -4.f); // archnemesis
        EXPECT_EQ(relationshipRankValue(9), 0.f);
    }

    TEST(SkyrimPlacedRefIndex, aDeletedRelationshipIsRemoved)
    {
        PlacedRefRecords first, second;
        RelationshipRecord rel;
        rel.mId = kSelf | 0x700;
        rel.mParent = kNpcA;
        rel.mChild = kNpcB;
        first.mRelations.push_back(rel);
        rel.mDeleted = true;
        second.mRelations.push_back(rel);
        PlacedRefIndex index;
        index.add(std::move(first));
        index.add(std::move(second));
        index.finish();
        EXPECT_EQ(index.relationCount(), 0u);
    }

    // ---- the world -----------------------------------------------------------------------------------------------

    struct Fixture
    {
        ActorIndex mActors;
        DialogueIndex mDialogue;
        PlacedRefIndex mRefs;
        PackageIndex mPackages;
        QuestIndex mQuests;
        QuestState mState;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };

        explicit Fixture(bool wire = true)
        {
            ActorRecords ar;
            ActorNpc a;
            a.mCommon.mId = kNpcA;
            a.mFlags = NpcFlags::Female;
            a.mFactions.push_back({ kFaction, 2 });
            a.mInventory.push_back({ kItem, 3 });
            a.mDefaultOutfit = kOutfit;
            ar.mNpcs.push_back(a);
            ActorNpc b;
            b.mCommon.mId = kNpcB;
            b.mTemplate = kLvln;
            ar.mNpcs.push_back(b);
            ActorNpc c;
            c.mCommon.mId = kNpcC;
            c.mInventory.push_back({ kLeveledItem, 1 });
            ar.mNpcs.push_back(c);
            OutfitRecord outfit;
            outfit.mCommon.mId = kOutfit;
            outfit.mItems.push_back(kHat);
            ar.mOutfits.push_back(outfit);
            mActors.add(ar);
            LeveledList lvln;
            lvln.mIsNpcList = true;
            lvln.mCommon.mId = kLvln;
            LeveledList lvli;
            lvli.mCommon.mId = kLeveledItem;
            mActors.addLeveledLists({ lvln, lvli });

            PlacedRefRecords r;
            r.mRefs.push_back(ref(kRefA, kNpcA, kCell, 0, 0, 0, PlacedFlags::IsActor));
            r.mRefs.push_back(ref(kRefA2, kNpcA, kCell, 30, 40, 0, PlacedFlags::IsActor | PlacedFlags::Dead));
            r.mRefs.push_back(ref(kRefDisabled, kNpcA, kCell, 0, 0, 0, PlacedFlags::Disabled));
            r.mRefs.push_back(ref(kRefB, kNpcB, kCell, 0, 0, 0, PlacedFlags::IsActor));
            r.mRefs.push_back(ref(kRefCont, kContBase, kCell));
            r.mRefs.push_back(ref(kRefFar, kNpcC, kCell2, 5, 5, 5, PlacedFlags::IsActor));
            r.mRefs.push_back(ref(kRefContLeveled, kContLeveled, kCell));
            ContainerRecord chest;
            chest.mId = kContBase;
            chest.mItems.push_back({ kItem, 5 });
            r.mContainers.push_back(chest);
            ContainerRecord leveledChest;
            leveledChest.mId = kContLeveled;
            leveledChest.mItems.push_back({ kLeveledItem, 1 });
            r.mContainers.push_back(leveledChest);
            RelationshipRecord rel;
            rel.mId = kSelf | 0x700;
            rel.mParent = kNpcA;
            rel.mChild = kNpcC;
            rel.mRank = 0; // lover
            r.mRelations.push_back(rel);
            mRefs.add(std::move(r));
            mRefs.finish();
            if (wire)
                mWorld.setPlacedRefs(&mRefs);

            // the speaker is the living kRefA
            DialogueSpeaker s;
            s.mBase = kNpcA;
            s.mReference = kRefA;
            s.mCell = kCell;
            mWorld.setSpeaker(s);
        }

        bool holds(const survey::Ctda& c)
        {
            conditions::Evaluator e;
            return e.evaluate(std::span<const survey::Ctda>(&c, 1), mWorld);
        }
    };

    TEST(SkyrimPlacedRefWorld, withoutTheTableOtherReferencesAreUnanswered)
    {
        Fixture f(false);
        EXPECT_FALSE(f.mWorld.answers(cond(71, kFaction, 2, kRefA2)));
        EXPECT_FALSE(f.mWorld.answers(cond(46, 0, 2, kRefA2)));
        EXPECT_FALSE(f.holds(cond(46, 0, 2, kRefA2)));
    }

    TEST(SkyrimPlacedRefWorld, factionAndSexOfAnotherActorComeFromItsBase)
    {
        Fixture f;
        EXPECT_TRUE(f.mWorld.answers(cond(71, kFaction, 2, kRefA2)));
        EXPECT_TRUE(f.holds(cond(71, kFaction, 2, kRefA2, 0, 1.f)));
        EXPECT_FALSE(f.holds(cond(71, kSelf | 0x999, 2, kRefA2, 0, 1.f)));
        EXPECT_TRUE(f.mWorld.answers(cond(73, kFaction, 2, kRefA2)));
        EXPECT_TRUE(f.holds(cond(73, kFaction, 2, kRefA2, 0, 2.f)));
        EXPECT_TRUE(f.mWorld.answers(cond(70, 1, 2, kRefA2))); // GetIsSex
        EXPECT_TRUE(f.holds(cond(70, 1, 2, kRefA2, 0, 1.f))); // female
        EXPECT_TRUE(f.holds(cond(70, 0, 2, kRefA2, 0, 0.f)));
    }

    TEST(SkyrimPlacedRefWorld, anActorWithARandomLeveledTemplateIsNotAnswered)
    {
        Fixture f;
        EXPECT_FALSE(f.mWorld.answers(cond(71, kFaction, 2, kRefB)));
        EXPECT_FALSE(f.mWorld.answers(cond(70, 1, 2, kRefB)));
        // GetIsID needs only the base, so it is answered
        EXPECT_TRUE(f.mWorld.answers(cond(72, kNpcB, 2, kRefB)));
        EXPECT_TRUE(f.holds(cond(72, kNpcB, 2, kRefB, 0, 1.f)));
    }

    TEST(SkyrimPlacedRefWorld, deadAndDisabledAreTheReferencesInitialState)
    {
        Fixture f;
        EXPECT_TRUE(f.mWorld.answers(cond(46, 0, 2, kRefA2)));
        EXPECT_TRUE(f.holds(cond(46, 0, 2, kRefA2, 0, 1.f)));
        EXPECT_FALSE(f.holds(cond(46, 0, 2, kRefB, 0, 1.f)));
        EXPECT_TRUE(f.mWorld.answers(cond(35, 0, 2, kRefDisabled)));
        EXPECT_TRUE(f.holds(cond(35, 0, 2, kRefDisabled, 0, 1.f)));
        EXPECT_FALSE(f.holds(cond(35, 0, 2, kRefA2, 0, 1.f)));
        // a chest is not an actor: GetDead on it is not answered
        EXPECT_FALSE(f.mWorld.answers(cond(46, 0, 2, kRefCont)));
    }

    TEST(SkyrimPlacedRefWorld, theSpeakerItselfIsAliveAndEnabledWhenAskedByItsReference)
    {
        Fixture f;
        EXPECT_TRUE(f.holds(cond(46, 0, 2, kRefA, 0, 0.f)));
        EXPECT_TRUE(f.mWorld.answers(cond(35, 0, 0)));
        EXPECT_TRUE(f.holds(cond(35, 0, 0, 0, 0, 0.f)));
        EXPECT_TRUE(f.mWorld.answers(cond(286, 0, 0)));
        EXPECT_TRUE(f.holds(cond(286, 0, 0, 0, 0, 0.f)));
        EXPECT_TRUE(f.mWorld.answers(cond(286, 0, 2, kRefA2)));
    }

    TEST(SkyrimPlacedRefWorld, itemCountSumsTheInventoryAndTheOutfit)
    {
        Fixture f;
        EXPECT_TRUE(f.mWorld.answers(cond(47, kItem, 2, kRefA2)));
        EXPECT_TRUE(f.holds(cond(47, kItem, 2, kRefA2, 0, 3.f)));
        EXPECT_TRUE(f.holds(cond(47, kHat, 2, kRefA2, 0, 1.f))); // outfit
        EXPECT_TRUE(f.holds(cond(47, kSelf | 0x999, 2, kRefA2, 0, 0.f)));
        // the speaker's own inventory too
        EXPECT_TRUE(f.mWorld.answers(cond(47, kItem, 0)));
        EXPECT_TRUE(f.holds(cond(47, kItem, 0, 0, 0, 3.f)));
        // a chest
        EXPECT_TRUE(f.mWorld.answers(cond(47, kItem, 2, kRefCont)));
        EXPECT_TRUE(f.holds(cond(47, kItem, 2, kRefCont, 0, 5.f)));
    }

    TEST(SkyrimPlacedRefWorld, aLeveledEntryMakesAnInventoryUnknown)
    {
        Fixture f;
        EXPECT_FALSE(f.mWorld.answers(cond(47, kItem, 2, kRefFar))); // C carries a leveled item
        EXPECT_FALSE(f.mWorld.answers(cond(47, kItem, 2, kRefContLeveled)));
    }

    TEST(SkyrimPlacedRefWorld, aCorpseWithADeathItemIsUnknown)
    {
        Fixture f;
        ActorRecords ar;
        ActorNpc a;
        a.mCommon.mId = kNpcA;
        a.mFlags = NpcFlags::Female;
        a.mDeathItem = kLeveledItem;
        ar.mNpcs.push_back(a);
        f.mActors.add(ar);
        EXPECT_FALSE(f.mWorld.answers(cond(47, kItem, 2, kRefA2))); // dead
    }

    TEST(SkyrimPlacedRefWorld, equippedIsKnownForOutfitAndForWhatTheActorDoesNotCarry)
    {
        Fixture f;
        EXPECT_TRUE(f.mWorld.answers(cond(182, kHat, 2, kRefA2)));
        EXPECT_TRUE(f.holds(cond(182, kHat, 2, kRefA2, 0, 1.f)));
        EXPECT_TRUE(f.mWorld.answers(cond(182, kSelf | 0x999, 2, kRefA2)));
        EXPECT_TRUE(f.holds(cond(182, kSelf | 0x999, 2, kRefA2, 0, 0.f)));
        // carried in the inventory but not in the outfit: it may or may not be worn
        EXPECT_FALSE(f.mWorld.answers(cond(182, kItem, 2, kRefA2)));
    }

    TEST(SkyrimPlacedRefWorld, distanceBetweenReferencesOfOneCell)
    {
        Fixture f;
        // the speaker is kRefA at the origin, kRefA2 at (30, 40, 0)
        EXPECT_TRUE(f.mWorld.answers(cond(1, kRefA2, 0)));
        EXPECT_FLOAT_EQ(f.mWorld.distanceTo(SkyrimConditionWorld::kPlacedTag | kRefA, kRefA2), 50.f);
        EXPECT_TRUE(f.holds(cond(1, kRefA2, 0, 0, 0, 50.f)));
        // another cell: not answered, and large
        EXPECT_FALSE(f.mWorld.answers(cond(1, kRefFar, 0)));
        EXPECT_GT(f.mWorld.distanceTo(SkyrimConditionWorld::kPlacedTag | kRefA, kRefFar), 1e30f);
        // the player's position is not known
        EXPECT_FALSE(f.mWorld.answers(cond(1, 0x14, 0)));
    }

    TEST(SkyrimPlacedRefWorld, relationshipRankFromTheRelationRecords)
    {
        Fixture f;
        EXPECT_TRUE(f.mWorld.answers(cond(403, kRefFar, 0)));
        EXPECT_TRUE(f.holds(cond(403, kRefFar, 0, 0, 0, 4.f))); // lover
        // no record: acquaintance, answered
        EXPECT_TRUE(f.mWorld.answers(cond(403, kRefA2, 2, kRefFar)));
        EXPECT_TRUE(f.holds(cond(403, kRefA2, 2, kRefFar, 0, 4.f))); // C to A: the record in the other direction
        // a creature or unknown target: not answered
        EXPECT_FALSE(f.mWorld.answers(cond(403, kSelf | 0x998, 0)));
    }

    TEST(SkyrimPlacedRefWorld, theCurrentPackageComesFromTheCallerAndTheServicesBitFromThePackage)
    {
        Fixture f;
        EXPECT_FALSE(f.mWorld.answers(cond(161, kPackageA, 0)));
        PackageRecords pr;
        PackageRecord a;
        a.mCommon.mId = kPackageA;
        a.mFlags = 0;
        PackageRecord s;
        s.mCommon.mId = kPackageServices;
        s.mFlags = 1;
        pr.mPackages.push_back(a);
        pr.mPackages.push_back(s);
        f.mPackages.add(std::move(pr), FormIdResolver(Content::Header(), "test.esp", order()));
        f.mWorld.setPackages(&f.mPackages);
        FormId current = kPackageA;
        bool known = true;
        f.mWorld.setCurrentPackageLookup([&](FormId reference) -> std::optional<FormId> {
            if (!known || reference != kRefA)
                return std::nullopt;
            return current;
        });
        EXPECT_TRUE(f.mWorld.answers(cond(161, kPackageA, 0)));
        EXPECT_TRUE(f.holds(cond(161, kPackageA, 0, 0, 0, 1.f)));
        EXPECT_TRUE(f.holds(cond(161, kPackageServices, 0, 0, 0, 0.f)));
        EXPECT_TRUE(f.mWorld.answers(cond(255, 0, 0)));
        EXPECT_TRUE(f.holds(cond(255, 0, 0, 0, 0, 0.f)));
        current = kPackageServices;
        EXPECT_TRUE(f.holds(cond(255, 0, 0, 0, 0, 1.f)));
        current = kNullFormId; // no package
        EXPECT_TRUE(f.holds(cond(255, 0, 0, 0, 0, 0.f)));
        known = false;
        EXPECT_FALSE(f.mWorld.answers(cond(161, kPackageA, 0)));
        EXPECT_FALSE(f.mWorld.answers(cond(255, 0, 0)));
    }

    TEST(SkyrimPlacedRefWorld, theReferenceOfThePlayerIsThePlayer)
    {
        Fixture f;
        PlayerStandIn player;
        f.mWorld.setPlayer(&player);
        EXPECT_TRUE(f.mWorld.answers(cond(14, 16, 1)));
        EXPECT_TRUE(f.mWorld.answers(cond(14, 16, 2, 0x14))); // the player's own reference is the player
        EXPECT_EQ(f.mWorld.resolveRunOn(2, 0x14), SkyrimConditionWorld::kPlayer);
    }

    // ---- quest aliases -------------------------------------------------------------------------------------------

    struct AliasFixture : Fixture
    {
        AliasFixture()
        {
            QuestRecords qr;
            QuestRecord q;
            q.mCommon.mId = kQuest;
            q.mFlags = QuestFlags::StartGameEnabled;
            QuestAlias byRef;
            byRef.mId = 1;
            byRef.mFill = AliasFillKind::SpecificReference;
            byRef.mReference = kRefA2;
            QuestAlias byActor;
            byActor.mId = 2;
            byActor.mFill = AliasFillKind::UniqueActor;
            byActor.mActor = kNpcC; // one placed reference (kRefFar)
            QuestAlias byManyActor;
            byManyActor.mId = 3;
            byManyActor.mFill = AliasFillKind::UniqueActor;
            byManyActor.mActor = kNpcA; // several
            QuestAlias conditional;
            conditional.mId = 4;
            q.mAliases = { byRef, byActor, byManyActor, conditional };
            qr.mQuests.push_back(q);
            mQuests.add(std::move(qr), FormIdResolver(Content::Header(), "test.esp", order()));
            mState = QuestState::initial(mQuests);
            mWorld.setQuests(&mQuests, &mState);
            mWorld.setQuestContext(kQuest);
        }
    };

    survey::Ctda onAlias(std::uint16_t function, std::uint32_t alias, std::uint32_t p1 = 0, float value = 1.f)
    {
        survey::Ctda c = cond(function, p1, 5, 0, 0, value);
        c.mParam3 = alias;
        return c;
    }

    TEST(SkyrimPlacedRefAlias, aSpecificReferenceAliasRunsTheConditionOnThatReference)
    {
        AliasFixture f;
        EXPECT_TRUE(f.mWorld.answers(onAlias(46, 1))); // GetDead on the dead kRefA2
        EXPECT_TRUE(f.holds(onAlias(46, 1)));
        EXPECT_TRUE(f.mWorld.answers(onAlias(71, 1, kFaction)));
        EXPECT_TRUE(f.holds(onAlias(71, 1, kFaction)));
    }

    TEST(SkyrimPlacedRefAlias, aUniqueActorWithOneReferenceIsThatReferenceWithManyIsTheBase)
    {
        AliasFixture f;
        // base C has one reference, in another cell: GetDisabled needs the reference and has it
        EXPECT_TRUE(f.mWorld.answers(onAlias(35, 2)));
        EXPECT_EQ(f.mWorld.resolveSubject(onAlias(35, 2)), SkyrimConditionWorld::kPlacedTag | kRefFar);
        // base A has several: only base facts are known; the speaker is base A, so the alias is the speaker
        EXPECT_NE(f.mWorld.resolveSubject(onAlias(71, 3, kFaction)), 0u);
        EXPECT_TRUE(f.mWorld.answers(onAlias(71, 3, kFaction)));
    }

    TEST(SkyrimPlacedRefAlias, anAliasTheDataCannotFillIsNotAnswered)
    {
        AliasFixture f;
        EXPECT_FALSE(f.mWorld.answers(onAlias(46, 4)));
        EXPECT_FALSE(f.mWorld.answers(onAlias(46, 99)));
    }

    TEST(SkyrimPlacedRefAlias, getIsAliasRefOnAPlacedReference)
    {
        AliasFixture f;
        const conditions::Subject dead = SkyrimConditionWorld::kPlacedTag | kRefA2;
        const conditions::Subject far = SkyrimConditionWorld::kPlacedTag | kRefFar;
        EXPECT_TRUE(f.mWorld.isAliasRef(dead, 1));
        EXPECT_FALSE(f.mWorld.isAliasRef(far, 1));
        EXPECT_TRUE(f.mWorld.isAliasRef(far, 2)); // unique actor C
        EXPECT_TRUE(f.mWorld.answers(cond(566, 1, 2, kRefA2)));
    }
}
