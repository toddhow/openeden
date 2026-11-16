// Task 0201 (milestone M-S7): the map markers, the discovered state, the quest targets, the save chunk and the
// "skyrimse.map" bindings. Generated plugins and hand-made quests; no game data.
#include <game_skyrimse/map.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using namespace game_skyrimse::map;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kWorld = kSelf | 0x100;
    constexpr FormId kPersistent = kSelf | 0x101;
    constexpr FormId kInterior = kSelf | 0x102;
    constexpr FormId kMarkerBase = 0x10;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes marker(FormId id, std::uint32_t recordFlags, std::uint8_t flags, std::uint8_t type, const Bytes& name,
        float x, float y, bool withXmrk = true)
    {
        Bytes data;
        data.append(sub4("NAME", Bytes().u32(kMarkerBase)));
        if (withXmrk)
        {
            data.append(sub4("XMRK", Bytes()));
            data.append(sub4("FNAM", Bytes().u8(flags)));
            data.append(sub4("FULL", name));
            data.append(sub4("TNAM", Bytes().u8(type).u8(0)));
        }
        data.append(sub4("DATA", Bytes().f32(x).f32(y).f32(0).f32(0).f32(0).f32(0)));
        return record4("REFR", recordFlags, id, data);
    }

    Bytes testPlugin(std::uint32_t headerFlags = 0, bool localized = false)
    {
        const auto name = [&](const char* text, std::uint32_t id) {
            return localized ? Bytes().u32(id) : Bytes().zstring(text);
        };
        Bytes persistent;
        persistent.append(marker(kSelf | 0x200, 0x400, 1, 5, name("Whiterun", 7), 100, 200));
        persistent.append(marker(kSelf | 0x201, 0x400, 0, 7, name("Riverwood", 8), 5000, -300));
        persistent.append(marker(kSelf | 0x202, 0x400, 0, 0, name("", 0), 0, 0, false)); // a plain REFR
        persistent.append(marker(kSelf | 0x203, 0x400 | 0x20, 0, 0, name("", 0), 0, 0, false)); // deleted
        Bytes worldChildren;
        worldChildren.append(record4("CELL", 0, kPersistent, sub4("EDID", Bytes().zstring("Persistent"))));
        worldChildren.append(group4(kPersistent, 6, group4(kPersistent, 8, persistent)));

        Bytes interior;
        interior.append(marker(kSelf | 0x204, 0x400, 0, 3, name("Inside", 9), 1, 1));
        Bytes cellBlock;
        cellBlock.append(record4("CELL", 0, kInterior, sub4("EDID", Bytes().zstring("Inside"))));
        cellBlock.append(group4(kInterior, 6, group4(kInterior, 8, interior)));

        Bytes all = tes4Header(1.7f, headerFlags, "A", "B", 4, { { "Skyrim.esm", 0 } }, 24, 44);
        all.append(group4(kWorld, 1, worldChildren));
        all.append(group4(Content::fourCC("CELL"), 0, cellBlock));
        return all;
    }

    MarkerRecords parse(const Bytes& bytes, bool& ok)
    {
        Content::Error error;
        const auto plugin = Content::Plugin::open(
            std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
        MarkerRecords out;
        ok = plugin != nullptr && parseMarkers(*plugin, order(), MarkerLimits(), out);
        return out;
    }

    Marker m(FormId ref, FormId world, float x, float y, std::uint8_t flags = 0, std::uint8_t type = 1)
    {
        Marker marker;
        marker.mRef = ref;
        marker.mWorld = world;
        marker.mX = x;
        marker.mY = y;
        marker.mFlags = flags;
        marker.mType = type;
        marker.mName.mPresent = true;
        marker.mName.mText = "M" + std::to_string(ref & 0xFFF);
        return marker;
    }

    MarkerIndex indexOf(std::vector<Marker> markers)
    {
        MarkerRecords records;
        records.mMarkers = std::move(markers);
        MarkerIndex index;
        index.add(std::move(records));
        index.finish();
        return index;
    }

    // ---- the parse ----------------------------------------------------------------------------------------------

    TEST(SkyrimMapParse, readsTheMarkersOfAWorldsPersistentCell)
    {
        bool ok = false;
        const MarkerRecords r = parse(testPlugin(), ok);
        ASSERT_TRUE(ok);
        EXPECT_EQ(r.mFailed, 0u);
        EXPECT_EQ(r.mOddSubrecords, 0u);
        // the plain REFR is not a marker; the deleted one, the two world markers and the interior one are listed
        ASSERT_EQ(r.mMarkers.size(), 4u);
        const Marker& whiterun = r.mMarkers[0];
        EXPECT_EQ(whiterun.mRef, kSelf | 0x200);
        EXPECT_EQ(whiterun.mWorld, kWorld);
        EXPECT_EQ(whiterun.mFlags, 1);
        EXPECT_EQ(whiterun.mType, 5);
        EXPECT_FLOAT_EQ(whiterun.mX, 100.f);
        EXPECT_FLOAT_EQ(whiterun.mY, 200.f);
        EXPECT_EQ(whiterun.mName.mText, "Whiterun");
        EXPECT_FALSE(whiterun.mName.mLocalized);
        EXPECT_EQ(r.mMarkers[1].mType, 7);
        EXPECT_EQ(r.mMarkers[1].mWorld, kWorld);
        EXPECT_FLOAT_EQ(r.mMarkers[1].mY, -300.f);
    }

    TEST(SkyrimMapParse, aMarkerOutsideAWorldHasNoWorld)
    {
        bool ok = false;
        const MarkerRecords r = parse(testPlugin(), ok);
        ASSERT_TRUE(ok);
        EXPECT_EQ(r.mMarkers.back().mRef, kSelf | 0x204);
        EXPECT_EQ(r.mMarkers.back().mWorld, kNullFormId);
    }

    TEST(SkyrimMapParse, aLocalisedNameIsAnIdNotText)
    {
        bool ok = false;
        const MarkerRecords r = parse(testPlugin(0x80, true), ok);
        ASSERT_TRUE(ok);
        EXPECT_TRUE(r.mMarkers[0].mName.mLocalized);
        EXPECT_EQ(r.mMarkers[0].mName.mId, 7u);
        EXPECT_EQ(r.mMarkers[1].mName.mId, 8u);
    }

    TEST(SkyrimMapParse, aSubrecordOfTheWrongSizeIsCountedAndNeverGuessed)
    {
        Bytes data;
        data.append(sub4("NAME", Bytes().u32(kMarkerBase)));
        data.append(sub4("XMRK", Bytes()));
        data.append(sub4("FNAM", Bytes().u8(1).u8(1))); // 2 bytes
        data.append(sub4("TNAM", Bytes().u8(4))); // 1 byte
        data.append(sub4("DATA", Bytes().f32(1).f32(2).f32(0).f32(0).f32(0).f32(0)));
        Bytes worldChildren;
        worldChildren.append(record4("CELL", 0, kPersistent, sub4("EDID", Bytes().zstring("P"))));
        worldChildren.append(group4(kPersistent, 6, group4(kPersistent, 8, record4("REFR", 0, kSelf | 0x300, data))));
        Bytes all = tes4Header(1.7f, 0, "A", "B", 4, { { "Skyrim.esm", 0 } }, 24, 44);
        all.append(group4(kWorld, 1, worldChildren));
        bool ok = false;
        const MarkerRecords r = parse(all, ok);
        ASSERT_TRUE(ok);
        EXPECT_EQ(r.mOddSubrecords, 2u);
        ASSERT_EQ(r.mMarkers.size(), 1u);
        EXPECT_EQ(r.mMarkers[0].mFlags, 0);
        EXPECT_EQ(r.mMarkers[0].mType, 0);
    }

    TEST(SkyrimMapParse, aCutShortRecordFailsThatRecordAndTheWalkGoesOn)
    {
        Bytes data;
        data.append(sub4("NAME", Bytes().u32(kMarkerBase)));
        data.append(Bytes().fourCC("XMRK").u16(40)); // claims 40 bytes, has none
        Bytes worldChildren;
        worldChildren.append(record4("CELL", 0, kPersistent, sub4("EDID", Bytes().zstring("P"))));
        worldChildren.append(group4(kPersistent, 6,
            group4(kPersistent, 8,
                record4("REFR", 0, kSelf | 0x300, data)
                    .append(marker(kSelf | 0x301, 0x400, 1, 2, Bytes().zstring("Ok"), 3, 4)))));
        Bytes all = tes4Header(1.7f, 0, "A", "B", 4, { { "Skyrim.esm", 0 } }, 24, 44);
        all.append(group4(kWorld, 1, worldChildren));
        bool ok = false;
        const MarkerRecords r = parse(all, ok);
        ASSERT_TRUE(ok);
        EXPECT_EQ(r.mFailed, 1u);
        ASSERT_EQ(r.mMarkers.size(), 1u);
        EXPECT_EQ(r.mMarkers[0].mName.mText, "Ok");
    }

    // ---- the index ------------------------------------------------------------------------------------------------

    TEST(SkyrimMapIndex, aMarkerOfNoWorldAndADeletedOneAreNotKept)
    {
        bool ok = false;
        MarkerRecords r = parse(testPlugin(), ok);
        ASSERT_TRUE(ok);
        MarkerIndex index;
        index.add(std::move(r));
        index.finish();
        ASSERT_EQ(index.size(), 2u);
        EXPECT_NE(index.find(kSelf | 0x200), nullptr);
        EXPECT_EQ(index.find(kSelf | 0x204), nullptr);
        EXPECT_EQ(index.find(kSelf | 0x203), nullptr);
        EXPECT_TRUE(index.hasWorld(kWorld));
        EXPECT_FALSE(index.hasWorld(kWorld + 1));
    }

    TEST(SkyrimMapIndex, aLaterPluginReplacesAnEarlierRecordAndADeletedOneRemovesIt)
    {
        MarkerRecords first;
        first.mMarkers = { m(1, kWorld, 0, 0), m(2, kWorld, 10, 10), m(3, kWorld, 20, 20) };
        MarkerRecords second;
        second.mMarkers = { m(1, kWorld, 99, 99, MarkerFlags::Visible) };
        Marker removed = m(2, kWorld, 0, 0);
        removed.mDeleted = true;
        second.mMarkers.push_back(removed);
        MarkerIndex index;
        index.add(std::move(first));
        index.add(std::move(second));
        index.finish();
        ASSERT_EQ(index.size(), 2u);
        EXPECT_FLOAT_EQ(index.find(1)->mX, 99.f);
        EXPECT_EQ(index.find(1)->mPlugin, 1u); // the plugin whose record won: its string table names it
        EXPECT_EQ(index.find(2), nullptr);
        EXPECT_NE(index.find(3), nullptr);
        EXPECT_EQ(index.find(3)->mPlugin, 0u);
    }

    TEST(SkyrimMapIndex, theListIsSortedByReference)
    {
        const MarkerIndex index = indexOf({ m(9, kWorld, 0, 0), m(3, kWorld, 0, 0), m(5, kWorld, 0, 0) });
        ASSERT_EQ(index.size(), 3u);
        EXPECT_EQ(index.all()[0].mRef, 3u);
        EXPECT_EQ(index.all()[2].mRef, 9u);
    }

    // ---- discovery ------------------------------------------------------------------------------------------------

    TEST(SkyrimMapDiscovery, aMarkerWithinTheRadiusIsDiscoveredOnce)
    {
        const MarkerIndex index = indexOf({ m(1, kWorld, 100, 0), m(2, kWorld, 5000, 0), m(3, kWorld + 1, 100, 0) });
        Discovery d;
        EXPECT_EQ(d.visit(index, kWorld, 0, 0, 1000.f), 1u);
        EXPECT_TRUE(d.discovered(1));
        EXPECT_FALSE(d.discovered(2));
        EXPECT_FALSE(d.discovered(3)); // another world
        const std::uint64_t revision = d.revision();
        EXPECT_EQ(d.visit(index, kWorld, 0, 0, 1000.f), 0u);
        EXPECT_EQ(d.revision(), revision); // nothing new, nothing changed
        EXPECT_EQ(d.visit(index, kWorld, 4500, 0, 1000.f), 1u);
        EXPECT_GT(d.revision(), revision);
        EXPECT_EQ(d.count(), 2u);
    }

    TEST(SkyrimMapDiscovery, theDistanceIsHorizontalAndTheEdgeCounts)
    {
        Marker high = m(1, kWorld, 3, 4);
        high.mZ = 99999.f;
        const MarkerIndex index = indexOf({ high });
        Discovery d;
        EXPECT_EQ(d.visit(index, kWorld, 0, 0, 4.9f), 0u);
        EXPECT_EQ(d.visit(index, kWorld, 0, 0, 5.f), 1u);
    }

    TEST(SkyrimMapDiscovery, aBadPositionOrRadiusDiscoversNothing)
    {
        const MarkerIndex index = indexOf({ m(1, kWorld, 0, 0) });
        Discovery d;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        EXPECT_EQ(d.visit(index, kWorld, nan, 0, 100.f), 0u);
        EXPECT_EQ(d.visit(index, kWorld, 0, nan, 100.f), 0u);
        EXPECT_EQ(d.visit(index, kWorld, 0, 0, nan), 0u);
        EXPECT_EQ(d.visit(index, kWorld, 0, 0, -1.f), 0u);
        EXPECT_EQ(d.count(), 0u);
    }

    TEST(SkyrimMapDiscovery, aVisibleFlagShowsAMarkerFromTheStart)
    {
        const Marker visible = m(1, kWorld, 0, 0, MarkerFlags::Visible);
        const Marker hidden = m(2, kWorld, 0, 0, 0);
        Discovery d;
        EXPECT_TRUE(d.shown(visible));
        EXPECT_FALSE(d.shown(hidden));
        const MarkerIndex index = indexOf({ visible, hidden });
        (void)d.visit(index, kWorld, 0, 0, 10.f);
        EXPECT_TRUE(d.shown(hidden));
    }

    TEST(SkyrimMapDiscovery, restoreSortsRemovesDuplicatesAndCaps)
    {
        Discovery d;
        d.restore({ 5, 3, 5, 9 });
        ASSERT_EQ(d.found().size(), 3u);
        EXPECT_EQ(d.found()[0], 3u);
        EXPECT_EQ(d.found()[2], 9u);
        std::vector<FormId> many;
        for (FormId i = 0; i < kMaxDiscovered + 10; ++i)
            many.push_back(i + 1);
        d.restore(many);
        EXPECT_EQ(d.count(), kMaxDiscovered);
    }

    // ---- queries --------------------------------------------------------------------------------------------------

    TEST(SkyrimMapQuery, nearestFirstThenByReferenceAndOnlyShownOrHinted)
    {
        const MarkerIndex index = indexOf({ m(10, kWorld, 300, 0, MarkerFlags::Visible), m(11, kWorld, 100, 0, MarkerFlags::Visible),
            m(12, kWorld, 100, 0, MarkerFlags::Visible), m(13, kWorld, 200, 0, 0), m(14, kWorld, 9000, 0, 0),
            m(15, kWorld, 20000, 0, MarkerFlags::Visible) });
        Discovery d;
        std::vector<View> out;
        query(index, d, kWorld, 0, 0, 16384.f, 1000.f, 0, 256, out);
        // 15 is shown but past the range, 14 is hidden and past the hint range; 13 is hidden and inside it
        ASSERT_EQ(out.size(), 4u);
        EXPECT_EQ(out[0].mMarker->mRef, 11u);
        EXPECT_EQ(out[1].mMarker->mRef, 12u); // ties by reference
        EXPECT_EQ(out[2].mMarker->mRef, 13u);
        EXPECT_FALSE(out[2].mDiscovered); // hinted, not discovered
        EXPECT_EQ(out[3].mMarker->mRef, 10u);
        EXPECT_TRUE(out[3].mDiscovered);
    }

    TEST(SkyrimMapQuery, aPageSkipsTheEarlierOnes)
    {
        std::vector<Marker> markers;
        for (FormId i = 1; i <= 600; ++i)
            markers.push_back(m(i, kWorld, static_cast<float>(i), 0, MarkerFlags::Visible));
        const MarkerIndex index = indexOf(markers);
        Discovery d;
        std::vector<View> out;
        query(index, d, kWorld, 0, 0, 1e9f, 0.f, kPageSize * 2, kPageSize, out);
        ASSERT_EQ(out.size(), 600u - 2 * kPageSize);
        EXPECT_EQ(out.front().mMarker->mRef, 2 * kPageSize + 1);
        query(index, d, kWorld, 0, 0, 1e9f, 0.f, 0, kPageSize, out);
        EXPECT_EQ(out.size(), kPageSize);
    }

    TEST(SkyrimMapQuery, severalWorldsAreMergedNearestFirst)
    {
        const MarkerIndex index = indexOf({ m(1, kWorld, 500, 0, MarkerFlags::Visible),
            m(2, kWorld + 1, 100, 0, MarkerFlags::Visible), m(3, kWorld + 2, 50, 0, MarkerFlags::Visible) });
        Discovery d;
        std::vector<View> out;
        const FormId worlds[] = { kWorld, kWorld + 1 };
        query(index, d, worlds, 0, 0, 16384.f, 0.f, 0, 256, out);
        ASSERT_EQ(out.size(), 2u);
        EXPECT_EQ(out[0].mMarker->mRef, 2u);
        EXPECT_EQ(out[1].mMarker->mRef, 1u);
        EXPECT_EQ(d.visit(index, std::span<const FormId>(worlds), 0, 0, 200.f), 1u);
        EXPECT_TRUE(d.discovered(2));
    }

    TEST(SkyrimMapQuery, anotherWorldGivesNothing)
    {
        const MarkerIndex index = indexOf({ m(1, kWorld, 0, 0, MarkerFlags::Visible) });
        Discovery d;
        std::vector<View> out;
        query(index, d, kWorld + 1, 0, 0, 1e9f, 1e9f, 0, 256, out);
        EXPECT_TRUE(out.empty());
    }

    // ---- quest targets ----------------------------------------------------------------------------------------------

    constexpr FormId kQuest = kSelf | 0x400;
    constexpr FormId kQuest2 = kSelf | 0x401;
    constexpr FormId kRefA = kSelf | 0x500;
    constexpr FormId kRefB = kSelf | 0x501;
    constexpr FormId kNpc = kSelf | 0x510;
    constexpr FormId kRefNpc = kSelf | 0x502;
    constexpr FormId kRefNpcOff = kSelf | 0x503;
    constexpr FormId kCellExt = kSelf | 0x600;
    constexpr FormId kCellInt = kSelf | 0x601;

    QuestRecord quest(FormId id, std::uint16_t flags, std::vector<QuestObjective> objectives,
        std::vector<QuestAlias> aliases)
    {
        QuestRecord q;
        q.mCommon.mId = id;
        q.mFlags = flags;
        q.mObjectives = std::move(objectives);
        q.mAliases = std::move(aliases);
        return q;
    }

    QuestObjective objective(std::uint16_t index, const std::string& text, std::vector<std::uint32_t> aliases)
    {
        QuestObjective o;
        o.mIndex = index;
        o.mText.mPresent = true;
        o.mText.mText = text;
        for (const std::uint32_t a : aliases)
        {
            QuestObjectiveTarget t;
            t.mAlias = a;
            o.mTargets.push_back(t);
        }
        return o;
    }

    QuestAlias alias(std::uint32_t id, AliasFillKind kind, FormId reference, FormId actor = kNullFormId)
    {
        QuestAlias a;
        a.mId = id;
        a.mFill = kind;
        a.mReference = reference;
        a.mActor = actor;
        return a;
    }

    PlacedEntry placedRef(FormId id, FormId base, FormId cell, float x, float y, std::uint8_t flags = 0)
    {
        PlacedEntry e;
        e.mId = id;
        e.mBase = base;
        e.mCell = cell;
        e.mX = x;
        e.mY = y;
        e.mFlags = flags;
        return e;
    }

    struct TargetEnv
    {
        QuestIndex mQuests;
        QuestState mState;
        PlacedRefIndex mPlaced;
        std::map<std::pair<FormId, std::uint16_t>, journal::ObjectiveFlags> mFlags;

        TargetEnv()
        {
            QuestRecords records;
            records.mQuests.push_back(quest(kQuest, QuestFlags::StartGameEnabled,
                { objective(10, "Go to the reference", { 1 }), objective(20, "Find the NPC", { 2 }),
                    objective(30, "Not displayed", { 1 }), objective(40, "Done already", { 1 }),
                    objective(50, "In a cave", { 3 }), objective(60, "Nobody fills it", { 4 }) },
                { alias(1, AliasFillKind::SpecificReference, kRefA), alias(2, AliasFillKind::UniqueActor, kNullFormId, kNpc),
                    alias(3, AliasFillKind::SpecificReference, kRefB), alias(4, AliasFillKind::FindMatching, kNullFormId) }));
            records.mQuests.push_back(quest(kQuest2, 0, { objective(10, "Not running", { 1 }) },
                { alias(1, AliasFillKind::SpecificReference, kRefA) }));
            QuestRecords wrapped;
            wrapped.mQuests = records.mQuests;
            LoadOrder o = order();
            mQuests.add(std::move(wrapped), FormIdResolver(Content::Header(), "test.esp", o));
            mState = QuestState::initial(mQuests);
            PlacedRefRecords refs;
            refs.mRefs = { placedRef(kRefA, 1, kCellExt, 100, 200), placedRef(kRefB, 1, kCellInt, 7, 8),
                placedRef(kRefNpcOff, kNpc, kCellExt, 1, 1, PlacedFlags::Disabled),
                placedRef(kRefNpc, kNpc, kCellExt, 300, 400, PlacedFlags::IsActor) };
            mPlaced.add(std::move(refs));
            mPlaced.finish();
            mFlags[{ kQuest, 10 }] = { true, false };
            mFlags[{ kQuest, 20 }] = { true, false };
            mFlags[{ kQuest, 30 }] = { false, false };
            mFlags[{ kQuest, 40 }] = { true, true };
            mFlags[{ kQuest, 50 }] = { true, false };
            mFlags[{ kQuest, 60 }] = { true, false };
            mFlags[{ kQuest2, 10 }] = { true, false };
        }

        TargetInputs inputs()
        {
            TargetInputs in;
            in.mQuests = &mQuests;
            in.mState = &mState;
            in.mPlaced = &mPlaced;
            in.mObjective = [this](FormId q, std::uint16_t i) {
                const auto it = mFlags.find({ q, i });
                return it == mFlags.end() ? journal::ObjectiveFlags() : it->second;
            };
            in.mText = [](const LString& s, std::uint32_t, StringTableKind) { return s.mText; };
            in.mCellWorld = [](FormId cell, FormId& world, bool& interior) {
                if (cell == kCellExt)
                {
                    world = kWorld;
                    interior = false;
                    return true;
                }
                if (cell == kCellInt)
                {
                    world = kNullFormId;
                    interior = true;
                    return true;
                }
                return false;
            };
            return in;
        }
    };

    TEST(SkyrimMapTargets, aDisplayedUncompletedObjectiveOfARunningQuestPointsAtItsAlias)
    {
        TargetEnv env;
        std::vector<Target> out;
        buildTargets(env.inputs(), out);
        // objective 10 (reference), 20 (the unique actor's first enabled reference), 50 (an interior reference)
        ASSERT_EQ(out.size(), 3u);
        EXPECT_EQ(out[0].mObjective, 10);
        EXPECT_FLOAT_EQ(out[0].mX, 100.f);
        EXPECT_FLOAT_EQ(out[0].mY, 200.f);
        EXPECT_EQ(out[0].mWorld, kWorld);
        EXPECT_EQ(out[0].mText, "Go to the reference");
        EXPECT_EQ(out[1].mObjective, 20);
        EXPECT_FLOAT_EQ(out[1].mX, 300.f); // the disabled placement is skipped
    }

    TEST(SkyrimMapTargets, anInteriorTargetHasNoWorld)
    {
        TargetEnv env;
        std::vector<Target> out;
        buildTargets(env.inputs(), out);
        ASSERT_EQ(out.size(), 3u);
        EXPECT_EQ(out[2].mObjective, 50);
        EXPECT_EQ(out[2].mWorld, kNullFormId);
        EXPECT_EQ(out[2].mCell, kCellInt);
    }

    TEST(SkyrimMapTargets, aQuestThatIsNotRunningAndAnAliasNothingFillsGiveNone)
    {
        TargetEnv env;
        std::vector<Target> out;
        buildTargets(env.inputs(), out);
        for (const Target& t : out)
        {
            EXPECT_NE(t.mQuest, kQuest2);
            EXPECT_NE(t.mObjective, 60);
        }
    }

    TEST(SkyrimMapTargets, withoutTheHooksThereAreNone)
    {
        std::vector<Target> out{ Target() };
        buildTargets(TargetInputs(), out);
        EXPECT_TRUE(out.empty());
    }

    // ---- the chunk --------------------------------------------------------------------------------------------------

    class MemoryWriter final : public openeden::sdk::ChunkWriter
    {
    public:
        void write(const void* data, std::size_t size) override
        {
            const auto* bytes = static_cast<const unsigned char*>(data);
            mBytes.insert(mBytes.end(), bytes, bytes + size);
        }
        std::vector<unsigned char> mBytes;
    };

    class MemoryReader final : public openeden::sdk::ChunkReader
    {
    public:
        explicit MemoryReader(const std::vector<unsigned char>& bytes)
            : mBytes(bytes)
        {
        }
        bool read(void* data, std::size_t size) override
        {
            if (size > remaining())
                return false;
            std::memcpy(data, mBytes.data() + mPos, size);
            mPos += size;
            return true;
        }
        std::size_t remaining() const override { return mBytes.size() - mPos; }

    private:
        const std::vector<unsigned char>& mBytes;
        std::size_t mPos = 0;
    };

    std::vector<unsigned char> chunkBytes(std::initializer_list<std::uint32_t> values)
    {
        std::vector<unsigned char> bytes;
        for (const std::uint32_t v : values)
        {
            unsigned char b[4];
            std::memcpy(b, &v, 4);
            bytes.insert(bytes.end(), b, b + 4);
        }
        return bytes;
    }

    TEST(SkyrimMapChunk, theDiscoveredMarkersRoundTrip)
    {
        Discovery d;
        d.restore({ 7, 3, 99 });
        MapChunk chunk(d);
        MemoryWriter writer;
        chunk.write(writer);
        Discovery fresh;
        MapChunk readBack(fresh);
        MemoryReader reader(writer.mBytes);
        ASSERT_TRUE(readBack.read(reader, MapChunk::kVersion));
        EXPECT_EQ(fresh.found(), d.found());
    }

    TEST(SkyrimMapChunk, aWrongVersionACountThatDoesNotFitAndUnsortedIdsAreRefusedAndChangeNothing)
    {
        Discovery d;
        d.restore({ 1 });
        MapChunk chunk(d);
        std::string reason;
        chunk.setRefusalSink([&](const std::string& r) { reason = r; });
        const auto expectRefused = [&](const std::vector<unsigned char>& bytes, std::uint32_t version) {
            MemoryReader reader(bytes);
            EXPECT_FALSE(chunk.read(reader, version));
            EXPECT_FALSE(reason.empty());
            reason.clear();
            ASSERT_EQ(d.found().size(), 1u);
            EXPECT_EQ(d.found()[0], 1u);
        };
        expectRefused(chunkBytes({ 1, 5 }), 2);
        expectRefused(chunkBytes({ 3, 5, 6 }), 1); // claims three, holds two
        expectRefused(chunkBytes({ 1, 5, 6 }), 1); // claims one, holds two
        expectRefused(chunkBytes({ 2, 6, 5 }), 1); // descending
        expectRefused(chunkBytes({ 2, 5, 5 }), 1); // repeated
        expectRefused(chunkBytes({ 0xFFFFFFFFu }), 1);
        expectRefused({}, 1);
    }

    TEST(SkyrimMapChunk, anEmptyChunkClearsTheState)
    {
        Discovery d;
        d.restore({ 1, 2 });
        MapChunk chunk(d);
        const std::vector<unsigned char> bytes = chunkBytes({ 0 });
        MemoryReader reader(bytes);
        ASSERT_TRUE(chunk.read(reader, 1));
        EXPECT_EQ(d.count(), 0u);
    }

    // ---- the service and its bindings -------------------------------------------------------------------------------

    class FakeBindings final : public openeden::sdk::BindingsService
    {
    public:
        openeden::sdk::DeclareResult declareNamespace(const openeden::sdk::NamespaceDescriptor& d) override
        {
            mName = std::string(d.name);
            mVersion = d.version;
            for (const auto& f : d.functions)
                mFunctions[std::string(f.name)] = f;
            return { true, {} };
        }
        openeden::sdk::BindingResult call(const std::string& name, std::vector<BindingValue> args = {})
        {
            const auto it = mFunctions.find(name);
            EXPECT_NE(it, mFunctions.end()) << name;
            mArgs = std::move(args);
            return it->second.handler->call(mArgs);
        }
        std::string mName;
        openeden::sdk::Version mVersion{ 0, 0 };
        std::map<std::string, openeden::sdk::FunctionDescriptor> mFunctions;
        std::vector<BindingValue> mArgs;
    };

    BindingValue i32(std::int32_t v)
    {
        BindingValue b;
        b.type = BindingType::Int32;
        b.integer = v;
        return b;
    }
    BindingValue dbl(double v)
    {
        BindingValue b;
        b.type = BindingType::Double;
        b.number = v;
        return b;
    }

    struct MapServiceFixture : testing::Test
    {
        TargetEnv mEnv;
        MarkerIndex mMarkers;
        std::unique_ptr<MapService> mService;
        FakeBindings mBindings;
        bool mAccessReady = true;
        std::uint64_t mObjectiveRevision = 0;
        std::vector<std::string> mLog;

        void SetUp() override
        {
            MarkerRecords records;
            records.mMarkers = { m(1, kWorld, 100, 0, MarkerFlags::Visible, 5), m(2, kWorld, 3000, 0, 0, 7),
                m(3, kWorld + 7, 0, 0, MarkerFlags::Visible, 1) };
            records.mMarkers[0].mName.mText = "Whiterun";
            records.mMarkers[1].mName.mText = "Riverwood";
            mMarkers.add(std::move(records));
            mMarkers.finish();
            Hooks hooks;
            hooks.mAccess = [this](Access& out) {
                if (!mAccessReady)
                    return false;
                out.mMarkers = &mMarkers;
                out.mPlaced = &mEnv.mPlaced;
                out.mQuests = &mEnv.mQuests;
                out.mState = &mEnv.mState;
                out.mText = [](const LString& s, std::uint32_t, StringTableKind) { return s.mText; };
                out.mCellWorld = mEnv.inputs().mCellWorld;
                out.mParentOf = [](FormId world) { return world == kWorld + 7 ? kWorld : kNullFormId; };
                out.mLandBounds = [](FormId, std::int32_t& minX, std::int32_t& minY, std::int32_t& w, std::int32_t& h) {
                    minX = -3;
                    minY = -2;
                    w = 6;
                    h = 4;
                    return true;
                };
                out.mLandRow = [](FormId, std::int32_t row, std::vector<std::int32_t>& px) {
                    px.assign(6, row);
                    return row >= 0 && row < 4;
                };
                return true;
            };
            hooks.mObjective = mEnv.inputs().mObjective;
            hooks.mObjectiveRevision = [this] { return mObjectiveRevision; };
            hooks.mLog = [this](const std::string& line) { mLog.push_back(line); };
            mService = std::make_unique<MapService>(std::move(hooks));
            ASSERT_TRUE(mService->declare(mBindings).ok);
        }

        std::vector<double> numbers(const openeden::sdk::BindingResult& r)
        {
            std::vector<double> out;
            for (std::size_t i = 0; i < r.value.count; ++i)
                out.push_back(r.value.elements[i].type == BindingType::Int32 ? r.value.elements[i].integer
                                                                              : r.value.elements[i].number);
            return out;
        }

        std::vector<BindingValue> queryArgs(FormId world, double x, double y, double range, double hint, int page = 0)
        {
            return { i32(static_cast<std::int32_t>(world)), dbl(x), dbl(y), dbl(range), dbl(hint), i32(page) };
        }
    };

    TEST_F(MapServiceFixture, theNamespaceIsDeclaredWithItsVersionAndFunctions)
    {
        EXPECT_EQ(mBindings.mName, "skyrimse.map");
        EXPECT_EQ(mBindings.mVersion.major, 1);
        for (const char* name : { "ready", "revision", "marker_x", "marker_y", "marker_type", "marker_discovered",
                 "marker_name", "target_x", "target_y", "target_world", "target_text", "raster_info", "raster_row",
                 "visit" })
            EXPECT_TRUE(mBindings.mFunctions.count(name)) << name;
        EXPECT_EQ(mBindings.mFunctions["visit"].access, openeden::sdk::BindingAccess::Mutate);
        EXPECT_EQ(mBindings.mFunctions["marker_x"].access, openeden::sdk::BindingAccess::Read);
    }

    TEST_F(MapServiceFixture, nothingIsReadyUntilTheAccessIs)
    {
        mAccessReady = false;
        EXPECT_FALSE(mBindings.call("ready").value.boolean);
        EXPECT_EQ(mBindings.call("revision").value.integer, 0);
        EXPECT_EQ(mBindings.call("marker_x", queryArgs(kWorld, 0, 0, 1e9, 1e9)).value.count, 0u);
        EXPECT_EQ(mBindings.call("target_x").value.count, 0u);
        mAccessReady = true;
        EXPECT_TRUE(mBindings.call("ready").value.boolean);
        EXPECT_GT(mBindings.call("revision").value.integer, 0);
    }

    TEST_F(MapServiceFixture, markersComeAsParallelArraysInTheSameOrder)
    {
        const auto args = queryArgs(kWorld, 0, 0, 16384, 8192);
        EXPECT_EQ(numbers(mBindings.call("marker_x", args)), (std::vector<double>{ 100, 3000 }));
        EXPECT_EQ(numbers(mBindings.call("marker_y", args)), (std::vector<double>{ 0, 0 }));
        EXPECT_EQ(numbers(mBindings.call("marker_type", args)), (std::vector<double>{ 5, 7 }));
        const auto found = mBindings.call("marker_discovered", args);
        ASSERT_EQ(found.value.count, 2u);
        EXPECT_TRUE(found.value.elements[0].boolean);
        EXPECT_FALSE(found.value.elements[1].boolean);
    }

    TEST_F(MapServiceFixture, theNameOfAnUndiscoveredMarkerIsNotGiven)
    {
        const auto args = queryArgs(kWorld, 0, 0, 16384, 8192);
        const auto names = mBindings.call("marker_name", args);
        ASSERT_EQ(names.value.count, 2u);
        EXPECT_EQ(names.value.elements[0].text, "Whiterun");
        EXPECT_EQ(names.value.elements[1].text, "");
    }

    TEST_F(MapServiceFixture, visitingDiscoversAndChangesTheRevision)
    {
        const int before = mBindings.call("revision").value.integer;
        (void)mBindings.call("visit", { i32(static_cast<std::int32_t>(kWorld)), dbl(3000), dbl(0) });
        EXPECT_GT(mBindings.call("revision").value.integer, before);
        const auto found = mBindings.call("marker_discovered", queryArgs(kWorld, 0, 0, 16384, 8192));
        EXPECT_TRUE(found.value.elements[1].boolean);
        EXPECT_NE(std::find_if(mLog.begin(), mLog.end(), [](const std::string& l) { return l.find("MAP discovered") == 0; }),
            mLog.end());
    }

    TEST_F(MapServiceFixture, aWorldAnswersForItselfAndTheParentsItBorrowsFrom)
    {
        mService->poll();
        EXPECT_EQ(mService->markerWorlds(kWorld), (std::vector<FormId>{ kWorld }));
        EXPECT_EQ(mService->markerWorlds(kWorld + 7), (std::vector<FormId>{ kWorld + 7, kWorld }));
        // markers of both worlds come in one answer, nearest first
        const auto args = queryArgs(kWorld + 7, 0, 0, 16384, 8192);
        EXPECT_EQ(numbers(mBindings.call("marker_x", args)), (std::vector<double>{ 0, 100, 3000 }));
        // and a visit discovers in both
        (void)mBindings.call("visit", { i32(static_cast<std::int32_t>(kWorld + 7)), dbl(3000), dbl(0) });
        EXPECT_TRUE(mService->discovery().discovered(2));
    }

    TEST_F(MapServiceFixture, theTargetsFollowTheQuestStateAndTheObjectiveFlags)
    {
        EXPECT_EQ(mBindings.call("target_x").value.count, 3u);
        const int before = mBindings.call("revision").value.integer;
        mEnv.mFlags[{ kQuest, 10 }] = { true, true }; // completed
        ++mObjectiveRevision;
        EXPECT_GT(mBindings.call("revision").value.integer, before);
        const auto xs = mBindings.call("target_x");
        EXPECT_EQ(xs.value.count, 2u);
        const auto ws = mBindings.call("target_world");
        EXPECT_EQ(ws.value.elements[0].integer, static_cast<std::int32_t>(kWorld));
        const auto texts = mBindings.call("target_text");
        EXPECT_EQ(texts.value.elements[0].text, "Find the NPC");
    }

    TEST_F(MapServiceFixture, theRasterIsHandedOutARowAtATime)
    {
        const auto info = mBindings.call("raster_info", { i32(static_cast<std::int32_t>(kWorld)) });
        EXPECT_EQ(numbers(info), (std::vector<double>{ -3, -2, 6, 4 }));
        const auto row = mBindings.call("raster_row", { i32(static_cast<std::int32_t>(kWorld)), i32(2) });
        EXPECT_EQ(numbers(row), (std::vector<double>{ 2, 2, 2, 2, 2, 2 }));
        EXPECT_EQ(mBindings.call("raster_row", { i32(static_cast<std::int32_t>(kWorld)), i32(9) }).value.count, 0u);
    }

    TEST_F(MapServiceFixture, theRevisionOfAnUntouchedServiceDoesNotMove)
    {
        const int a = mBindings.call("revision").value.integer;
        (void)mBindings.call("marker_x", queryArgs(kWorld, 0, 0, 1e9, 1e9));
        (void)mBindings.call("target_x");
        EXPECT_EQ(mBindings.call("revision").value.integer, a);
    }
}
