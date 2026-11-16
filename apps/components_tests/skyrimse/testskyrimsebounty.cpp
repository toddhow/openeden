// Task 0226: the bounty quests (BQ01 to BQ04) start. Their three location aliases need: the alias keyword (KNAM) read and
// used, a location alias that searches by a REFERENCE alias (the Hold: the nearest ancestor of the reference's editor
// location with the keyword), HasSameEditorLocAsRefAlias (fn_181) on a candidate location, and the filler waiting for the
// alias a condition reads. Generated records; no game data. Written first: against the code before the task these tests do
// not compile (no `mKeyword`, no `LocationIndex::editorLocationOf`, no `Environment::mSetAliasView`); mutations after.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/location.hpp>
#include <game_skyrimse/location_conditions.hpp>
#include <game_skyrimse/placed_refs.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/quest_alias.hpp>
#include <game_skyrimse/story.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
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
    constexpr FormId kHold = kSelf | 0x100; // a hold (keyword Hold)
    constexpr FormId kCity = kSelf | 0x101; // inside the hold, lists the centre marker
    constexpr FormId kDungeon = kSelf | 0x102; // inside the hold, lists the boss
    constexpr FormId kHold2 = kSelf | 0x103;
    constexpr FormId kDungeon2 = kSelf | 0x104; // inside the other hold
    constexpr FormId kWilds = kSelf | 0x105; // no hold above it
    constexpr FormId kRoom = kSelf | 0x106; // inside the dungeon
    constexpr FormId kKwHold = kSelf | 0x600;
    constexpr FormId kKwDungeon = kSelf | 0x601;
    constexpr FormId kKwCity = kSelf | 0x602;
    constexpr FormId kTypeMarker = kSelf | 0x300;
    constexpr FormId kTypeBoss = kSelf | 0x301;
    constexpr FormId kMarker = kSelf | 0xA01;
    constexpr FormId kBoss = kSelf | 0xA02;
    constexpr FormId kBoss2 = kSelf | 0xA03;
    constexpr FormId kLoose = kSelf | 0xA04; // listed by no location
    constexpr FormId kCell = kSelf | 0x150;
    constexpr FormId kBase = kSelf | 0xB00;

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

    Bytes questRecord(FormId id, const Bytes& aliases)
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("BQ" + std::to_string(id & 0xFFF))));
        b.append(sub4("DNAM", Bytes().u16(0).u8(30).u8(0).u32(0).u32(6)));
        b.append(sub4("NEXT", Bytes()));
        for (std::uint16_t stage : { 10, 20 })
        {
            b.append(sub4("INDX", Bytes().u16(stage).u8(0).u8(0)));
            b.append(sub4("QSDT", Bytes().u8(0)));
        }
        b.append(sub4("ANAM", Bytes().u32(40)));
        b.append(aliases);
        return record4("QUST", 0u, id, b);
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

    Bytes fromEvent(char kind, char which)
    {
        Bytes b;
        b.append(sub4("ALFE", Bytes()));
        b.append(sub4("ALFD", Bytes().u32(static_cast<std::uint32_t>(static_cast<unsigned char>(kind))
                | (static_cast<std::uint32_t>(static_cast<unsigned char>(which)) << 8))));
        return b;
    }

    Bytes inAlias(std::uint32_t target, FormId refType = 0, FormId keyword = 0)
    {
        Bytes b;
        b.append(sub4("ALFA", Bytes().u32(target)));
        if (refType != 0)
            b.append(sub4("ALRT", Bytes().u32(refType)));
        if (keyword != 0)
            b.append(sub4("KNAM", Bytes().u32(keyword)));
        return b;
    }

    Bytes special(FormId type, FormId ref)
    {
        return Bytes().u32(type).u32(ref).u32(kWorld).u16(0).u16(0);
    }

    Bytes lctn(FormId id, FormId parent, const std::vector<FormId>& keywords, const Bytes& specialRefs = Bytes(),
        const Bytes& persistent = Bytes())
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("L" + std::to_string(id & 0xFFF))));
        if (parent != 0)
            b.append(sub4("PNAM", Bytes().u32(parent)));
        if (!keywords.empty())
        {
            b.append(sub4("KSIZ", Bytes().u32(static_cast<std::uint32_t>(keywords.size()))));
            Bytes kw;
            for (const FormId k : keywords)
                kw.u32(k);
            b.append(sub4("KWDA", kw));
        }
        if (!specialRefs.str().empty())
            b.append(sub4("LCSR", specialRefs));
        if (!persistent.str().empty())
            b.append(sub4("LCPR", persistent));
        return record4("LCTN", 0u, id, b);
    }

    /// Hold > City (marker), Hold > Dungeon (boss) > Room, Hold2 > Dungeon2 (boss2), Wilds (no hold).
    Bytes world()
    {
        Bytes r = lctn(kHold, 0, { kKwHold });
        r.append(lctn(kCity, kHold, { kKwCity }, special(kTypeMarker, kMarker)));
        r.append(lctn(kDungeon, kHold, { kKwDungeon }, special(kTypeBoss, kBoss)));
        r.append(lctn(kHold2, 0, { kKwHold }));
        r.append(lctn(kDungeon2, kHold2, { kKwDungeon }, special(kTypeBoss, kBoss2)));
        r.append(lctn(kWilds, 0, { kKwDungeon }));
        r.append(lctn(kRoom, kDungeon, {}));
        return r;
    }

    PlacedEntry placed(FormId id, FormId base = kBase, std::uint8_t flags = 0)
    {
        PlacedEntry r;
        r.mId = id;
        r.mBase = base;
        r.mCell = kCell;
        r.mFlags = flags;
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

    // ---- the keyword is read ----------------------------------------------------------------------------------------

    struct QuestOnly
    {
        explicit QuestOnly(const Bytes& aliases)
        {
            Bytes plugin = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            plugin.append(group4(Content::fourCC("QUST"), 0, questRecord(kQuest, aliases)));
            Content::Error error;
            std::unique_ptr<Content::Plugin> p = Content::Plugin::open(
                std::make_unique<std::istringstream>(plugin.str(), std::ios::binary), "test.esp", error);
            EXPECT_NE(p, nullptr);
            if (p == nullptr)
                return;
            QuestRecords qr;
            EXPECT_TRUE(parseQuestRecords(*p, order(), QuestLimits(), qr));
            mOdd = qr.mOddSubrecords;
            mQuests.add(std::move(qr), FormIdResolver(p->header(), "test.esp", order()));
        }
        QuestIndex mQuests;
        std::uint64_t mOdd = 0;
    };

    TEST(SkyrimBountyParse, aLocationAliasKeywordIsReadAndResolved)
    {
        const Bytes aliases = alias(false, 3, "Marker", 0, inAlias(0, kTypeMarker))
            + alias(true, 4, "Hold", 0x300, inAlias(3, 0, kKwHold)) + alias(true, 5, "Plain", 0, Bytes());
        QuestOnly q(aliases);
        const QuestRecord* quest = q.mQuests.quest(kQuest);
        ASSERT_NE(quest, nullptr);
        ASSERT_NE(quest->alias(4), nullptr);
        EXPECT_EQ(quest->alias(4)->mKeyword, kKwHold);
        EXPECT_EQ(quest->alias(4)->mOtherAlias, 3u);
        EXPECT_EQ(quest->alias(4)->mFill, AliasFillKind::InLocationAlias);
        EXPECT_EQ(quest->alias(3)->mKeyword, kNullFormId);
        EXPECT_EQ(quest->alias(5)->mKeyword, kNullFormId);
        EXPECT_EQ(q.mOdd, 0u);
    }

    // ---- the location tree ------------------------------------------------------------------------------------------

    struct Tree
    {
        Tree() { mIndex.add(parseLocations(world())); }
        LocationIndex mIndex;
    };

    TEST(SkyrimBountyLocations, theAncestorWithAKeywordIsTheLocationItselfOrTheNearestParentThatHasIt)
    {
        Tree t;
        EXPECT_EQ(t.mIndex.ancestorWithKeyword(kHold, kKwHold), kHold); // itself
        EXPECT_EQ(t.mIndex.ancestorWithKeyword(kCity, kKwHold), kHold); // the parent
        EXPECT_EQ(t.mIndex.ancestorWithKeyword(kRoom, kKwHold), kHold); // two steps
        EXPECT_EQ(t.mIndex.ancestorWithKeyword(kRoom, kKwDungeon), kDungeon); // nearest, not the root
        EXPECT_EQ(t.mIndex.ancestorWithKeyword(kWilds, kKwHold), kNullFormId);
        EXPECT_EQ(t.mIndex.ancestorWithKeyword(kCity, kNullFormId), kNullFormId);
        EXPECT_EQ(t.mIndex.ancestorWithKeyword(kSelf | 0x999, kKwHold), kNullFormId); // not a location
        EXPECT_EQ(t.mIndex.ancestorWithKeyword(kNullFormId, kKwHold), kNullFormId);
    }

    TEST(SkyrimBountyLocations, aCycleInTheParentChainEnds)
    {
        Bytes r = lctn(kHold, kCity, {});
        r.append(lctn(kCity, kHold, {}));
        LocationIndex index;
        index.add(parseLocations(r));
        EXPECT_EQ(index.ancestorWithKeyword(kHold, kKwHold), kNullFormId);
    }

    TEST(SkyrimBountyLocations, theDescendantWithAKeywordIsTheNearestOneBelowAndNeverTheLocationItself)
    {
        Tree t;
        EXPECT_EQ(t.mIndex.descendantWithKeyword(kHold, kKwDungeon), kDungeon); // one step
        EXPECT_EQ(t.mIndex.descendantWithKeyword(kHold2, kKwDungeon), kDungeon2);
        EXPECT_EQ(t.mIndex.descendantWithKeyword(kDungeon, kKwDungeon), kNullFormId); // not itself
        EXPECT_EQ(t.mIndex.descendantWithKeyword(kHold, kKwHold), kNullFormId);
        EXPECT_EQ(t.mIndex.descendantWithKeyword(kWilds, kKwDungeon), kNullFormId);
        EXPECT_EQ(t.mIndex.descendantWithKeyword(kHold, kNullFormId), kNullFormId);
    }

    TEST(SkyrimBountyLocations, theEditorLocationOfAReferenceIsTheLocationThatListsIt)
    {
        Tree t;
        EXPECT_EQ(t.mIndex.editorLocationOf(kMarker), kCity);
        EXPECT_EQ(t.mIndex.editorLocationOf(kBoss), kDungeon);
        EXPECT_EQ(t.mIndex.editorLocationOf(kBoss2), kDungeon2);
        EXPECT_EQ(t.mIndex.editorLocationOf(kLoose), kNullFormId);
        EXPECT_EQ(t.mIndex.editorLocationOf(kNullFormId), kNullFormId);
    }

    TEST(SkyrimBountyLocations, aPersistentReferenceCountsAndTheDeepestLocationWinsATieGoesToTheLowerId)
    {
        // kLoose is persistent in the hold and in the room (deeper, so the room wins); kBoss is listed by the dungeon and, as
        // persistent, by the city (same depth, and the city has the lower id, so the city wins the tie)
        Bytes persistentLoose = Bytes().u32(kLoose).u32(kWorld).u16(0).u16(0);
        Bytes persistentBoss = Bytes().u32(kBoss).u32(kWorld).u16(0).u16(0);
        Bytes r = lctn(kHold, 0, { kKwHold }, Bytes(), persistentLoose);
        r.append(lctn(kCity, kHold, { kKwCity }, Bytes(), persistentBoss));
        r.append(lctn(kDungeon, kHold, { kKwDungeon }, special(kTypeBoss, kBoss)));
        r.append(lctn(kRoom, kDungeon, {}, Bytes(), persistentLoose));
        LocationIndex index;
        index.add(parseLocations(r));
        EXPECT_EQ(index.editorLocationOf(kLoose), kRoom);
        EXPECT_EQ(index.editorLocationOf(kBoss), kCity); // 0x101 < 0x102
    }

    TEST(SkyrimBountyLocations, theReferenceTableFollowsLaterPlugins)
    {
        Tree t;
        EXPECT_EQ(t.mIndex.editorLocationOf(kLoose), kNullFormId); // builds the table
        t.mIndex.add(parseLocations(lctn(kWilds, 0, { kKwDungeon }, special(kTypeBoss, kLoose))));
        EXPECT_EQ(t.mIndex.editorLocationOf(kLoose), kWilds);
    }

    // ---- fn_181 -----------------------------------------------------------------------------------------------------

    survey::Ctda cond181(std::uint32_t alias, FormId keyword)
    {
        survey::Ctda c;
        c.mFunction = 181;
        c.mParam1 = alias;
        c.mParam2 = keyword;
        c.mCompare = 0;
        c.mComparison = 1.f;
        return c;
    }

    struct CondRig
    {
        CondRig()
        {
            mIndex.add(parseLocations(world()));
            mWorld.setLocations(&mIndex);
            mTable = locationExtraFunctions();
            mTable.insert(mTable.end(), aliasLocationExtraFunctions().begin(), aliasLocationExtraFunctions().end());
            mEvaluator.setExtraFunctions(mTable);
            mWorld.setAliasView([](std::uint32_t alias) { return alias == 3 ? kMarker : kNullFormId; });
        }
        bool holds(const survey::Ctda& c) { return mEvaluator.evaluate(std::span<const survey::Ctda>(&c, 1), mWorld); }
        ActorIndex mActors;
        DialogueIndex mDialogue;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };
        LocationIndex mIndex;
        conditions::Evaluator mEvaluator;
        std::vector<conditions::ExtraFunction> mTable;
    };

    TEST(SkyrimBountyCond, aCandidateInTheSameHoldAsTheAliasReferenceHasTheSameEditorLocation)
    {
        CondRig rig;
        rig.mWorld.setSubjectLocation(kDungeon); // the marker is in the city of the same hold
        EXPECT_TRUE(rig.holds(cond181(3, kKwHold)));
        rig.mWorld.setSubjectLocation(kRoom); // a room inside the dungeon: its nearest hold is the same
        EXPECT_TRUE(rig.holds(cond181(3, kKwHold)));
        rig.mWorld.setSubjectLocation(kHold); // the hold itself
        EXPECT_TRUE(rig.holds(cond181(3, kKwHold)));
    }

    TEST(SkyrimBountyCond, aCandidateInAnotherHoldOrInNoneIsRefused)
    {
        CondRig rig;
        rig.mWorld.setSubjectLocation(kDungeon2);
        EXPECT_FALSE(rig.holds(cond181(3, kKwHold)));
        rig.mWorld.setSubjectLocation(kWilds); // no hold above it
        EXPECT_FALSE(rig.holds(cond181(3, kKwHold)));
        rig.mWorld.setSubjectLocation(kNullFormId); // no candidate at all
        EXPECT_FALSE(rig.holds(cond181(3, kKwHold)));
    }

    TEST(SkyrimBountyCond, theKeywordDecidesWhichAncestorIsMeant)
    {
        CondRig rig;
        // the dungeon and the city sit in the same hold but are different locations of the Dungeon and the City keyword
        rig.mWorld.setSubjectLocation(kDungeon);
        EXPECT_FALSE(rig.holds(cond181(3, kKwDungeon))); // the marker's city has no dungeon above it
        EXPECT_FALSE(rig.holds(cond181(3, kKwCity))); // the candidate has no city above it
        rig.mWorld.setSubjectLocation(kCity);
        EXPECT_TRUE(rig.holds(cond181(3, kKwCity))); // both are the city
    }

    TEST(SkyrimBountyCond, anAliasWithNoReferenceOrNoKnownLocationIsFalse)
    {
        CondRig rig;
        rig.mWorld.setSubjectLocation(kDungeon);
        EXPECT_FALSE(rig.holds(cond181(9, kKwHold))); // alias 9 holds nothing
        rig.mWorld.setAliasView([](std::uint32_t) { return kLoose; }); // a reference no location lists
        EXPECT_FALSE(rig.holds(cond181(3, kKwHold)));
        rig.mWorld.setReferenceLocation([](FormId) { return kCity; }); // ... until the fallback knows its cell's location
        EXPECT_TRUE(rig.holds(cond181(3, kKwHold)));
    }

    TEST(SkyrimBountyCond, withoutTheExtraTableTheFunctionStillFailsClosed)
    {
        CondRig rig;
        conditions::Evaluator plain;
        rig.mWorld.setSubjectLocation(kDungeon);
        const survey::Ctda c = cond181(3, kKwHold);
        EXPECT_FALSE(plain.evaluate(std::span<const survey::Ctda>(&c, 1), rig.mWorld));
        EXPECT_EQ(plain.mListsFailedClosed, 1u);
    }

    // ---- the filler -------------------------------------------------------------------------------------------------

    struct FillRig
    {
        explicit FillRig(const Bytes& aliases, std::vector<PlacedEntry> refs = { placed(kMarker), placed(kBoss), placed(kBoss2) })
        {
            Bytes plugin = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            plugin.append(group4(Content::fourCC("QUST"), 0, questRecord(kQuest, aliases)));
            plugin.append(group4(Content::fourCC("LCTN"), 0, world()));
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
            mFiller = std::make_unique<QuestAliasFiller>(mQuests, mRefs,
                [](const QuestRecord&, const QuestAlias&, FormId, FormId) { return true; });
            QuestAliasFiller::Environment env;
            env.mLocations = &mLocations;
            env.mLocationMatcher = [this](const QuestRecord&, const QuestAlias& a, FormId location) {
                mAskedLocations.push_back({ a.mId, location });
                if (mView)
                    mViewDuring.push_back(mView(3));
                // the stub stands for the conditions of the BountyLocation (alias 6) only; the other aliases take anything
                return a.mId != 6 || mAcceptLocation.empty() || mAcceptLocation.count(location) != 0;
            };
            env.mSetAliasView = [this](const std::function<FormId(std::uint32_t)>& view) {
                mView = view;
                mViewCalls.push_back(static_cast<bool>(view));
            };
            if (mReferenceLocation)
                env.mReferenceLocation = mReferenceLocation;
            mFiller->setEnvironment(std::move(env));
        }

        FormId filled(std::uint32_t aliasId) const
        {
            const AliasFill f = mState.aliasFill(kQuest, aliasId);
            return f.mKnown ? f.mReference : kNullFormId;
        }
        StoryEvent event(FormId arrival) const
        {
            StoryEvent e;
            e.mType = "CLOC";
            e.mLocation2 = arrival;
            return e;
        }
        /// What the story manager does when it starts the quest: the event aliases, then the rest.
        void start(FormId arrival)
        {
            const StoryEvent e = event(arrival);
            ASSERT_TRUE(mFiller->canStart(kQuest, mState, &e));
            mFiller->fillEventAliases(kQuest, mState, e);
            mFiller->fillQuest(kQuest, mState, true);
        }

        QuestIndex mQuests;
        LocationIndex mLocations;
        QuestState mState;
        PlacedRefIndex mRefs;
        std::unique_ptr<QuestAliasFiller> mFiller;
        std::set<FormId> mAcceptLocation;
        std::vector<std::pair<std::uint32_t, FormId>> mAskedLocations;
        std::function<FormId(FormId)> mReferenceLocation;
        std::function<FormId(std::uint32_t)> mView;
        std::vector<FormId> mViewDuring;
        std::vector<bool> mViewCalls;
    };

    /// The BQ shape: Location (event L2), the centre marker found in it, the Hold found from the marker, the BountyLocation
    /// found by conditions that read the marker, and the Bounty found in the BountyLocation. The aliases are in the
    /// quest's order, which is NOT a dependency order: the Hold and the BountyLocation come before the marker.
    Bytes bountyAliases(std::uint32_t holdFlags = 0x300)
    {
        return alias(true, 0, "Location", 0, fromEvent('L', '2')) + alias(true, 4, "Hold", holdFlags, inAlias(3, 0, kKwHold))
            + alias(true, 6, "BountyLocation", 0x101, Bytes(), ctda(181, 3, kKwHold) + ctda(562, kKwDungeon))
            + alias(false, 3, "LocationCenterMarker", 0, inAlias(0, kTypeMarker))
            + alias(false, 7, "Bounty", 0x101, inAlias(6, kTypeBoss));
    }

    TEST(SkyrimBountyFill, theHoldIsTheNearestAncestorWithTheKeywordOfTheMarkersLocation)
    {
        FillRig rig(bountyAliases());
        rig.mAcceptLocation = { kDungeon };
        rig.start(kCity);
        EXPECT_EQ(rig.filled(0), kCity);
        EXPECT_EQ(rig.filled(3), kMarker);
        EXPECT_EQ(rig.filled(4), kHold); // the Hold: the marker is listed by the city, whose parent has the keyword
        EXPECT_EQ(rig.filled(6), kDungeon); // the BountyLocation: the matcher's pick
        EXPECT_EQ(rig.filled(7), kBoss); // the Bounty: the boss the dungeon lists, found from the BountyLocation
    }

    TEST(SkyrimBountyFill, theStartIsBlockedWhenNoAncestorHasTheKeyword)
    {
        // the marker's location is in no hold: a required Hold cannot be filled
        Bytes broken = lctn(kCity, 0, { kKwCity }, special(kTypeMarker, kMarker));
        FillRig rig(alias(true, 0, "Location", 0, fromEvent('L', '2')) + alias(false, 3, "Marker", 0, inAlias(0, kTypeMarker))
            + alias(true, 4, "Hold", 0x300, inAlias(3, 0, kKwHold)));
        rig.mLocations = LocationIndex(); // replaced below
        rig.mLocations.add(parseLocations(broken));
        QuestAliasFiller::Environment env;
        env.mLocations = &rig.mLocations;
        rig.mFiller->setEnvironment(std::move(env));
        const StoryEvent e = rig.event(kCity);
        EXPECT_FALSE(rig.mFiller->canStart(kQuest, rig.mState, &e));
    }

    TEST(SkyrimBountyFill, whenNoAncestorHasTheKeywordTheNearestLocationBelowItDoes)
    {
        // the marker's location is the hold itself... of the OTHER keyword: ask for Dungeon from the city, whose parent (the
        // hold) has no Dungeon keyword, so the nearest descendant of the city: none; from the hold: the dungeon
        const Bytes aliases = alias(true, 0, "Location", 0, fromEvent('L', '2'))
            + alias(false, 3, "Marker", 0, inAlias(0, kTypeMarker)) + alias(true, 4, "Dungeon", 0x2, inAlias(3, 0, kKwDungeon));
        FillRig rig(aliases);
        rig.mLocations.add(parseLocations(lctn(kCity, kHold, { kKwCity }, special(kTypeMarker, kMarker))));
        rig.start(kCity);
        // the city has no dungeon keyword, nor does anything above it: nothing below it has one either
        EXPECT_EQ(rig.filled(4), kNullFormId);
        // a marker that the hold itself lists: the descendants of the hold are searched
        FillRig rig2(aliases);
        rig2.mLocations.add(parseLocations(lctn(kHold, 0, { kKwHold }, special(kTypeMarker, kMarker))));
        rig2.mLocations.add(parseLocations(lctn(kCity, 0, {}))); // the city no longer lists the marker
        rig2.start(kHold);
        EXPECT_EQ(rig2.filled(4), kDungeon);
    }

    TEST(SkyrimBountyFill, aReferenceNoLocationListsIsLocatedByTheEnvironmentsFallback)
    {
        // a reference that no location lists: it is its cell's location that is meant
        FillRig rig(alias(false, 3, "Self", 0, sub4("ALFR", Bytes().u32(kLoose))) + alias(true, 4, "Hold", 0, inAlias(3, 0, kKwHold)),
            { placed(kLoose) });
        rig.mReferenceLocation = [](FormId reference) { return reference == kLoose ? kCity : kNullFormId; };
        QuestAliasFiller::Environment env;
        env.mLocations = &rig.mLocations;
        env.mReferenceLocation = rig.mReferenceLocation;
        rig.mFiller->setEnvironment(std::move(env));
        EXPECT_EQ(rig.mFiller->fillQuest(kQuest, rig.mState, true), 1u); // the hold (the reference is in the initial state)
        EXPECT_EQ(rig.filled(4), kHold);
    }

    TEST(SkyrimBountyFill, aFindMatchingLocationWaitsForTheAliasItsConditionReadsAndIsNeverAskedBefore)
    {
        FillRig rig(bountyAliases());
        rig.mAcceptLocation = { kDungeon };
        rig.start(kCity);
        ASSERT_FALSE(rig.mAskedLocations.empty());
        // every time the matcher was asked for the BountyLocation, the marker (alias 3) was already known to the condition
        for (const FormId seen : rig.mViewDuring)
            EXPECT_EQ(seen, kMarker);
        for (const auto& [aliasId, location] : rig.mAskedLocations)
            EXPECT_NE(aliasId, 3u); // the marker is a reference alias: never asked as a location
    }

    TEST(SkyrimBountyFill, theAliasViewIsGivenAroundTheEvaluationAndTakenAwayAfterwards)
    {
        FillRig rig(bountyAliases());
        rig.mAcceptLocation = { kDungeon };
        rig.start(kCity);
        ASSERT_FALSE(rig.mViewCalls.empty());
        EXPECT_FALSE(rig.mViewCalls.back()); // the last call hands over an empty function
        EXPECT_FALSE(static_cast<bool>(rig.mView));
        // calls come in pairs: a function, then an empty one
        for (std::size_t i = 0; i + 1 < rig.mViewCalls.size(); i += 2)
        {
            EXPECT_TRUE(rig.mViewCalls[i]);
            EXPECT_FALSE(rig.mViewCalls[i + 1]);
        }
    }

    TEST(SkyrimBountyFill, theKeywordAlsoFiltersTheChildSearchOfALocationAlias)
    {
        // ALFA naming a LOCATION alias (not in the data; the old child search): with a keyword only the children that have it
        const Bytes aliases = alias(true, 1, "Town", 0, sub4("ALFL", Bytes().u32(kHold)))
            + alias(true, 2, "Inside", 0, inAlias(1, 0, kKwDungeon));
        FillRig rig(aliases);
        EXPECT_EQ(rig.mFiller->fillQuest(kQuest, rig.mState, true), 2u);
        EXPECT_EQ(rig.filled(2), kDungeon); // the city is also inside the hold, but only the dungeon has the keyword
        FillRig other(alias(true, 1, "Town", 0, sub4("ALFL", Bytes().u32(kHold)))
            + alias(true, 2, "Inside", 0, inAlias(1, 0, kKwHold)));
        other.mFiller->fillQuest(kQuest, other.mState, true);
        EXPECT_EQ(other.filled(2), kNullFormId); // nothing INSIDE the hold has the hold keyword
    }

    TEST(SkyrimBountyFill, aSecondHoldIsNotTheAnswerForAMarkerOfTheFirst)
    {
        FillRig rig(bountyAliases());
        rig.mAcceptLocation = { kDungeon2 }; // the matcher (the real conditions) would refuse this one: another hold
        const StoryEvent e = rig.event(kCity);
        // with the stub matcher accepting only the other hold's dungeon the BountyLocation is filled by it: the fill code
        // trusts the matcher, which is where fn_181 refuses it (SkyrimBountyCond above)
        rig.mFiller->fillEventAliases(kQuest, rig.mState, e);
        rig.mFiller->fillQuest(kQuest, rig.mState, true);
        EXPECT_EQ(rig.filled(6), kDungeon2);
        EXPECT_EQ(rig.filled(4), kHold);
    }
}
