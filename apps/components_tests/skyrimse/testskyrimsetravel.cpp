// Task 0218 (milestone M-S7): fast travel from the world map in a Skyrim SE session (game_packs/skyrimse travel.*): the
// rules, the destination, the time, the service over the map's markers and the bindings `skyrimse.travel`; plus the XLKR
// of a map marker. Generated plugins and hand-made indexes; no game data.
#include <game_skyrimse/travel.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using namespace game_skyrimse::travel;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    constexpr double kPi = 3.14159265358979323846;
    constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
    constexpr double kInf = std::numeric_limits<double>::infinity();

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kWorld = kSelf | 0x100;
    constexpr FormId kChild = kSelf | 0x107; // a world that borrows its land (and so its markers) from kWorld
    constexpr FormId kOtherWorld = kSelf | 0x108;
    constexpr FormId kOutdoors = kSelf | 0x101; // a cell of kWorld
    constexpr FormId kIndoors = kSelf | 0x102; // an interior cell
    constexpr FormId kMarkerBase = 0x10;

    // ---- the rules --------------------------------------------------------------------------------------------------

    TEST(SkyrimTravelRules, theFirstReasonThatHoldsWinsInTheDocumentedOrder)
    {
        Inputs ok;
        ok.mReady = true;
        ok.mKnown = true;
        ok.mDiscovered = true;
        ok.mHasDestination = true;
        EXPECT_EQ(decideTravel(ok), Refusal::None);

        Inputs all = ok;
        all.mInterior = true;
        all.mInCombat = true;
        all.mEnemiesNear = true;
        all.mKnown = false;
        all.mDiscovered = false;
        all.mHasDestination = false;
        all.mReady = false;
        EXPECT_EQ(decideTravel(all), Refusal::NotReady);
        all.mReady = true;
        EXPECT_EQ(decideTravel(all), Refusal::Interior);
        all.mInterior = false;
        EXPECT_EQ(decideTravel(all), Refusal::UnknownMarker);
        all.mKnown = true;
        EXPECT_EQ(decideTravel(all), Refusal::NotDiscovered);
        all.mDiscovered = true;
        EXPECT_EQ(decideTravel(all), Refusal::InCombat);
        all.mInCombat = false;
        EXPECT_EQ(decideTravel(all), Refusal::EnemiesNear);
        all.mEnemiesNear = false;
        EXPECT_EQ(decideTravel(all), Refusal::NoDestination);
        all.mHasDestination = true;
        EXPECT_EQ(decideTravel(all), Refusal::None);
    }

    TEST(SkyrimTravelRules, refusalsHaveStableLowerCaseNames)
    {
        EXPECT_EQ(refusalName(Refusal::None), "");
        EXPECT_EQ(refusalName(Refusal::NotReady), "not_ready");
        EXPECT_EQ(refusalName(Refusal::Interior), "interior");
        EXPECT_EQ(refusalName(Refusal::UnknownMarker), "unknown");
        EXPECT_EQ(refusalName(Refusal::NotDiscovered), "undiscovered");
        EXPECT_EQ(refusalName(Refusal::InCombat), "combat");
        EXPECT_EQ(refusalName(Refusal::EnemiesNear), "enemies");
        EXPECT_EQ(refusalName(Refusal::NoDestination), "no_destination");
    }

    TEST(SkyrimTravelRules, aGameHourCoversAFixedDistanceAndTheTimeIsCut)
    {
        EXPECT_EQ(travelHours(0), 1);
        EXPECT_EQ(travelHours(1), 1);
        EXPECT_EQ(travelHours(kUnitsPerGameHour), 1);
        EXPECT_EQ(travelHours(kUnitsPerGameHour + 1), 2);
        EXPECT_EQ(travelHours(10.0 * kUnitsPerGameHour), 10);
        EXPECT_EQ(travelHours(1e12), kMaxTravelHours);
        EXPECT_EQ(travelHours(-5), 1);
        EXPECT_EQ(travelHours(kNan), 1);
        EXPECT_EQ(travelHours(kInf), 1); // not a number of hours: the minimum, as every other bad number
    }

    TEST(SkyrimTravelRules, theHeadingFollowsTheEnginesConvention)
    {
        EXPECT_NEAR(yawToward(0, 0, 0, 10), 0.0, 1e-9); // +Y is 0
        EXPECT_NEAR(yawToward(0, 0, 10, 0), kPi / 2, 1e-9); // clockwise: east is +pi/2
        EXPECT_NEAR(yawToward(0, 0, 0, -10), kPi, 1e-9);
        EXPECT_NEAR(yawToward(0, 0, -10, 0), -kPi / 2, 1e-9);
        EXPECT_NEAR(yawToward(5, 5, 5, 5), 0.0, 1e-9);
        EXPECT_NEAR(yawToward(kNan, 0, 1, 1), 0.0, 1e-9);
        EXPECT_NEAR(yawToward(0, 0, kInf, 1), 0.0, 1e-9);
    }

    // ---- the destination --------------------------------------------------------------------------------------------

    map::Marker markerAt(FormId ref, FormId world, float x, float y, float z = 0.f)
    {
        map::Marker marker;
        marker.mRef = ref;
        marker.mWorld = world;
        marker.mX = x;
        marker.mY = y;
        marker.mZ = z;
        marker.mName.mPresent = true;
        marker.mName.mText = "M" + std::to_string(ref & 0xFFF);
        return marker;
    }

    PlacedEntry placedAt(FormId id, FormId cell, float x, float y, float z)
    {
        PlacedEntry e;
        e.mId = id;
        e.mCell = cell;
        e.mX = x;
        e.mY = y;
        e.mZ = z;
        return e;
    }

    TEST(SkyrimTravelDestination, aUsableLinkedReferenceIsWhereTheTravelEndsFacingTheMarker)
    {
        const map::Marker marker = markerAt(kSelf | 0x200, kWorld, 0, 1000, 5);
        const PlacedEntry link = placedAt(kSelf | 0x500, kOutdoors, 0, 0, 77);
        const Destination d = chooseDestination(marker, &link, true);
        EXPECT_TRUE(d.mFromLink);
        EXPECT_FLOAT_EQ(d.mX, 0.f);
        EXPECT_FLOAT_EQ(d.mY, 0.f);
        EXPECT_FLOAT_EQ(d.mZ, 77.f);
        EXPECT_NEAR(d.mYaw, 0.0, 1e-9); // the marker is due north of the landing
    }

    TEST(SkyrimTravelDestination, aLinkThatIsNotUsableOrNotThereLeavesTheMarkerItself)
    {
        const map::Marker marker = markerAt(kSelf | 0x200, kWorld, 300, 400, 5);
        const PlacedEntry link = placedAt(kSelf | 0x500, kIndoors, 1, 2, 3);
        const Destination notUsable = chooseDestination(marker, &link, false);
        const Destination none = chooseDestination(marker, nullptr, true);
        for (const Destination& d : { notUsable, none })
        {
            EXPECT_FALSE(d.mFromLink);
            EXPECT_FLOAT_EQ(d.mX, 300.f);
            EXPECT_FLOAT_EQ(d.mY, 400.f);
            EXPECT_FLOAT_EQ(d.mZ, 5.f);
            EXPECT_NEAR(d.mYaw, 0.0, 1e-9);
        }
    }

    // ---- the service ------------------------------------------------------------------------------------------------

    struct TravelEnv
    {
        map::MarkerIndex mMarkers;
        PlacedRefIndex mPlaced;
        std::unique_ptr<map::MapService> mMap;
        bool mAccessReady = true;

        TravelEnv()
        {
            map::MarkerRecords records;
            records.mMarkers.push_back(markerAt(kSelf | 0x200, kWorld, 0, 0, 10)); // linked, to the east
            records.mMarkers.back().mLinked = kSelf | 0x500;
            records.mMarkers.push_back(markerAt(kSelf | 0x201, kWorld, 90000, 0, 20)); // far, no link, can travel
            records.mMarkers.back().mFlags = map::MarkerFlags::CanTravel;
            records.mMarkers.push_back(markerAt(kSelf | 0x202, kWorld, 3000, 0, 30)); // near the first, link is indoors
            records.mMarkers.back().mLinked = kSelf | 0x501;
            records.mMarkers.push_back(markerAt(kSelf | 0x203, kChild, 5000, 5000, 40)); // in the child world
            records.mMarkers.push_back(markerAt(kSelf | 0x204, kOtherWorld, 1, 1, 1));
            mMarkers.add(std::move(records));
            mMarkers.finish();
            PlacedRefRecords placed;
            placed.mRefs.push_back(placedAt(kSelf | 0x500, kOutdoors, 1000, 0, 55));
            placed.mRefs.push_back(placedAt(kSelf | 0x501, kIndoors, 1, 1, 1));
            mPlaced.add(std::move(placed));
            mPlaced.finish();
            map::Hooks hooks;
            hooks.mAccess = [this](map::Access& out) {
                if (!mAccessReady)
                    return false;
                out.mMarkers = &mMarkers;
                out.mPlaced = &mPlaced;
                out.mCellWorld = [](FormId cell, FormId& world, bool& interior) {
                    if (cell == kOutdoors)
                    {
                        world = kWorld;
                        interior = false;
                        return true;
                    }
                    if (cell == kIndoors)
                    {
                        world = kNullFormId;
                        interior = true;
                        return true;
                    }
                    return false;
                };
                out.mParentOf = [](FormId world) { return world == kChild ? kWorld : kNullFormId; };
                return true;
            };
            mMap = std::make_unique<map::MapService>(std::move(hooks));
        }
    };

    struct TravelServiceTest : testing::Test
    {
        TravelEnv mEnv;
        bool mReady = true;
        bool mCombat = false;
        bool mEnemies = false;
        std::size_t mFollowers = 0;
        std::vector<std::string> mLog;
        std::unique_ptr<TravelService> mService;

        void SetUp() override
        {
            Hooks hooks;
            hooks.mReady = [this] { return mReady; };
            hooks.mInCombat = [this] { return mCombat; };
            hooks.mEnemiesNear = [this] { return mEnemies; };
            hooks.mFollowerCount = [this] { return mFollowers; };
            hooks.mLog = [this](const std::string& line) { mLog.push_back(line); };
            mService = std::make_unique<TravelService>(*mEnv.mMap, std::move(hooks));
        }

        bool logged(const std::string& needle) const
        {
            for (const std::string& line : mLog)
                if (line.find(needle) != std::string::npos)
                    return true;
            return false;
        }
    };

    TEST_F(TravelServiceTest, thePlanIsTheLandingOfALinkedMarkerWithTheTimeFromWhereThePlayerStands)
    {
        Plan plan;
        ASSERT_TRUE(mService->plan(kWorld, kSelf | 0x200, 0, 0, plan));
        EXPECT_TRUE(plan.mWhere.mFromLink);
        EXPECT_FLOAT_EQ(plan.mWhere.mX, 1000.f);
        EXPECT_FLOAT_EQ(plan.mWhere.mZ, 55.f);
        EXPECT_NEAR(plan.mWhere.mYaw, -kPi / 2, 1e-9); // the marker is west of the landing
        EXPECT_NEAR(plan.mDistance, 1000.0, 1e-6);
        EXPECT_EQ(plan.mHours, 1);

        ASSERT_TRUE(mService->plan(kWorld, kSelf | 0x201, 0, 0, plan)); // no link: the marker itself
        EXPECT_FALSE(plan.mWhere.mFromLink);
        EXPECT_FLOAT_EQ(plan.mWhere.mX, 90000.f);
        EXPECT_NEAR(plan.mDistance, 90000.0, 1e-6);
        EXPECT_EQ(plan.mHours, 10);
    }

    TEST_F(TravelServiceTest, aLinkInAnInteriorGivesTheMarkerItself)
    {
        Plan plan;
        ASSERT_TRUE(mService->plan(kWorld, kSelf | 0x202, 0, 0, plan));
        EXPECT_FALSE(plan.mWhere.mFromLink);
        EXPECT_FLOAT_EQ(plan.mWhere.mX, 3000.f);
    }

    TEST_F(TravelServiceTest, aMarkerOfAnotherWorldOrNoneHasNoPlan)
    {
        Plan plan;
        EXPECT_FALSE(mService->plan(kWorld, kSelf | 0x204, 0, 0, plan));
        EXPECT_FALSE(mService->plan(kWorld, kSelf | 0x999, 0, 0, plan));
        EXPECT_FALSE(mService->plan(kWorld, kNullFormId, 0, 0, plan));
    }

    TEST_F(TravelServiceTest, aWorldThatBorrowsItsLandAnswersForTheMarkersOfItsParentNotTheOtherWayRound)
    {
        Plan plan;
        EXPECT_TRUE(mService->plan(kChild, kSelf | 0x200, 0, 0, plan));
        EXPECT_TRUE(mService->plan(kChild, kSelf | 0x203, 0, 0, plan));
        EXPECT_FALSE(mService->plan(kWorld, kSelf | 0x203, 0, 0, plan)); // the parent does not borrow from the child
    }

    TEST_F(TravelServiceTest, anUndiscoveredMarkerWithoutTheCanTravelBitIsRefusedAndADiscoveredOneIsNot)
    {
        EXPECT_EQ(mService->refusal(kWorld, false, kSelf | 0x200, 0, 0), Refusal::NotDiscovered);
        EXPECT_EQ(mService->refusal(kWorld, false, kSelf | 0x201, 0, 0), Refusal::None); // marked can travel
        (void)mService->commit(kWorld, false, kSelf | 0x201, 0, 0); // discovers around x 90000, not the marker at 0
        EXPECT_EQ(mService->refusal(kWorld, false, kSelf | 0x200, 0, 0), Refusal::NotDiscovered);
        (void)mEnv.mMap->visit(kWorld, 0, 0);
        EXPECT_EQ(mService->refusal(kWorld, false, kSelf | 0x200, 0, 0), Refusal::None);
    }

    TEST_F(TravelServiceTest, everyRefusalComesFromItsOwnCondition)
    {
        (void)mEnv.mMap->visit(kWorld, 0, 0); // discovers 0x200 and 0x202
        mReady = false;
        EXPECT_EQ(mService->refusal(kWorld, false, kSelf | 0x200, 0, 0), Refusal::NotReady);
        mReady = true;
        EXPECT_EQ(mService->refusal(kWorld, true, kSelf | 0x200, 0, 0), Refusal::Interior);
        EXPECT_EQ(mService->refusal(kWorld, false, kSelf | 0x204, 0, 0), Refusal::UnknownMarker);
        mCombat = true;
        EXPECT_EQ(mService->refusal(kWorld, false, kSelf | 0x200, 0, 0), Refusal::InCombat);
        mCombat = false;
        mEnemies = true;
        EXPECT_EQ(mService->refusal(kWorld, false, kSelf | 0x200, 0, 0), Refusal::EnemiesNear);
        mEnemies = false;
        EXPECT_EQ(mService->refusal(kWorld, false, kSelf | 0x200, 0, 0), Refusal::None);
    }

    TEST_F(TravelServiceTest, theMapNotBeingReadYetIsNotReady)
    {
        mEnv.mAccessReady = false;
        EXPECT_EQ(mService->refusal(kWorld, false, kSelf | 0x200, 0, 0), Refusal::NotReady);
        mEnv.mAccessReady = true;
        EXPECT_NE(mService->refusal(kWorld, false, kSelf | 0x200, 0, 0), Refusal::NotReady);
    }

    TEST_F(TravelServiceTest, aRefusedCommitChangesNothingAndIsCounted)
    {
        (void)mEnv.mMap->visit(kWorld, 0, 0);
        const std::size_t found = mEnv.mMap->discovery().count();
        mCombat = true;
        EXPECT_FALSE(mService->commit(kWorld, false, kSelf | 0x201, 0, 0));
        EXPECT_EQ(mService->performed(), 0u);
        EXPECT_EQ(mService->refused(), 1u);
        EXPECT_EQ(mEnv.mMap->discovery().count(), found);
        EXPECT_TRUE(logged("TRAVEL refused: combat"));
    }

    TEST_F(TravelServiceTest, aCommitDiscoversWhatLiesAroundTheArrivalAndIsLogged)
    {
        mFollowers = 2;
        EXPECT_FALSE(mEnv.mMap->discovery().discovered(kSelf | 0x201));
        ASSERT_TRUE(mService->commit(kWorld, false, kSelf | 0x201, 0, 0));
        EXPECT_EQ(mService->performed(), 1u);
        EXPECT_TRUE(mEnv.mMap->discovery().discovered(kSelf | 0x201)); // the arrival is next to it
        EXPECT_FALSE(mEnv.mMap->discovery().discovered(kSelf | 0x200)); // 90 km away
        EXPECT_TRUE(logged("TRAVEL to M513"));
        EXPECT_TRUE(logged("10 hours"));
        EXPECT_TRUE(logged("2 followers"));
    }

    TEST_F(TravelServiceTest, travelFromAnInteriorIsRefusedEvenToADiscoveredMarker)
    {
        (void)mEnv.mMap->visit(kWorld, 0, 0);
        EXPECT_FALSE(mService->commit(kWorld, true, kSelf | 0x200, 0, 0));
        EXPECT_TRUE(logged("TRAVEL refused: interior"));
    }

    // ---- the bindings -----------------------------------------------------------------------------------------------

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
    BindingValue flag(bool v)
    {
        BindingValue b;
        b.type = BindingType::Bool;
        b.boolean = v;
        return b;
    }
    BindingValue str(const std::string& v)
    {
        BindingValue b;
        b.type = BindingType::String;
        b.text = v;
        return b;
    }

    struct TravelBindingsTest : TravelServiceTest
    {
        FakeBindings mBindings;
        void SetUp() override
        {
            TravelServiceTest::SetUp();
            ASSERT_TRUE(mService->declare(mBindings).ok);
        }
        std::int32_t world() const { return static_cast<std::int32_t>(kWorld); }
        std::vector<std::string> texts(const openeden::sdk::BindingResult& r)
        {
            std::vector<std::string> out;
            for (std::size_t i = 0; i < r.value.count; ++i)
                out.emplace_back(r.value.elements[i].text);
            return out;
        }
        std::string refusalOf(std::int32_t w, bool interior, const std::string& marker)
        {
            return std::string(mBindings.call("refusal", { i32(w), flag(interior), str(marker), dbl(0), dbl(0) })
                                   .value.text);
        }
    };

    TEST_F(TravelBindingsTest, theNamespaceIsDeclaredWithItsVersionAndFunctions)
    {
        EXPECT_EQ(mBindings.mName, "skyrimse.travel");
        EXPECT_EQ(mBindings.mVersion.major, 1);
        for (const char* name : { "ready", "marker_ref", "refusal", "plan", "follower_count", "commit" })
            EXPECT_TRUE(mBindings.mFunctions.count(name)) << name;
        EXPECT_EQ(mBindings.mFunctions["commit"].access, openeden::sdk::BindingAccess::Mutate);
        EXPECT_EQ(mBindings.mFunctions["refusal"].access, openeden::sdk::BindingAccess::Read);
        EXPECT_EQ(mBindings.mFunctions["plan"].access, openeden::sdk::BindingAccess::Read);
        EXPECT_EQ(mBindings.mFunctions["marker_ref"].access, openeden::sdk::BindingAccess::Read);
    }

    TEST_F(TravelBindingsTest, theMarkersOfAQueryComeAsEightDigitHexTextInTheMapsOrder)
    {
        (void)mEnv.mMap->visit(kWorld, 0, 0);
        const auto refs
            = texts(mBindings.call("marker_ref", { i32(world()), dbl(0), dbl(0), dbl(1e9), dbl(0), i32(0) }));
        ASSERT_GE(refs.size(), 2u);
        EXPECT_EQ(refs[0], "01000200"); // nearest first, like the map's own arrays
        EXPECT_EQ(refs[1], "01000202");
    }

    TEST_F(TravelBindingsTest, refusalAnswersByNameFromAHexMarker)
    {
        EXPECT_EQ(refusalOf(world(), false, "01000200"), "undiscovered");
        EXPECT_EQ(refusalOf(world(), false, "01000201"), "");
        EXPECT_EQ(refusalOf(world(), true, "01000201"), "interior");
        // text that is no marker is an unknown marker, not a crash
        for (const char* bad : { "", "zz", "123456789012345678", "01000999" })
            EXPECT_EQ(refusalOf(world(), false, bad), "unknown") << bad;
    }

    TEST_F(TravelBindingsTest, thePlanIsSixNumbersAndEmptyForAnUnknownMarker)
    {
        const auto r = mBindings.call("plan", { i32(world()), str("01000200"), dbl(0), dbl(0) });
        ASSERT_EQ(r.value.count, 6u);
        EXPECT_NEAR(r.value.elements[0].number, 1000.0, 1e-3); // x
        EXPECT_NEAR(r.value.elements[1].number, 0.0, 1e-3); // y
        EXPECT_NEAR(r.value.elements[2].number, 55.0, 1e-3); // z
        EXPECT_NEAR(r.value.elements[3].number, -kPi / 2, 1e-6); // yaw
        EXPECT_NEAR(r.value.elements[4].number, 1.0, 1e-9); // hours
        EXPECT_NEAR(r.value.elements[5].number, 1000.0, 1e-3); // distance
        EXPECT_EQ(mBindings.call("plan", { i32(world()), str("nothex"), dbl(0), dbl(0) }).value.count, 0u);
        EXPECT_EQ(mBindings.call("plan", { i32(world()), str("01000204"), dbl(0), dbl(0) }).value.count, 0u);
    }

    TEST_F(TravelBindingsTest, theFollowerCountIsTheHooksAndCommitRunsTheTravel)
    {
        mFollowers = 3;
        EXPECT_EQ(mBindings.call("follower_count").value.integer, 3);
        mBindings.call("commit", { i32(world()), flag(false), str("01000201"), dbl(0), dbl(0) });
        EXPECT_EQ(mService->performed(), 1u);
        mBindings.call("commit", { i32(world()), flag(false), str("garbage"), dbl(0), dbl(0) });
        EXPECT_EQ(mService->performed(), 1u);
        EXPECT_EQ(mService->refused(), 1u);
        mReady = false;
        EXPECT_FALSE(mBindings.call("ready").value.boolean);
    }

    // ---- the XLKR of a map marker -----------------------------------------------------------------------------------

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes linkedMarker(FormId id, const Bytes& xlkr, bool withLink = true)
    {
        Bytes data;
        data.append(sub4("NAME", Bytes().u32(kMarkerBase)));
        data.append(sub4("XMRK", Bytes()));
        data.append(sub4("FNAM", Bytes().u8(0)));
        data.append(sub4("FULL", Bytes().zstring("Somewhere")));
        data.append(sub4("TNAM", Bytes().u8(3).u8(0)));
        if (withLink)
            data.append(sub4("XLKR", xlkr));
        data.append(sub4("DATA", Bytes().f32(1).f32(2).f32(3).f32(0).f32(0).f32(0)));
        return record4("REFR", 0x400, id, data);
    }

    map::MarkerRecords parse(const Bytes& persistent, bool& ok)
    {
        Bytes worldChildren;
        worldChildren.append(record4("CELL", 0, kSelf | 0x110, sub4("EDID", Bytes().zstring("Persistent"))));
        worldChildren.append(group4(kSelf | 0x110, 6, group4(kSelf | 0x110, 8, persistent)));
        Bytes all = tes4Header(1.7f, 0, "A", "B", 4, { { "Skyrim.esm", 0 } }, 24, 44);
        all.append(group4(kWorld, 1, worldChildren));
        Content::Error error;
        const auto plugin = Content::Plugin::open(
            std::make_unique<std::istringstream>(all.str(), std::ios::binary), "test.esp", error);
        map::MarkerRecords out;
        ok = plugin != nullptr && map::parseMarkers(*plugin, order(), map::MarkerLimits(), out);
        return out;
    }

    TEST(SkyrimTravelMarkerLink, aNullKeywordLinkedReferenceIsTheLanding)
    {
        Bytes persistent;
        persistent.append(linkedMarker(kSelf | 0x300, Bytes().u32(0).u32(kSelf | 0x500)));
        persistent.append(linkedMarker(kSelf | 0x301, Bytes(), false));
        bool ok = false;
        const map::MarkerRecords r = parse(persistent, ok);
        ASSERT_TRUE(ok);
        ASSERT_EQ(r.mMarkers.size(), 2u);
        EXPECT_EQ(r.mMarkers[0].mLinked, kSelf | 0x500);
        EXPECT_EQ(r.mMarkers[1].mLinked, kNullFormId); // no XLKR: no landing
        EXPECT_EQ(r.mOddSubrecords, 0u);
    }

    TEST(SkyrimTravelMarkerLink, aLinkWithAKeywordIsAnotherKindOfLinkAndIsIgnored)
    {
        Bytes persistent;
        persistent.append(linkedMarker(kSelf | 0x300, Bytes().u32(kSelf | 0x77).u32(kSelf | 0x500)));
        bool ok = false;
        const map::MarkerRecords r = parse(persistent, ok);
        ASSERT_TRUE(ok);
        ASSERT_EQ(r.mMarkers.size(), 1u);
        EXPECT_EQ(r.mMarkers[0].mLinked, kNullFormId);
        EXPECT_EQ(r.mOddSubrecords, 0u);
    }

    TEST(SkyrimTravelMarkerLink, aLinkOfAnotherSizeIsOddAndDecodesToNothing)
    {
        Bytes persistent;
        persistent.append(linkedMarker(kSelf | 0x300, Bytes().u32(0)));
        bool ok = false;
        const map::MarkerRecords r = parse(persistent, ok);
        ASSERT_TRUE(ok);
        ASSERT_EQ(r.mMarkers.size(), 1u);
        EXPECT_EQ(r.mMarkers[0].mLinked, kNullFormId);
        EXPECT_EQ(r.mOddSubrecords, 1u);
    }
}
