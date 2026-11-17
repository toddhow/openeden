// Task 0211: the alias filler for radiant quests: location aliases (from the data, from an event, find-matching, inside
// another alias), references found in a location's lists, external aliases, closest-first and loaded-area matching, the
// event gate for every event type, and the story manager's start gate (canStart). Generated records; no game data.
// Written first and run red against stubs.
#include <game_skyrimse/location.hpp>
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
    constexpr FormId kQuestA = kSelf | 0x800;
    constexpr FormId kQuestB = kSelf | 0x801;
    constexpr FormId kWorld = 0x0000003C;
    constexpr FormId kCityA = kSelf | 0x100; // grid (0, 0)
    constexpr FormId kCityB = kSelf | 0x101; // grid (10, 0)
    constexpr FormId kCityC = kSelf | 0x102; // no cell
    constexpr FormId kCave = kSelf | 0x103; // inside CityB
    constexpr FormId kCellA = kSelf | 0x150;
    constexpr FormId kCellB = kSelf | 0x151;
    constexpr FormId kCellInterior = kSelf | 0x152;
    constexpr FormId kRefType = kSelf | 0x300;
    constexpr FormId kRefTypeOther = kSelf | 0x301;
    constexpr FormId kRef1 = kSelf | 0xA01;
    constexpr FormId kRef2 = kSelf | 0xA02;
    constexpr FormId kRef3 = kSelf | 0xA03;
    constexpr FormId kRef4 = kSelf | 0xA04;
    constexpr FormId kBaseA = kSelf | 0xB00;
    constexpr FormId kBaseB = kSelf | 0xB01;
    constexpr FormId kPlayerRef = 0x14;

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

    Bytes ctda(std::uint16_t function, std::uint32_t p1 = 0)
    {
        Bytes b;
        b.u8(0).u8(0).u8(0).u8(0).f32(1.f);
        b.u16(function).u16(0).u32(p1).u32(0).u32(0).u32(0).u32(0);
        return sub4("CTDA", b);
    }

    Bytes questRecord(FormId id, const Bytes& aliases)
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("Q" + std::to_string(id & 0xFFF))));
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
        b.append(sub4("ALFD",
            Bytes().u32(static_cast<std::uint32_t>(static_cast<unsigned char>(kind))
                | (static_cast<std::uint32_t>(static_cast<unsigned char>(which)) << 8))));
        return b;
    }

    Bytes lctn(
        FormId id, FormId parent, const Bytes& cells, const Bytes& special = Bytes(), const Bytes& persistent = Bytes())
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("L" + std::to_string(id & 0xFFF))));
        if (parent != 0)
            b.append(sub4("PNAM", Bytes().u32(parent)));
        if (!cells.str().empty())
            b.append(sub4("LCEC", cells));
        if (!special.str().empty())
            b.append(sub4("LCSR", special));
        if (!persistent.str().empty())
            b.append(sub4("LCPR", persistent));
        return record4("LCTN", 0u, id, b);
    }

    Bytes gridCell(int y, int x)
    {
        return Bytes().u32(kWorld).u16(static_cast<std::uint16_t>(y)).u16(static_cast<std::uint16_t>(x));
    }

    Bytes special(FormId type, FormId ref)
    {
        return Bytes().u32(type).u32(ref).u32(kWorld).u16(0).u16(0);
    }

    constexpr std::uint32_t kOptional = 0x2;
    constexpr std::uint32_t kLoadedArea = 0x20;

    PlacedEntry placedRef(
        FormId id, FormId base, FormId cell, float x, float y, std::uint8_t flags = PlacedFlags::IsActor)
    {
        PlacedEntry r;
        r.mId = id;
        r.mBase = base;
        r.mCell = cell;
        r.mX = x;
        r.mY = y;
        r.mFlags = flags;
        return r;
    }

    struct Rig
    {
        Rig(const std::vector<Bytes>& quests, const Bytes& locations, std::vector<PlacedEntry> refs)
        {
            Bytes plugin = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes all;
            for (const Bytes& q : quests)
                all.append(q);
            plugin.append(group4(Content::fourCC("QUST"), 0, all));
            plugin.append(group4(Content::fourCC("LCTN"), 0, locations));
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
            mFiller = std::make_unique<QuestAliasFiller>(
                mQuests, mRefs, [this](const QuestRecord&, const QuestAlias&, FormId reference, FormId) {
                    mAsked.push_back(reference);
                    return mAccept.empty() || mAccept.count(reference) != 0;
                });
            QuestAliasFiller::Environment env;
            env.mLocations = &mLocations;
            env.mLocationMatcher = [this](const QuestRecord&, const QuestAlias&, FormId location) {
                mAskedLocations.push_back(location);
                return mAcceptLocation.empty() || mAcceptLocation.count(location) != 0;
            };
            env.mPlayer = [this] { return mSite; };
            env.mCell = [this](FormId cell) {
                const auto it = mCellFacts.find(cell);
                return it == mCellFacts.end() ? QuestAliasFiller::CellFacts() : it->second;
            };
            mFiller->setEnvironment(std::move(env));
        }

        QuestIndex mQuests;
        LocationIndex mLocations;
        QuestState mState;
        PlacedRefIndex mRefs;
        std::unique_ptr<QuestAliasFiller> mFiller;
        std::set<FormId> mAccept;
        std::set<FormId> mAcceptLocation;
        std::vector<FormId> mAsked;
        std::vector<FormId> mAskedLocations;
        QuestAliasFiller::PlayerSite mSite;
        std::map<FormId, QuestAliasFiller::CellFacts> mCellFacts;

        FormId filled(FormId quest, std::uint32_t aliasId) const
        {
            const AliasFill f = mState.aliasFill(quest, aliasId);
            return f.mKnown ? f.mReference : kNullFormId;
        }
    };

    Bytes cities()
    {
        Bytes recs = lctn(kCityA, 0, gridCell(0, 0));
        recs.append(lctn(kCityB, 0, gridCell(0, 10)));
        recs.append(lctn(kCityC, 0, Bytes()));
        recs.append(lctn(kCave, kCityB, gridCell(0, 11)));
        return recs;
    }

    QuestAliasFiller::PlayerSite exteriorAt(double x, double y)
    {
        QuestAliasFiller::PlayerSite s;
        s.mKnown = true;
        s.mWorld = kWorld;
        s.mX = x;
        s.mY = y;
        return s;
    }

    // ---- location aliases -------------------------------------------------------------------------------------------

    TEST(RadiantLocation, aSpecificLocationIsFilledFromTheDataWithoutMatching)
    {
        Rig rig({ questRecord(kQuestA, alias(true, 1, "Where", 0, sub4("ALFL", Bytes().u32(kCityB)))) }, cities(), {});
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, false), 1u);
        EXPECT_EQ(rig.filled(kQuestA, 1), kCityB);
        EXPECT_EQ(rig.mFiller->stats().mLocationsFilled, 1u);
    }

    TEST(RadiantLocation, aFindMatchingLocationTakesTheClosestOneWhoseConditionsPass)
    {
        Rig rig({ questRecord(kQuestA, alias(true, 1, "Where", 0, Bytes(), ctda(562, kSelf | 0x600))) }, cities(), {});
        rig.mSite = exteriorAt(10.0 * 4096 + 100, 100); // near city B (grid x 10)
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, true), 1u);
        // B's cell is the closest; the cave (x 11) and A (x 0) are farther, C has no cell
        EXPECT_EQ(rig.filled(kQuestA, 1), kCityB);
        EXPECT_EQ(rig.mAskedLocations.front(), kCityB);
    }

    TEST(RadiantLocation, theMatcherRejectsAndTheNextClosestIsTaken)
    {
        Rig rig({ questRecord(kQuestA, alias(true, 1, "Where", 0, Bytes())) }, cities(), {});
        rig.mSite = exteriorAt(10.0 * 4096 + 100, 100);
        rig.mAcceptLocation = { kCityA, kCityC };
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, true), 1u);
        EXPECT_EQ(rig.filled(kQuestA, 1), kCityA); // B and the cave were asked first and refused; A is next by distance
        EXPECT_EQ(rig.mAskedLocations, (std::vector<FormId>{ kCityB, kCave, kCityA }));
    }

    TEST(RadiantLocation, withoutAPlayerPlaceTheLocationsAreTriedInIdOrder)
    {
        Rig rig({ questRecord(kQuestA, alias(true, 1, "Where", 0, Bytes())) }, cities(), {});
        rig.mAcceptLocation = { kCityC };
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, true), 1u);
        EXPECT_EQ(rig.filled(kQuestA, 1), kCityC);
        EXPECT_EQ(rig.mAskedLocations, (std::vector<FormId>{ kCityA, kCityB, kCityC }));
    }

    TEST(RadiantLocation, aLocationAnotherAliasHoldsIsNotTakenAgain)
    {
        const Bytes aliases
            = alias(true, 1, "One", 0, sub4("ALFL", Bytes().u32(kCityA))) + alias(true, 2, "Two", 0, Bytes());
        Rig rig({ questRecord(kQuestA, aliases) }, cities(), {});
        rig.mSite = exteriorAt(100, 100);
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, true), 2u);
        EXPECT_EQ(
            rig.filled(kQuestA, 2), kCityB); // A is held by alias 1: the next closest to (100, 100) is B, not the cave
    }

    TEST(RadiantLocation, anInteriorPlayerIsPlacedAtTheFirstCellOfTheNearestLocationUpItsChain)
    {
        constexpr FormId kRoom = kSelf | 0x104;
        Bytes recs = cities();
        recs.append(lctn(kRoom, kCityB, Bytes()));
        Rig rig({ questRecord(kQuestA, alias(true, 1, "Where", 0, Bytes())) }, recs, {});
        rig.mSite.mKnown = true;
        rig.mSite.mInterior = true;
        rig.mSite.mCell = kCellInterior;
        rig.mSite.mLocation = kRoom;
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, true), 1u);
        EXPECT_EQ(rig.filled(kQuestA, 1), kCityB); // by id order it would be city A
    }

    TEST(RadiantLocation, aLocationInsideTheLocationOfAnotherAliasIsTakenFromItsChildren)
    {
        const Bytes aliases = alias(true, 1, "Town", 0, sub4("ALFL", Bytes().u32(kCityB)))
            + alias(true, 2, "Inside", 0, sub4("ALFA", Bytes().u32(1)));
        Rig rig({ questRecord(kQuestA, aliases) }, cities(), {});
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, true), 2u);
        EXPECT_EQ(rig.filled(kQuestA, 2), kCave);
    }

    TEST(RadiantLocation, anInLocationAliasWithNoParentFillStaysEmpty)
    {
        const Bytes aliases = alias(true, 2, "Inside", 0, sub4("ALFA", Bytes().u32(1)));
        Rig rig({ questRecord(kQuestA, aliases) }, cities(), {});
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, true), 0u);
    }

    // ---- event aliases ----------------------------------------------------------------------------------------------

    TEST(RadiantEvent, aLocationAliasTakesTheEventsLocation)
    {
        const Bytes aliases
            = alias(true, 1, "Left", 0, fromEvent('L', '1')) + alias(true, 2, "Came", 0, fromEvent('L', '2'));
        Rig rig({ questRecord(kQuestA, aliases) }, cities(), {});
        StoryEvent e;
        e.mType = "CLOC";
        e.mLocation1 = kCityA;
        e.mLocation2 = kCityB;
        EXPECT_TRUE(rig.mFiller->eventAliasesMatch(kQuestA, e));
        EXPECT_EQ(rig.mFiller->fillEventAliases(kQuestA, rig.mState, e), 2u);
        EXPECT_EQ(rig.filled(kQuestA, 1), kCityA);
        EXPECT_EQ(rig.filled(kQuestA, 2), kCityB);
    }

    TEST(RadiantEvent, aRequiredLocationAliasWithNoEventLocationBlocksTheMatchAndAnOptionalOneDoesNot)
    {
        Rig rig({ questRecord(kQuestA, alias(true, 1, "Came", 0, fromEvent('L', '2'))),
                    questRecord(kQuestB, alias(true, 1, "Came", kOptional, fromEvent('L', '2'))) },
            cities(), {});
        StoryEvent e;
        e.mType = "CLOC";
        e.mLocation1 = kCityA; // no L2
        EXPECT_FALSE(rig.mFiller->eventAliasesMatch(kQuestA, e));
        EXPECT_TRUE(rig.mFiller->eventAliasesMatch(kQuestB, e));
    }

    TEST(RadiantEvent, aLocationAliasMatcherJudgesTheEventsLocation)
    {
        Rig rig({ questRecord(kQuestA, alias(true, 1, "Came", 0, fromEvent('L', '2'))) }, cities(), {});
        StoryEvent e;
        e.mType = "CLOC";
        e.mLocation2 = kCityB;
        rig.mAcceptLocation = { kCityA };
        EXPECT_FALSE(rig.mFiller->eventAliasesMatch(kQuestA, e));
        rig.mAcceptLocation = { kCityB };
        EXPECT_TRUE(rig.mFiller->eventAliasesMatch(kQuestA, e));
    }

    TEST(RadiantEvent, thePlayerReferenceIsAcceptedAsAnEventReferenceWithThePlayersBase)
    {
        Rig rig({ questRecord(kQuestA, alias(false, 1, "Killer", 0, fromEvent('R', '2'))) }, cities(),
            { placedRef(kRef1, kBaseA, kCellA, 0, 0) });
        StoryEvent e;
        e.mType = "KILL";
        e.mRef1 = kRef1;
        e.mRef2 = kPlayerRef;
        EXPECT_TRUE(rig.mFiller->eventAliasesMatch(kQuestA, e));
        ASSERT_FALSE(rig.mAsked.empty());
        EXPECT_EQ(rig.mAsked.back(), kPlayerRef);
        EXPECT_EQ(rig.mFiller->fillEventAliases(kQuestA, rig.mState, e), 1u);
        EXPECT_EQ(rig.filled(kQuestA, 1), kPlayerRef);
    }

    TEST(RadiantEvent, aReferenceThatTheDataDoesNotPlaceIsNotAnEventReference)
    {
        Rig rig({ questRecord(kQuestA, alias(false, 1, "Victim", 0, fromEvent('R', '1'))) }, cities(), {});
        StoryEvent e;
        e.mType = "KILL";
        e.mRef1 = kRef4;
        EXPECT_FALSE(rig.mFiller->eventAliasesMatch(kQuestA, e));
    }

    // ---- references in a location, external and find-matching ------------------------------------------------------

    TEST(RadiantReference, anInLocationAliasTakesASpecialReferenceOfItsTypeThatPasses)
    {
        const Bytes recs = lctn(kCityB, 0, gridCell(0, 10),
            special(kRefTypeOther, kRef1) + special(kRefType, kRef2) + special(kRefType, kRef3));
        const Bytes aliases = alias(true, 1, "Town", 0, sub4("ALFL", Bytes().u32(kCityB)))
            + alias(false, 2, "Boss", 0, sub4("ALFA", Bytes().u32(1)) + sub4("ALRT", Bytes().u32(kRefType)));
        Rig rig({ questRecord(kQuestA, aliases) }, recs,
            { placedRef(kRef1, kBaseA, kCellB, 0, 0), placedRef(kRef2, kBaseA, kCellB, 0, 0),
                placedRef(kRef3, kBaseB, kCellB, 0, 0) });
        rig.mAccept = { kRef3 };
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, true), 2u);
        EXPECT_EQ(rig.filled(kQuestA, 2), kRef3);
        EXPECT_EQ(rig.mAsked, (std::vector<FormId>{ kRef2, kRef3 })); // the other type was never asked
        EXPECT_EQ(rig.mFiller->stats().mInLocationFilled, 1u);
    }

    TEST(RadiantReference, withNoReferenceTypeAnyListedReferenceOfTheLocationCounts)
    {
        const Bytes recs
            = lctn(kCityB, 0, gridCell(0, 10), special(kRefType, kRef2), Bytes().u32(kRef1).u32(kWorld).u16(0).u16(0));
        const Bytes aliases = alias(true, 1, "Town", 0, sub4("ALFL", Bytes().u32(kCityB)))
            + alias(false, 2, "Any", 0, sub4("ALFA", Bytes().u32(1)));
        Rig rig({ questRecord(kQuestA, aliases) }, recs,
            { placedRef(kRef1, kBaseA, kCellB, 0, 0), placedRef(kRef2, kBaseA, kCellB, 0, 0) });
        rig.mAccept = { kRef1 };
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, true), 2u);
        EXPECT_EQ(rig.filled(kQuestA, 2), kRef1);
    }

    TEST(RadiantReference, aListedReferenceTheDataDoesNotPlaceOrADisabledOneIsSkipped)
    {
        const Bytes recs = lctn(
            kCityB, 0, gridCell(0, 10), special(kRefType, kRef4) + special(kRefType, kRef1) + special(kRefType, kRef2));
        const Bytes aliases = alias(true, 1, "Town", 0, sub4("ALFL", Bytes().u32(kCityB)))
            + alias(false, 2, "Boss", 0, sub4("ALFA", Bytes().u32(1)) + sub4("ALRT", Bytes().u32(kRefType)));
        Rig rig({ questRecord(kQuestA, aliases) }, recs,
            { placedRef(kRef1, kBaseA, kCellB, 0, 0, PlacedFlags::IsActor | PlacedFlags::Disabled),
                placedRef(kRef2, kBaseA, kCellB, 0, 0) });
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, true), 2u);
        EXPECT_EQ(rig.filled(kQuestA, 2), kRef2);
    }

    TEST(RadiantReference, anExternalAliasTakesWhatTheOtherQuestsAliasHolds)
    {
        Rig rig(
            { questRecord(kQuestA, alias(false, 5, "Boss", 0, sub4("ALFR", Bytes().u32(kRef2)))),
                questRecord(kQuestB,
                    alias(false, 1, "Same", 0, sub4("ALEQ", Bytes().u32(kQuestA)) + sub4("ALEA", Bytes().u32(5)))) },
            cities(), { placedRef(kRef2, kBaseA, kCellB, 0, 0) });
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestB, rig.mState, true), 1u);
        EXPECT_EQ(rig.filled(kQuestB, 1), kRef2);
        EXPECT_EQ(rig.mFiller->stats().mExternalFilled, 1u);
    }

    TEST(RadiantReference, anExternalAliasWhoseOtherAliasIsEmptyStaysEmpty)
    {
        Rig rig({ questRecord(kQuestB,
                    alias(false, 1, "Same", 0, sub4("ALEQ", Bytes().u32(kQuestA)) + sub4("ALEA", Bytes().u32(5)))) },
            cities(), {});
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestB, rig.mState, true), 0u);
    }

    TEST(RadiantReference, findMatchingReferencesAreTriedClosestToThePlayerFirstInTheSameWorld)
    {
        Rig rig({ questRecord(kQuestA, alias(false, 1, "Any", 0, Bytes())) }, cities(),
            { placedRef(kRef1, kBaseA, kCellA, 5000, 0), placedRef(kRef2, kBaseA, kCellA, 100, 0),
                placedRef(kRef3, kBaseA, kCellA, 900, 0) });
        rig.mCellFacts[kCellA] = QuestAliasFiller::CellFacts{ true, false, kWorld, true };
        rig.mSite = exteriorAt(0, 0);
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, true), 1u);
        EXPECT_EQ(rig.filled(kQuestA, 1), kRef2);
        EXPECT_EQ(rig.mAsked, (std::vector<FormId>{ kRef2 })); // the closest passes: nothing else is asked
    }

    TEST(RadiantReference, aReferenceInAnotherInteriorIsFartherThanAnyInTheSameWorld)
    {
        Rig rig({ questRecord(kQuestA, alias(false, 1, "Any", 0, Bytes())) }, cities(),
            { placedRef(kRef1, kBaseA, kCellInterior, 0, 0), placedRef(kRef2, kBaseA, kCellA, 90000, 0) });
        rig.mCellFacts[kCellA] = QuestAliasFiller::CellFacts{ true, false, kWorld, true };
        rig.mCellFacts[kCellInterior] = QuestAliasFiller::CellFacts{ true, true, kNullFormId, true };
        rig.mSite = exteriorAt(0, 0);
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, true), 1u);
        EXPECT_EQ(rig.filled(kQuestA, 1), kRef2);
    }

    TEST(RadiantReference, theLoadedAreaFlagKeepsOnlyReferencesInBuiltCells)
    {
        Rig rig({ questRecord(kQuestA, alias(false, 1, "Near", kLoadedArea, Bytes())) }, cities(),
            { placedRef(kRef1, kBaseA, kCellA, 100, 0), placedRef(kRef2, kBaseA, kCellB, 200, 0) });
        rig.mCellFacts[kCellA] = QuestAliasFiller::CellFacts{ true, false, kWorld, false }; // not built
        rig.mCellFacts[kCellB] = QuestAliasFiller::CellFacts{ true, false, kWorld, true };
        rig.mSite = exteriorAt(0, 0);
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, true), 1u);
        EXPECT_EQ(rig.filled(kQuestA, 1), kRef2);
    }

    TEST(RadiantReference, withoutAPlayerPlaceFindMatchingKeepsTheOldIdOrder)
    {
        Rig rig({ questRecord(kQuestA, alias(false, 1, "Any", 0, Bytes())) }, cities(),
            { placedRef(kRef2, kBaseA, kCellA, 100, 0), placedRef(kRef1, kBaseA, kCellA, 5000, 0) });
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, true), 1u);
        EXPECT_EQ(rig.filled(kQuestA, 1), kRef1);
    }

    TEST(RadiantReference, aCreatedObjectAndANearAliasAreCountedAndNeverFilled)
    {
        const Bytes aliases = alias(false, 1, "Letter", 0, sub4("ALCO", Bytes().u32(0)))
            + alias(false, 2, "Near", 0, sub4("ALNA", Bytes().u32(1)));
        Rig rig({ questRecord(kQuestA, aliases) }, cities(), {});
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, true), 0u);
        EXPECT_EQ(rig.mFiller->stats().mCreatedNotAnswered, 2u);
    }

    // ---- the start gate ---------------------------------------------------------------------------------------------

    TEST(RadiantStart, aQuestWhoseRequiredFindMatchingLocationFindsNothingCannotStartAndNothingChanges)
    {
        Rig rig({ questRecord(kQuestA, alias(true, 1, "Where", 0, Bytes())) }, cities(), {});
        rig.mAcceptLocation = { 0x77777 };
        EXPECT_FALSE(rig.mFiller->canStart(kQuestA, rig.mState, nullptr));
        EXPECT_EQ(rig.filled(kQuestA, 1), kNullFormId);
        EXPECT_EQ(rig.mFiller->stats().mStartsBlocked, 1u);
        rig.mAcceptLocation = { kCityB };
        EXPECT_TRUE(rig.mFiller->canStart(kQuestA, rig.mState, nullptr));
        EXPECT_EQ(rig.filled(kQuestA, 1), kNullFormId); // a dry run
        EXPECT_EQ(rig.mFiller->stats().mFilled, 0u);
    }

    TEST(RadiantStart, aFindMatchingReferenceNeverBlocksAndAStartDoesNotLookForIt)
    {
        Rig rig({ questRecord(kQuestA, alias(false, 1, "Any", 0, Bytes())) }, cities(),
            { placedRef(kRef1, kBaseA, kCellA, 0, 0) });
        rig.mAccept = { kRef4 };
        EXPECT_TRUE(rig.mFiller->canStart(kQuestA, rig.mState, nullptr));
        EXPECT_TRUE(rig.mAsked.empty());
    }

    TEST(RadiantStart, anOptionalAliasThatFindsNothingDoesNotBlock)
    {
        Rig rig({ questRecord(kQuestA, alias(false, 1, "Any", kOptional, Bytes())) }, cities(), {});
        EXPECT_TRUE(rig.mFiller->canStart(kQuestA, rig.mState, nullptr));
    }

    TEST(RadiantStart, aCreatedObjectAliasNeverBlocks)
    {
        Rig rig({ questRecord(kQuestA, alias(false, 1, "Letter", 0, sub4("ALCO", Bytes().u32(0)))) }, cities(), {});
        EXPECT_TRUE(rig.mFiller->canStart(kQuestA, rig.mState, nullptr));
    }

    TEST(RadiantStart, aRequiredEventAliasNeedsTheEventAndItsReference)
    {
        Rig rig({ questRecord(kQuestA, alias(false, 1, "Victim", 0, fromEvent('R', '1'))) }, cities(),
            { placedRef(kRef1, kBaseA, kCellA, 0, 0) });
        EXPECT_FALSE(rig.mFiller->canStart(kQuestA, rig.mState, nullptr));
        StoryEvent e;
        e.mType = "KILL";
        e.mRef1 = kRef1;
        EXPECT_TRUE(rig.mFiller->canStart(kQuestA, rig.mState, &e));
        e.mRef1 = kRef4;
        EXPECT_FALSE(rig.mFiller->canStart(kQuestA, rig.mState, &e));
    }

    TEST(RadiantStart, aLaterAliasMayDependOnAnEarlierOneOfTheSamePlan)
    {
        // the reference alias is listed BEFORE its location alias: the plan fills locations first
        const Bytes recs = lctn(kCityB, 0, gridCell(0, 10), special(kRefType, kRef2));
        const Bytes aliases
            = alias(false, 2, "Boss", 0, sub4("ALFA", Bytes().u32(1)) + sub4("ALRT", Bytes().u32(kRefType)))
            + alias(true, 1, "Town", 0, sub4("ALFL", Bytes().u32(kCityB)));
        Rig rig({ questRecord(kQuestA, aliases) }, recs, { placedRef(kRef2, kBaseA, kCellB, 0, 0) });
        EXPECT_TRUE(rig.mFiller->canStart(kQuestA, rig.mState, nullptr));
        EXPECT_EQ(rig.mFiller->fillQuest(kQuestA, rig.mState, true), 2u);
        EXPECT_EQ(rig.filled(kQuestA, 2), kRef2);
    }

    TEST(RadiantStart, twoRunsGiveTheSameFillsAndTheSameQuestions)
    {
        std::vector<FormId> asked[2];
        FormId result[2] = {};
        for (int run = 0; run < 2; ++run)
        {
            Rig rig({ questRecord(kQuestA, alias(false, 1, "Any", 0, Bytes()) + alias(true, 2, "Where", 0, Bytes())) },
                cities(),
                { placedRef(kRef1, kBaseA, kCellA, 300, 0), placedRef(kRef2, kBaseA, kCellA, 200, 0),
                    placedRef(kRef3, kBaseA, kCellA, 200, 0) });
            rig.mCellFacts[kCellA] = QuestAliasFiller::CellFacts{ true, false, kWorld, true };
            rig.mSite = exteriorAt(0, 0);
            rig.mAccept = { kRef3, kRef1 };
            (void)rig.mFiller->fillQuest(kQuestA, rig.mState, true);
            asked[run] = rig.mAsked;
            result[run] = rig.filled(kQuestA, 1);
        }
        EXPECT_EQ(asked[0], asked[1]);
        EXPECT_EQ(result[0], result[1]);
        EXPECT_EQ(result[0], kRef3); // ties on distance go to the lower id: ref 2 first (refused), then ref 3
    }
}
