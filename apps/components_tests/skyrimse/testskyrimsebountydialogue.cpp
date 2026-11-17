// Task 0230: the bounty is offered in dialogue. The MainTopic INFOs of BQ01 to BQ04 need the speaker to fill the
// Steward or Innkeeper alias, whose conditions call GetIsEditorLocAlias (fn_567). Needs: the reference's own location
// (XLCN) read, the unique-NPC lists (LCUN) as an editor-location source, fn_567 itself, the alias view around the
// evaluation of a find-matching REFERENCE alias, and the lazy fill from a conversation. Generated records; no game
// data. Written first against stubs of the new functions (the red counts are in the task log).
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/location.hpp>
#include <game_skyrimse/location_conditions.hpp>
#include <game_skyrimse/placed_refs.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/quest_alias.hpp>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kQuest = kSelf | 0x800;
    constexpr FormId kWorld = 0x0000003C;
    constexpr FormId kHold = kSelf | 0x100;
    constexpr FormId kCity = kSelf | 0x101; // inside the hold
    constexpr FormId kInn = kSelf | 0x102; // inside the city
    constexpr FormId kHold2 = kSelf | 0x103;
    constexpr FormId kOther = kSelf | 0x104; // inside the other hold
    constexpr FormId kNpc = kSelf | 0xB00; // the steward base
    constexpr FormId kNpc2 = kSelf | 0xB01;
    constexpr FormId kSpeakerRef = kSelf | 0xA01;
    constexpr FormId kOtherRef = kSelf | 0xA02;
    constexpr FormId kListedRef = kSelf | 0xA03; // listed by the inn as a special reference
    constexpr FormId kCell = kSelf | 0x150;
    constexpr FormId kRefType = kSelf | 0x300;

    Bytes operator+(Bytes a, const Bytes& b)
    {
        a.append(b);
        return a;
    }

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes ctda(std::uint16_t function, std::uint32_t p1 = 0, std::uint32_t p2 = 0)
    {
        Bytes b;
        b.u8(0).u8(0).u8(0).u8(0).f32(1.f);
        b.u16(function).u16(0).u32(p1).u32(p2).u32(0).u32(0).u32(0);
        return sub4("CTDA", b);
    }

    survey::Ctda cond567(std::uint32_t alias)
    {
        survey::Ctda c;
        c.mFunction = 567;
        c.mParam1 = alias;
        c.mCompare = 0;
        c.mComparison = 1.f;
        return c;
    }

    Bytes lctn(FormId id, FormId parent, const Bytes& special = Bytes(), const Bytes& uniqueNpcs = Bytes())
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("L" + std::to_string(id & 0xFFF))));
        if (parent != 0)
            b.append(sub4("PNAM", Bytes().u32(parent)));
        if (!special.str().empty())
            b.append(sub4("LCSR", special));
        if (!uniqueNpcs.str().empty())
            b.append(sub4("LCUN", uniqueNpcs));
        return record4("LCTN", 0u, id, b);
    }

    Bytes uniqueEntry(FormId actor, FormId ref, FormId location)
    {
        return Bytes().u32(actor).u32(ref).u32(location);
    }

    /// Hold > City > Inn, Hold2 > Other.
    Bytes locationRecords(const Bytes& uniqueInCity = Bytes(), const Bytes& uniqueInOther = Bytes())
    {
        Bytes r = lctn(kHold, 0);
        r.append(lctn(kCity, kHold, Bytes(), uniqueInCity));
        r.append(lctn(kInn, kCity, Bytes().u32(kRefType).u32(kListedRef).u32(kWorld).u16(0).u16(0)));
        r.append(lctn(kHold2, 0));
        r.append(lctn(kOther, kHold2, Bytes(), uniqueInOther));
        return r;
    }

    LocationRecords parseLocations(const Bytes& records)
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(group4(Content::fourCC("LCTN"), 0, records));
        Content::Error error;
        std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
            std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
        LocationRecords out;
        EXPECT_NE(plugin, nullptr);
        if (plugin != nullptr)
        {
            EXPECT_TRUE(parseLocationRecords(*plugin, order(), LocationLimits(), out));
        }
        return out;
    }

    PlacedEntry entry(FormId id, FormId base, FormId location = kNullFormId)
    {
        PlacedEntry e;
        e.mId = id;
        e.mBase = base;
        e.mCell = kCell;
        e.mFlags = PlacedFlags::IsActor;
        e.mLocation = location;
        return e;
    }

    // ---- XLCN on a placed reference ---------------------------------------------------------------------------------

    Bytes achr(FormId id, FormId base, const Bytes& extra)
    {
        Bytes data = sub4("NAME", Bytes().u32(base));
        data.append(extra);
        data.append(sub4("DATA", Bytes().f32(1).f32(2).f32(3).f32(0).f32(0).f32(0)));
        return record4("ACHR", 0, id, data);
    }

    PlacedRefRecords parsePlaced(const Bytes& children)
    {
        Bytes cellBlock = record4("CELL", 0, kCell, sub4("EDID", Bytes().zstring("TestCell")));
        cellBlock.append(group4(kCell, 6, children));
        Bytes all = tes4Header(1.7f, 0, "A", "B", 4, { { "Skyrim.esm", 0 } }, 24, 44);
        all.append(group4(Content::fourCC("CELL"), 0, cellBlock));
        Content::Error error;
        const auto plugin = Content::Plugin::open(
            std::make_unique<std::istringstream>(all.str(), std::ios::binary), "test.esp", error);
        PlacedRefRecords out;
        EXPECT_NE(plugin, nullptr);
        if (plugin != nullptr)
        {
            EXPECT_TRUE(parsePlacedRefs(*plugin, order(), PlacedRefLimits(), out));
        }
        return out;
    }

    TEST(SkyrimBountyDlgPlaced, theReferencesOwnLocationIsReadFromXlcn)
    {
        Bytes children = achr(kSpeakerRef, kNpc, sub4("XLCN", Bytes().u32(kCity)));
        children.append(achr(kOtherRef, kNpc2, Bytes())); // no XLCN
        PlacedRefIndex index;
        index.add(parsePlaced(children));
        index.finish();
        ASSERT_NE(index.find(kSpeakerRef), nullptr);
        EXPECT_EQ(index.find(kSpeakerRef)->mLocation, kCity);
        ASSERT_NE(index.find(kOtherRef), nullptr);
        EXPECT_EQ(index.find(kOtherRef)->mLocation, kNullFormId);
    }

    TEST(SkyrimBountyDlgPlaced, aWrongSizedXlcnIsCountedAndIgnored)
    {
        const PlacedRefRecords r = parsePlaced(achr(kSpeakerRef, kNpc, sub4("XLCN", Bytes().u16(7))));
        ASSERT_EQ(r.mRefs.size(), 1u);
        EXPECT_EQ(r.mRefs[0].mLocation, kNullFormId);
        EXPECT_EQ(r.mOddSubrecords, 1u);
    }

    TEST(SkyrimBountyDlgPlaced, aLaterPluginsRecordReplacesTheLocationWholly)
    {
        PlacedRefIndex index;
        index.add(parsePlaced(achr(kSpeakerRef, kNpc, sub4("XLCN", Bytes().u32(kCity)))));
        index.add(parsePlaced(achr(kSpeakerRef, kNpc, sub4("XLCN", Bytes().u32(kOther)))));
        index.add(parsePlaced(achr(kOtherRef, kNpc2, sub4("XLCN", Bytes().u32(kCity)))));
        index.add(parsePlaced(achr(kOtherRef, kNpc2, Bytes()))); // an override that has none: none
        index.finish();
        EXPECT_EQ(index.find(kSpeakerRef)->mLocation, kOther);
        EXPECT_EQ(index.find(kOtherRef)->mLocation, kNullFormId);
    }

    // ---- LCUN -------------------------------------------------------------------------------------------------------

    TEST(SkyrimBountyDlgLocations, aUniqueNpcListGivesTheActorsLocationByReferenceThenByBase)
    {
        LocationIndex index;
        index.add(parseLocations(locationRecords(uniqueEntry(kNpc, kSpeakerRef, kInn))));
        EXPECT_EQ(index.uniqueNpcLocation(kNpc, kSpeakerRef), kInn); // the location the entry names
        EXPECT_EQ(index.uniqueNpcLocation(kNpc, kNullFormId), kInn); // by base only
        EXPECT_EQ(index.uniqueNpcLocation(kNpc, kOtherRef), kInn); // another reference of the same base: by base
        EXPECT_EQ(index.uniqueNpcLocation(kNpc2, kSpeakerRef), kInn); // the reference matches, the base does not
        EXPECT_EQ(index.uniqueNpcLocation(kNpc2, kOtherRef), kNullFormId);
        EXPECT_EQ(index.uniqueNpcLocation(kNullFormId, kNullFormId), kNullFormId);
    }

    TEST(SkyrimBountyDlgLocations, severalEntriesTakeTheDeepestLocationAndTheLowerIdOnATie)
    {
        // the actor is listed in two lists: the deeper location (the inn, depth 3) beats the city (depth 2)
        Bytes both = uniqueEntry(kNpc, kSpeakerRef, kCity) + uniqueEntry(kNpc, kSpeakerRef, kInn);
        LocationIndex index;
        index.add(parseLocations(locationRecords(both)));
        EXPECT_EQ(index.uniqueNpcLocation(kNpc, kSpeakerRef), kInn);
        // two at the same depth: the lower form id
        LocationIndex tie;
        tie.add(parseLocations(
            locationRecords(uniqueEntry(kNpc2, kOtherRef, kHold2) + uniqueEntry(kNpc2, kOtherRef, kHold))));
        EXPECT_EQ(tie.uniqueNpcLocation(kNpc2, kOtherRef), kHold);
    }

    // ---- fn_567 -----------------------------------------------------------------------------------------------------

    struct CondRig
    {
        explicit CondRig(const Bytes& locations = locationRecords(), std::vector<PlacedEntry> refs = {})
        {
            mIndex.add(parseLocations(locations));
            mWorld.setLocations(&mIndex);
            PlacedRefRecords r;
            r.mRefs = std::move(refs);
            mRefs.add(std::move(r));
            mRefs.finish();
            mWorld.setPlacedRefs(&mRefs);
            mTable = aliasLocationExtraFunctions();
            mEvaluator.setExtraFunctions(mTable);
            mHoldOf = [](std::uint32_t alias) { return alias == 4 ? kHold : kNullFormId; };
            mWorld.setAliasView([this](std::uint32_t alias) { return mHoldOf(alias); });
        }
        void speak(FormId base, FormId reference)
        {
            DialogueSpeaker s;
            s.mBase = base;
            s.mReference = reference;
            mWorld.setSpeaker(s);
        }
        bool holds(std::uint32_t alias = 4)
        {
            const survey::Ctda c = cond567(alias);
            return mEvaluator.evaluate(std::span<const survey::Ctda>(&c, 1), mWorld);
        }
        ActorIndex mActors;
        DialogueIndex mDialogue;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };
        LocationIndex mIndex;
        PlacedRefIndex mRefs;
        conditions::Evaluator mEvaluator;
        std::vector<conditions::ExtraFunction> mTable;
        std::function<FormId(std::uint32_t)> mHoldOf;
    };

    TEST(SkyrimBountyDlgCond, aSpeakerWhoseOwnLocationIsInsideTheAliasLocationHasIt)
    {
        CondRig rig(locationRecords(), { entry(kSpeakerRef, kNpc, kInn) });
        rig.speak(kNpc, kSpeakerRef);
        EXPECT_TRUE(rig.holds()); // the inn is inside the city, which is inside the hold
        rig.mHoldOf = [](std::uint32_t) { return kInn; }; // the alias holds the inn itself
        EXPECT_TRUE(rig.holds());
        rig.mHoldOf = [](std::uint32_t) { return kCity; };
        EXPECT_TRUE(rig.holds());
    }

    TEST(SkyrimBountyDlgCond, aLocationInAnotherHoldOrAnEmptyAliasIsRefused)
    {
        CondRig rig(locationRecords(), { entry(kSpeakerRef, kNpc, kInn) });
        rig.speak(kNpc, kSpeakerRef);
        rig.mHoldOf = [](std::uint32_t) { return kHold2; };
        EXPECT_FALSE(rig.holds());
        rig.mHoldOf = [](std::uint32_t) { return kOther; }; // inside the other hold
        EXPECT_FALSE(rig.holds());
        rig.mHoldOf = [](std::uint32_t) { return kNullFormId; }; // the alias is not filled
        EXPECT_FALSE(rig.holds());
    }

    TEST(SkyrimBountyDlgCond, theLocationOfTheReferenceBeatsTheUniqueNpcListWhichBeatsTheLists)
    {
        // XLCN (the inn) first: the list entry puts the same actor in the other hold
        CondRig rig(
            locationRecords(Bytes(), uniqueEntry(kNpc, kSpeakerRef, kOther)), { entry(kSpeakerRef, kNpc, kInn) });
        rig.speak(kNpc, kSpeakerRef);
        EXPECT_TRUE(rig.holds());
        rig.mHoldOf = [](std::uint32_t) { return kHold2; };
        EXPECT_FALSE(rig.holds());
        // without an XLCN the unique-NPC entry decides
        CondRig noXlcn(locationRecords(Bytes(), uniqueEntry(kNpc, kSpeakerRef, kOther)), { entry(kSpeakerRef, kNpc) });
        noXlcn.speak(kNpc, kSpeakerRef);
        noXlcn.mHoldOf = [](std::uint32_t) { return kHold2; };
        EXPECT_TRUE(noXlcn.holds());
        noXlcn.mHoldOf = [](std::uint32_t) { return kHold; };
        EXPECT_FALSE(noXlcn.holds());
    }

    TEST(SkyrimBountyDlgCond, withNoXlcnAndNoUniqueEntryTheListsThenTheCellDecide)
    {
        CondRig lists(locationRecords(), { entry(kListedRef, kNpc) }); // listed by the inn as a special reference
        lists.speak(kNpc, kListedRef);
        EXPECT_TRUE(lists.holds());
        CondRig cell(locationRecords(), { entry(kSpeakerRef, kNpc) });
        cell.speak(kNpc, kSpeakerRef);
        EXPECT_FALSE(cell.holds()); // nothing knows where it is
        cell.mWorld.setReferenceLocation([](FormId) { return kCity; }); // the location of its cell
        EXPECT_TRUE(cell.holds());
    }

    TEST(SkyrimBountyDlgCond, anActorKnownOnlyByItsBaseUsesTheUniqueNpcList)
    {
        CondRig rig(locationRecords(uniqueEntry(kNpc, kSpeakerRef, kInn)), {});
        rig.speak(kNpc, kNullFormId);
        EXPECT_TRUE(rig.holds());
        rig.speak(kNpc2, kNullFormId);
        EXPECT_FALSE(rig.holds());
    }

    TEST(SkyrimBountyDlgCond, withoutTheExtraTableTheFunctionStillFailsClosed)
    {
        CondRig rig(locationRecords(), { entry(kSpeakerRef, kNpc, kInn) });
        rig.speak(kNpc, kSpeakerRef);
        conditions::Evaluator plain;
        const survey::Ctda c = cond567(4);
        EXPECT_FALSE(plain.evaluate(std::span<const survey::Ctda>(&c, 1), rig.mWorld));
        EXPECT_EQ(plain.mListsFailedClosed, 1u);
        EXPECT_TRUE(rig.holds()); // and the table answers it
        EXPECT_EQ(rig.mEvaluator.mListsFailedClosed, 0u);
    }

    // ---- the filler -------------------------------------------------------------------------------------------------

    Bytes questRecord(const Bytes& aliases)
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("BQT")));
        b.append(sub4("DNAM", Bytes().u16(0).u8(30).u8(0).u32(0).u32(6)));
        b.append(sub4("NEXT", Bytes()));
        b.append(sub4("INDX", Bytes().u16(10).u8(0).u8(0)));
        b.append(sub4("QSDT", Bytes().u8(0)));
        b.append(sub4("ANAM", Bytes().u32(40)));
        b.append(aliases);
        return record4("QUST", 0u, kQuest, b);
    }

    Bytes alias(bool location, std::uint32_t id, const std::string& name, std::uint32_t flags, const Bytes& fill,
        const Bytes& conditions = Bytes())
    {
        Bytes b;
        b.append(sub4(location ? "ALLS" : "ALST", Bytes().u32(id)));
        b.append(sub4("ALID", Bytes().zstring(name)));
        b.append(sub4("FNAM", Bytes().u32(flags)));
        b.append(fill);
        b.append(conditions);
        b.append(sub4("ALED", Bytes()));
        return b;
    }

    /// The BQ shape: a Hold location alias (specific here), a Steward (alias 13) and an Innkeeper (alias 10), both find
    /// matching references whose conditions read the Hold through fn_567.
    Bytes bqAliases()
    {
        return alias(true, 4, "Hold", 0, sub4("ALFL", Bytes().u32(kHold)))
            + alias(false, 13, "Steward", 0, Bytes(), ctda(567, 4))
            + alias(false, 10, "Innkeeper", 0, Bytes(), ctda(567, 4));
    }

    struct FillRig
    {
        explicit FillRig(const Bytes& aliases, std::vector<PlacedEntry> refs)
        {
            Bytes plugin = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            plugin.append(group4(Content::fourCC("QUST"), 0, questRecord(aliases)));
            plugin.append(group4(Content::fourCC("LCTN"), 0, locationRecords()));
            Content::Error error;
            std::unique_ptr<Content::Plugin> p = Content::Plugin::open(
                std::make_unique<std::istringstream>(plugin.str(), std::ios::binary), "test.esp", error);
            EXPECT_NE(p, nullptr);
            if (p == nullptr)
                return;
            QuestRecords qr;
            EXPECT_TRUE(parseQuestRecords(*p, order(), QuestLimits(), qr));
            mQuests.add(std::move(qr), FormIdResolver(p->header(), "test.esp", order()));
            LocationRecords lr;
            EXPECT_TRUE(parseLocationRecords(*p, order(), LocationLimits(), lr));
            mLocations.add(std::move(lr));
            mState = QuestState::initial(mQuests);
            PlacedRefRecords r;
            r.mRefs = std::move(refs);
            mRefs.add(std::move(r));
            mRefs.finish();
            // the stand-in for the alias conditions: the speaker reference passes, and only while the Hold is known
            mFiller = std::make_unique<QuestAliasFiller>(
                mQuests, mRefs, [this](const QuestRecord&, const QuestAlias& a, FormId reference, FormId) {
                    mAsked.push_back({ a.mId, reference });
                    if (a.mId == 3) // the marker alias (the test that needs it): its own reference
                        return reference == kListedRef;
                    mHoldWhenAsked.push_back(mView ? mView(4) : kNullFormId);
                    return reference == kSpeakerRef && (!mNeedHold || (mView && mView(4) == kHold));
                });
            QuestAliasFiller::Environment env;
            env.mLocations = &mLocations;
            env.mSetAliasView = [this](const std::function<FormId(std::uint32_t)>& view) { mView = view; };
            mFiller->setEnvironment(std::move(env));
        }

        std::vector<FormId> mHoldWhenAsked; ///< what alias 4 held each time the matcher was asked

        FormId filled(std::uint32_t aliasId) const
        {
            const AliasFill f = mState.aliasFill(kQuest, aliasId);
            return f.mKnown ? f.mReference : kNullFormId;
        }
        void fill(std::uint32_t aliasId, FormId reference)
        {
            AliasFill f;
            f.mKnown = true;
            f.mReference = reference;
            mState.setAliasFill(kQuest, aliasId, f);
        }

        QuestIndex mQuests;
        LocationIndex mLocations;
        QuestState mState;
        PlacedRefIndex mRefs;
        std::unique_ptr<QuestAliasFiller> mFiller;
        std::function<FormId(std::uint32_t)> mView;
        std::vector<std::pair<std::uint32_t, FormId>> mAsked;
        bool mNeedHold = false;
    };

    TEST(SkyrimBountyDlgFill, aFindMatchingReferenceSeesTheAliasesFilledEarlierInTheSamePlan)
    {
        // the Hold is filled in the same plan as the Steward: its conditions (fn_567 on the Hold) must see it
        FillRig rig(bqAliases(), { entry(kSpeakerRef, kNpc, kCity) });
        rig.mNeedHold = true;
        rig.mState.start(kQuest);
        rig.mFiller->fillQuest(kQuest, rig.mState, true);
        EXPECT_EQ(rig.filled(4), kHold);
        EXPECT_EQ(rig.filled(13), kSpeakerRef);
        EXPECT_FALSE(static_cast<bool>(rig.mView)); // and the view is taken away afterwards
    }

    TEST(SkyrimBountyDlgFill, aFindMatchingReferenceWaitsForTheAliasItsConditionNamesWhenThatOneWaitsToo)
    {
        // the order of the quest is not a dependency order: the Steward (13) comes before the marker (3) that the Hold
        // (4) is found from, so in the first round the Hold is not filled yet and the Steward must wait, not fail
        const Bytes aliases = alias(true, 0, "Location", 0, sub4("ALFL", Bytes().u32(kInn)))
            + alias(true, 4, "Hold", 0x08, sub4("ALFA", Bytes().u32(3))) // reuse: the same location as alias 0
            + alias(false, 13, "Steward", 0, Bytes(), ctda(567, 4))
            + alias(false, 3, "Marker", 0, sub4("ALFA", Bytes().u32(0)) + sub4("ALRT", Bytes().u32(kRefType)));
        FillRig rig(aliases, { entry(kSpeakerRef, kNpc, kInn), entry(kListedRef, kNpc2) });
        rig.mState.start(kQuest);
        rig.mFiller->fillQuest(kQuest, rig.mState, true);
        EXPECT_EQ(rig.filled(3), kListedRef);
        EXPECT_EQ(rig.filled(4), kInn);
        EXPECT_NE(rig.filled(13), kNullFormId);
        for (const FormId seen : rig.mHoldWhenAsked)
            EXPECT_EQ(seen, kInn); // never asked before the Hold was known
    }

    TEST(SkyrimBountyDlgFill, aReferenceAlreadyHeldByAnotherAliasIsNotTakenAgain)
    {
        FillRig rig(bqAliases(), { entry(kSpeakerRef, kNpc, kCity) });
        rig.mState.start(kQuest);
        rig.mFiller->fillQuest(kQuest, rig.mState, true);
        EXPECT_EQ(rig.filled(13), kSpeakerRef); // the first alias in the order of the quest
        EXPECT_EQ(rig.filled(10), kNullFormId); // the only candidate is taken
    }

    TEST(SkyrimBountyDlgSpeaker, theSpeakerFillsAnEmptyAliasWhoseConditionsCallFn567)
    {
        FillRig rig(bqAliases(), { entry(kSpeakerRef, kNpc, kCity) });
        rig.mState.start(kQuest);
        rig.fill(4, kHold);
        rig.mNeedHold = true;
        EXPECT_EQ(rig.mFiller->fillForSpeaker(rig.mState, kSpeakerRef, kNpc), 1u); // one alias: the first in order
        EXPECT_EQ(rig.filled(13), kSpeakerRef);
        EXPECT_EQ(rig.filled(10), kNullFormId); // a reference fills one alias of a quest
        EXPECT_FALSE(static_cast<bool>(rig.mView));
    }

    TEST(SkyrimBountyDlgSpeaker, aSpeakerWhoseConditionsFailFillsNothing)
    {
        FillRig rig(bqAliases(), { entry(kSpeakerRef, kNpc, kCity), entry(kOtherRef, kNpc2, kCity) });
        rig.mState.start(kQuest);
        EXPECT_EQ(rig.mFiller->fillForSpeaker(rig.mState, kOtherRef, kNpc2), 0u);
        EXPECT_EQ(rig.filled(13), kNullFormId);
        EXPECT_EQ(rig.filled(10), kNullFormId);
        EXPECT_FALSE(rig.mAsked.empty()); // it was asked, and refused
    }

    TEST(SkyrimBountyDlgSpeaker, aQuestThatIsNotRunningIsLeftAlone)
    {
        FillRig rig(bqAliases(), { entry(kSpeakerRef, kNpc, kCity) });
        EXPECT_EQ(rig.mFiller->fillForSpeaker(rig.mState, kSpeakerRef, kNpc), 0u);
        EXPECT_EQ(rig.filled(13), kNullFormId);
        EXPECT_TRUE(rig.mAsked.empty()); // not even asked
    }

    TEST(SkyrimBountyDlgSpeaker, anAliasWhoseConditionsDoNotCallFn567IsNotFilledThisWay)
    {
        FillRig rig(alias(false, 10, "Innkeeper", 0, Bytes(), ctda(71, 0x50920)), { entry(kSpeakerRef, kNpc, kCity) });
        rig.mState.start(kQuest);
        EXPECT_EQ(rig.mFiller->fillForSpeaker(rig.mState, kSpeakerRef, kNpc), 0u);
        EXPECT_EQ(rig.filled(10), kNullFormId);
        EXPECT_TRUE(rig.mAsked.empty());
    }

    TEST(SkyrimBountyDlgSpeaker, anAliasThatIsFilledAlreadyKeepsItsReference)
    {
        FillRig rig(bqAliases(), { entry(kSpeakerRef, kNpc, kCity), entry(kOtherRef, kNpc2, kCity) });
        rig.mState.start(kQuest);
        rig.fill(13, kOtherRef);
        EXPECT_EQ(rig.mFiller->fillForSpeaker(rig.mState, kSpeakerRef, kNpc), 1u); // the Innkeeper, not the Steward
        EXPECT_EQ(rig.filled(13), kOtherRef);
        EXPECT_EQ(rig.filled(10), kSpeakerRef);
    }

    TEST(SkyrimBountyDlgSpeaker, anAliasThatAllowsReuseMayTakeAReferenceAnotherAliasHolds)
    {
        const Bytes aliases = alias(false, 13, "Steward", 0x08, Bytes(), ctda(567, 4))
            + alias(false, 10, "Innkeeper", 0x08, Bytes(), ctda(567, 4));
        FillRig rig(aliases, { entry(kSpeakerRef, kNpc, kCity) });
        rig.mState.start(kQuest);
        EXPECT_EQ(rig.mFiller->fillForSpeaker(rig.mState, kSpeakerRef, kNpc), 2u);
        EXPECT_EQ(rig.filled(13), kSpeakerRef);
        EXPECT_EQ(rig.filled(10), kSpeakerRef);
    }

    TEST(SkyrimBountyDlgSpeaker, aSpeakerWithoutAReferenceFillsNothing)
    {
        FillRig rig(bqAliases(), { entry(kSpeakerRef, kNpc, kCity) });
        rig.mState.start(kQuest);
        EXPECT_EQ(rig.mFiller->fillForSpeaker(rig.mState, kNullFormId, kNpc), 0u);
        EXPECT_EQ(rig.filled(13), kNullFormId);
    }
}
