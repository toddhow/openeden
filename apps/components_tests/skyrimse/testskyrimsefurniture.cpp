// Task 0135 stage S6b: furniture markers (the NIF reader, placement maths, entry points) and the package brain's
// furniture use (sleep, eat, sandbox sitting) over fake sources. No game data; the NIFs are generated.
#include <game_skyrimse/furniture.hpp>
#include <game_skyrimse/package_brain.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    namespace sdk = openeden::sdk::game;

    // ---- a generated NIF 20.2.0.7 with furniture marker blocks
    // ---------------------------------------------------------

    struct Bytes
    {
        std::vector<std::byte> v;
        template <class T>
        void put(T t)
        {
            std::byte b[sizeof(T)];
            std::memcpy(b, &t, sizeof(T));
            v.insert(v.end(), b, b + sizeof(T));
        }
        void text(const std::string& s)
        {
            for (char c : s)
                v.push_back(static_cast<std::byte>(c));
        }
    };

    struct NifBlock
    {
        std::string mType;
        std::vector<FurnitureMarker> mMarkers; // only for marker blocks
        std::uint32_t mPlainSize = 8; // other blocks: that many zero bytes
    };

    std::vector<std::byte> makeNif(
        const std::vector<NifBlock>& blocks, std::uint32_t version = 0x14020007u, std::uint32_t userVersion2 = 100)
    {
        std::vector<std::string> types;
        for (const NifBlock& b : blocks)
            if (std::find(types.begin(), types.end(), b.mType) == types.end())
                types.push_back(b.mType);
        std::vector<Bytes> bodies;
        for (const NifBlock& b : blocks)
        {
            Bytes body;
            if (b.mType == "BSFurnitureMarkerNode" || b.mType == "BSFurnitureMarker")
            {
                body.put<std::int32_t>(0); // name string index
                body.put<std::uint32_t>(static_cast<std::uint32_t>(b.mMarkers.size()));
                for (const FurnitureMarker& m : b.mMarkers)
                {
                    body.put(m.mOx);
                    body.put(m.mOy);
                    body.put(m.mOz);
                    body.put(m.mHeading);
                    body.put(m.mType);
                    body.put(m.mEntry);
                }
            }
            else
                for (std::uint32_t i = 0; i < b.mPlainSize; ++i)
                    body.put<std::uint8_t>(0);
            bodies.push_back(std::move(body));
        }
        Bytes out;
        out.text("Gamebryo File Format, Version 20.2.0.7\n");
        out.put<std::uint32_t>(version);
        out.put<std::uint8_t>(1);
        out.put<std::uint32_t>(12);
        out.put<std::uint32_t>(static_cast<std::uint32_t>(blocks.size()));
        out.put<std::uint32_t>(userVersion2);
        for (const char* s : { "author", "", "script" })
        {
            out.put<std::uint8_t>(static_cast<std::uint8_t>(std::strlen(s)));
            out.text(s);
        }
        out.put<std::uint16_t>(static_cast<std::uint16_t>(types.size()));
        for (const std::string& t : types)
        {
            out.put<std::uint32_t>(static_cast<std::uint32_t>(t.size()));
            out.text(t);
        }
        for (const NifBlock& b : blocks)
            out.put<std::uint16_t>(
                static_cast<std::uint16_t>(std::find(types.begin(), types.end(), b.mType) - types.begin()));
        for (const Bytes& b : bodies)
            out.put<std::uint32_t>(static_cast<std::uint32_t>(b.v.size()));
        out.put<std::uint32_t>(1); // one string
        out.put<std::uint32_t>(3);
        out.put<std::uint32_t>(3);
        out.text("FRN");
        out.put<std::uint32_t>(0); // groups
        for (const Bytes& b : bodies)
            out.v.insert(out.v.end(), b.v.begin(), b.v.end());
        return out.v;
    }

    FurnitureMarker marker(float ox, float oy, float oz, float heading, std::uint16_t type, std::uint16_t entry)
    {
        FurnitureMarker m;
        m.mOx = ox;
        m.mOy = oy;
        m.mOz = oz;
        m.mHeading = heading;
        m.mType = type;
        m.mEntry = entry;
        return m;
    }

    // ---- the reader
    // --------------------------------------------------------------------------------------------------

    TEST(SkyrimFurnitureReader, readsTheMarkersOfAChairLikeTheInstalledOne)
    {
        // CommonChair02: (0, 0, 34), heading 0, type 1, entry 13 (measured in Skyrim - Meshes0.bsa)
        const auto nif = makeNif({ { "NiNode", {}, 20 }, { "BSFurnitureMarkerNode", { marker(0, 0, 34, 0, 1, 13) } } });
        std::vector<FurnitureMarker> out;
        std::string error;
        ASSERT_TRUE(readFurnitureMarkers(nif, out, error)) << error;
        ASSERT_EQ(out.size(), 1u);
        EXPECT_EQ(out[0], marker(0, 0, 34, 0, MarkerType::Sit, 13));
    }

    TEST(SkyrimFurnitureReader, readsABedAndAnotherMarkerBlockOfTheOldName)
    {
        const auto nif = makeNif({ { "BSFurnitureMarkerNode", { marker(0, -11.2f, 37.1f, 0, 2, 12) } },
            { "BSFurnitureMarker", { marker(1, 2, 3, 4, 1, 3), marker(5, 6, 7, 8, 1, 3) } } });
        std::vector<FurnitureMarker> out;
        std::string error;
        ASSERT_TRUE(readFurnitureMarkers(nif, out, error)) << error;
        ASSERT_EQ(out.size(), 3u);
        EXPECT_EQ(out[0].mType, MarkerType::Sleep);
        EXPECT_EQ(out[2].mOx, 5.f);
    }

    TEST(SkyrimFurnitureReader, aModelWithoutMarkersIsReadableAndEmpty)
    {
        const auto nif = makeNif({ { "NiNode", {}, 16 }, { "BSTriShape", {}, 40 } });
        std::vector<FurnitureMarker> out{ marker(1, 1, 1, 1, 1, 1) };
        std::string error;
        EXPECT_TRUE(readFurnitureMarkers(nif, out, error));
        EXPECT_TRUE(out.empty()); // cleared first
    }

    TEST(SkyrimFurnitureReader, refusesWhatIsNotASkyrimNif)
    {
        std::vector<FurnitureMarker> out;
        std::string error;
        EXPECT_FALSE(readFurnitureMarkers({}, out, error));
        EXPECT_FALSE(error.empty());
        const auto morrowind = makeNif({ { "NiNode", {}, 8 } }, 0x04000002u);
        EXPECT_FALSE(readFurnitureMarkers(morrowind, out, error));
        const auto fallout4 = makeNif({ { "NiNode", {}, 8 } }, 0x14020007u, 130);
        EXPECT_FALSE(readFurnitureMarkers(fallout4, out, error));
        const std::string text = "not a nif at all, just text";
        EXPECT_FALSE(readFurnitureMarkers(
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(text.data()), text.size()), out, error));
    }

    TEST(SkyrimFurnitureReader, everyTruncationIsRefusedOrReadsAPrefixSafely)
    {
        const auto nif = makeNif({ { "NiNode", {}, 20 }, { "BSFurnitureMarkerNode", { marker(0, 0, 34, 0, 1, 13) } } });
        for (std::size_t cut = 0; cut < nif.size(); ++cut)
        {
            std::vector<FurnitureMarker> out;
            std::string error;
            const bool ok = readFurnitureMarkers(std::span<const std::byte>(nif.data(), cut), out, error);
            EXPECT_FALSE(ok) << "cut at " << cut;
            EXPECT_TRUE(out.empty());
        }
    }

    TEST(SkyrimFurnitureReader, aHostileMarkerCountIsRefused)
    {
        auto nif = makeNif({ { "BSFurnitureMarkerNode", { marker(0, 0, 0, 0, 1, 1) } } });
        // the count follows the 4-byte name index at the start of the only block, which is the last 28 bytes
        const std::size_t countAt = nif.size() - 20 - 4;
        const std::uint32_t huge = 0xFFFFFFF0u;
        std::memcpy(nif.data() + countAt, &huge, 4);
        std::vector<FurnitureMarker> out;
        std::string error;
        EXPECT_FALSE(readFurnitureMarkers(nif, out, error));
        EXPECT_TRUE(out.empty());
    }

    TEST(SkyrimFurnitureReader, aNonFiniteMarkerIsSkippedAndTheRestKept)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const auto nif
            = makeNif({ { "BSFurnitureMarkerNode", { marker(nan, 0, 0, 0, 1, 1), marker(1, 2, 3, 0, 1, 1) } } });
        std::vector<FurnitureMarker> out;
        std::string error;
        ASSERT_TRUE(readFurnitureMarkers(nif, out, error));
        ASSERT_EQ(out.size(), 1u);
        EXPECT_EQ(out[0].mOx, 1.f);
    }

    TEST(SkyrimFurnitureReader, aBlockCountOverTheLimitIsRefused)
    {
        FurnitureLimits limits;
        limits.mMaxBlocks = 1;
        const auto nif = makeNif({ { "NiNode", {}, 8 }, { "NiNode", {}, 8 } });
        std::vector<FurnitureMarker> out;
        std::string error;
        EXPECT_FALSE(readFurnitureMarkers(nif, out, error, limits));
    }

    // ---- placement
    // ------------------------------------------------------------------------------------------------------

    constexpr double kPi = 3.14159265358979323846;

    TEST(SkyrimFurniturePlacement, anUnrotatedChairSeatsAtItsOffsetAndFacesNorth)
    {
        const FurniturePlacement p{ 100, 200, 60, 0.0, 1.0 };
        const SeatPoint seat = markerInWorld(p, marker(0, 0, 34, 0, 1, 13));
        EXPECT_DOUBLE_EQ(seat.mX, 100);
        EXPECT_DOUBLE_EQ(seat.mY, 200);
        EXPECT_DOUBLE_EQ(seat.mZ, 60); // the actor's origin is on the furniture's floor
        EXPECT_DOUBLE_EQ(seat.mSurfaceZ, 94); // the marker's own height
        EXPECT_FLOAT_EQ(seat.mHeading, 0.f);
    }

    TEST(SkyrimFurniturePlacement, theOffsetTurnsClockwiseWithTheRotation)
    {
        // a quarter turn clockwise (the engine's positive): furniture forward (0, 1) becomes (1, 0)
        const FurniturePlacement p{ 0, 0, 0, kPi / 2, 1.0 };
        const SeatPoint seat = markerInWorld(p, marker(0, 10, 0, 0, 1, 1));
        EXPECT_NEAR(seat.mX, 10.0, 1e-9);
        EXPECT_NEAR(seat.mY, 0.0, 1e-9);
        // and its right (1, 0) becomes (0, -1)
        const SeatPoint right = markerInWorld(p, marker(10, 0, 0, 0, 1, 1));
        EXPECT_NEAR(right.mX, 0.0, 1e-9);
        EXPECT_NEAR(right.mY, -10.0, 1e-9);
        EXPECT_NEAR(seat.mHeading, kPi / 2, 1e-6);
    }

    TEST(SkyrimFurniturePlacement, theMarkerHeadingAddsToTheRotationAndScaleMultipliesTheOffset)
    {
        const FurniturePlacement p{ 0, 0, 10, 1.0, 2.0 };
        const SeatPoint seat = markerInWorld(p, marker(0, 0, 5, 3.142f, 2, 12));
        EXPECT_NEAR(seat.mHeading, 1.0 + 3.142, 1e-5);
        EXPECT_DOUBLE_EQ(seat.mSurfaceZ, 20); // 10 + 5 * 2
        EXPECT_DOUBLE_EQ(seat.mZ, 10);
    }

    TEST(SkyrimFurniturePlacement, aBadScaleIsOne)
    {
        for (const double scale : { 0.0, -3.0, std::numeric_limits<double>::quiet_NaN() })
        {
            const FurniturePlacement p{ 0, 0, 0, 0.0, scale };
            EXPECT_DOUBLE_EQ(markerInWorld(p, marker(0, 7, 0, 0, 1, 1)).mY, 7.0);
        }
    }

    TEST(SkyrimFurniturePlacement, entryPointsFollowTheFlagsInTheSittersFrame)
    {
        const FurniturePlacement p{ 0, 0, 60, 0.0, 1.0 };
        const SeatPoint seat{ 0, 0, 94, 0.f }; // faces +Y
        // a chair: front, right, left (13): ordered front, left, right
        const auto chair = entryPoints(p, seat, 13, 64.0);
        ASSERT_EQ(chair.size(), 3u);
        EXPECT_EQ(chair[0].mSide, Side::Front);
        EXPECT_NEAR(chair[0].mY, 64.0, 1e-9);
        EXPECT_EQ(chair[1].mSide, Side::Left);
        EXPECT_NEAR(chair[1].mX, -64.0, 1e-9);
        EXPECT_EQ(chair[2].mSide, Side::Right);
        EXPECT_NEAR(chair[2].mX, 64.0, 1e-9);
        EXPECT_DOUBLE_EQ(chair[0].mZ, 60); // the furniture's floor height
        // a bar stool: only behind (2)
        const auto stool = entryPoints(p, seat, 2, 64.0);
        ASSERT_EQ(stool.size(), 1u);
        EXPECT_EQ(stool[0].mSide, Side::Behind);
        EXPECT_NEAR(stool[0].mY, -64.0, 1e-9);
        // a bed: right and left (12)
        EXPECT_EQ(entryPoints(p, seat, 12, 96.0).size(), 2u);
        // no known side at all: all four
        EXPECT_EQ(entryPoints(p, seat, 0, 64.0).size(), 4u);
        EXPECT_EQ(entryPoints(p, seat, 16, 64.0).size(), 4u); // "up" alone is no side either
    }

    TEST(SkyrimFurniturePlacement, theEntryPointsTurnWithTheSitter)
    {
        const FurniturePlacement p{ 0, 0, 0, 0.0, 1.0 };
        const SeatPoint seat{ 0, 0, 0, static_cast<float>(kPi / 2) }; // faces +X
        const auto e = entryPoints(p, seat, EntryBit::Front, 10.0);
        ASSERT_EQ(e.size(), 1u);
        EXPECT_NEAR(e[0].mX, 10.0, 1e-6);
        EXPECT_NEAR(e[0].mY, 0.0, 1e-6);
    }

    // ---- the brain's furniture use
    // ----------------------------------------------------------------------------------------

    constexpr FormId kBaseId = 0x00013BA7;

    PackageRecord makePackage(FormId id, std::vector<std::uint8_t> bools = {})
    {
        PackageRecord p;
        p.mCommon.mId = id;
        p.mHasPkdt = true;
        p.mType = 18;
        p.mBools = std::move(bools);
        return p;
    }

    class Source final : public PackageSource
    {
    public:
        bool ready() const override { return true; }
        bool packagesFor(FormId base, std::vector<const PackageRecord*>& out) const override
        {
            const auto it = mLists.find(base);
            if (it == mLists.end())
                return false;
            out = it->second;
            return true;
        }
        PackageBehavior behaviorOf(const PackageRecord& p) const override
        {
            const auto it = mBehavior.find(p.mCommon.mId);
            return it == mBehavior.end() ? PackageBehavior::Stand : it->second;
        }
        bool conditionsPass(const PackageRecord& p, FormId, FormId) override
        {
            return mFailing.count(p.mCommon.mId) == 0;
        }
        bool locate(FormId, double&, double&, double&) const override { return false; }
        std::map<FormId, std::vector<const PackageRecord*>> mLists;
        std::map<FormId, PackageBehavior> mBehavior;
        std::set<FormId> mFailing;
    };

    class Furniture final : public FurnitureSource
    {
    public:
        void slotsNear(FormId, double x, double y, double z, double radius, sdk::FurniturePose pose, double, double,
            double, std::vector<FurnitureSlot>& out) override
        {
            ++mAsks;
            mLastCenter = { x, y, z };
            mLastRadius = radius;
            out.clear();
            for (const FurnitureSlot& s : mSlots)
                if (s.mPose == pose)
                    out.push_back(s);
        }
        std::vector<FurnitureSlot> mSlots;
        int mAsks = 0;
        std::array<double, 3> mLastCenter{};
        double mLastRadius = 0;
    };

    FurnitureSlot slot(std::uint64_t key, sdk::FurniturePose pose, double x, double y)
    {
        FurnitureSlot s;
        s.mKey = key;
        s.mPose = pose;
        s.mX = x;
        s.mY = y;
        s.mZ = 95;
        s.mHeading = 1.f;
        s.mEntryX = x + 50;
        s.mEntryY = y;
        s.mEntryZ = 60;
        s.mClip = pose == sdk::FurniturePose::Sleep ? "bed_leftsleep" : "chair_idlebasevar1";
        s.mClipPerSex = pose == sdk::FurniturePose::Sit;
        return s;
    }

    struct Call
    {
        enum Kind
        {
            Goto,
            Cancel,
            Use,
            Leave
        } mKind;
        sdk::ActorHandle mHandle;
        double mX = 0, mY = 0, mZ = 0;
        std::string mClip;
        sdk::FurniturePose mPose = sdk::FurniturePose::Sit;
    };

    class Writer final : public sdk::ActorRequestWriter
    {
    public:
        bool setMovement(sdk::ActorHandle, float, float) override { return true; }
        bool goTo(sdk::ActorHandle h, const sdk::ActorGoal& g) override
        {
            mCalls.push_back({ Call::Goto, h, g.x, g.y, g.z, {} });
            return mAccept;
        }
        bool cancelGoal(sdk::ActorHandle h) override
        {
            mCalls.push_back({ Call::Cancel, h, 0, 0, 0, {} });
            return true;
        }
        bool setHeading(sdk::ActorHandle, float) override { return true; }
        bool useFurniture(sdk::ActorHandle h, const sdk::FurnitureUse& u) override
        {
            mCalls.push_back({ Call::Use, h, u.x, u.y, u.z, std::string(u.clip), u.pose });
            return mAcceptUse;
        }
        bool leaveFurniture(sdk::ActorHandle h, double x, double y, double z) override
        {
            mCalls.push_back({ Call::Leave, h, x, y, z, {} });
            return true;
        }
        sdk::ActorClock clock() const override { return mClock; }
        sdk::ActorClock mClock{ 0.f, 0, true };
        bool mAccept = true;
        bool mAcceptUse = true;
        std::vector<Call> mCalls;

        std::vector<Call> of(Call::Kind k) const
        {
            std::vector<Call> r;
            for (const Call& c : mCalls)
                if (c.mKind == k)
                    r.push_back(c);
            return r;
        }
    };

    sdk::ActorInfo infoOf(sdk::WorldId ref, std::uint32_t session, double x = 0, double y = 0, double z = 60)
    {
        sdk::ActorInfo i;
        i.handle = { ref, session };
        i.base = kBaseId;
        i.editorId = "Test";
        i.x = x;
        i.y = y;
        i.z = z;
        return i;
    }

    struct Rig
    {
        Source source;
        Furniture furniture;
        Writer writer;
        std::vector<PackageRecord> packages;
        std::unique_ptr<PackageBrain> brain;

        explicit Rig(PackageBehavior behavior, std::vector<std::uint8_t> bools = {}, bool withFurniture = true)
        {
            packages.reserve(4);
            packages.push_back(makePackage(0x100, std::move(bools)));
            source.mLists[kBaseId] = { &packages[0] };
            source.mBehavior[0x100] = behavior;
            brain = std::make_unique<PackageBrain>(source);
            if (withFurniture)
                brain->setFurnitureSource(&furniture);
        }
        void step(int n, float dt = 0.1f)
        {
            for (int i = 0; i < n; ++i)
                brain->update(dt, writer);
        }
        void arrive(sdk::ActorHandle h) { brain->onGoalFinished(h, sdk::GoalResult::Arrived); }
    };

    TEST(SkyrimFurnitureBrain, aSleepingActorWalksToTheBedsEntryThenLiesDown)
    {
        Rig rig(PackageBehavior::Sleep);
        rig.furniture.mSlots = { slot(1, sdk::FurniturePose::Sleep, 500, 500) };
        const sdk::ActorHandle h{ 1, 1 };
        rig.brain->onActorAdded(infoOf(1, 1));
        rig.step(30);
        const auto gotos = rig.writer.of(Call::Goto);
        ASSERT_EQ(gotos.size(), 1u);
        EXPECT_DOUBLE_EQ(gotos[0].mX, 550); // the entry point, not the seat
        EXPECT_EQ(rig.brain->view(1)->mPhase, PackageBrain::Phase::Approaching);
        EXPECT_TRUE(rig.writer.of(Call::Use).empty()); // not before it arrived
        rig.arrive(h);
        rig.step(1);
        const auto uses = rig.writer.of(Call::Use);
        ASSERT_EQ(uses.size(), 1u);
        EXPECT_DOUBLE_EQ(uses[0].mX, 500);
        EXPECT_DOUBLE_EQ(uses[0].mZ, 95);
        EXPECT_EQ(uses[0].mClip, "bed_leftsleep");
        EXPECT_EQ(uses[0].mPose, sdk::FurniturePose::Sleep);
        EXPECT_TRUE(rig.brain->view(1)->mSeated);
        EXPECT_EQ(rig.brain->view(1)->mPhase, PackageBrain::Phase::Seated);
        rig.step(2000, 0.5f); // a sleeper stays as long as the package runs
        EXPECT_TRUE(rig.brain->view(1)->mSeated);
        EXPECT_TRUE(rig.writer.of(Call::Leave).empty());
        EXPECT_EQ(rig.brain->stats().mSeatings, 1u);
    }

    TEST(SkyrimFurnitureBrain, twoSleepersNeverShareABed)
    {
        Rig rig(PackageBehavior::Sleep);
        rig.furniture.mSlots
            = { slot(1, sdk::FurniturePose::Sleep, 500, 500), slot(2, sdk::FurniturePose::Sleep, 700, 500) };
        rig.brain->onActorAdded(infoOf(1, 1));
        rig.brain->onActorAdded(infoOf(2, 2));
        rig.step(30);
        const auto gotos = rig.writer.of(Call::Goto);
        ASSERT_EQ(gotos.size(), 2u);
        EXPECT_NE(gotos[0].mX, gotos[1].mX);
        EXPECT_NE(rig.brain->view(1)->mSlotKey, rig.brain->view(2)->mSlotKey);
    }

    TEST(SkyrimFurnitureBrain, aThirdSleeperWithTwoBedsStandsAsBefore)
    {
        Rig rig(PackageBehavior::Sleep);
        rig.furniture.mSlots
            = { slot(1, sdk::FurniturePose::Sleep, 500, 500), slot(2, sdk::FurniturePose::Sleep, 700, 500) };
        for (std::uint32_t i = 1; i <= 3; ++i)
            rig.brain->onActorAdded(infoOf(i, i, 10.0 * i, 0));
        rig.step(30);
        int approaching = 0, other = 0;
        for (sdk::WorldId i = 1; i <= 3; ++i)
            (rig.brain->view(i)->mPhase == PackageBrain::Phase::Approaching ? approaching : other)++;
        EXPECT_EQ(approaching, 2);
        EXPECT_EQ(other, 1);
        EXPECT_GE(rig.brain->stats().mNoFurniture, 1u);
    }

    TEST(SkyrimFurnitureBrain, withNoFurnitureSourceASleeperStandsAtTheLocationAsBefore)
    {
        Rig rig(PackageBehavior::Sleep, {}, false);
        rig.brain->onActorAdded(infoOf(1, 1));
        rig.step(30);
        EXPECT_TRUE(rig.writer.of(Call::Use).empty());
        EXPECT_EQ(rig.brain->view(1)->mPhase, PackageBrain::Phase::Done);
    }

    TEST(SkyrimFurnitureBrain, whenThePackageEndsTheSleeperGetsUpAndTheBedIsFree)
    {
        Rig rig(PackageBehavior::Sleep);
        rig.furniture.mSlots = { slot(1, sdk::FurniturePose::Sleep, 500, 500) };
        const sdk::ActorHandle h{ 1, 1 };
        rig.brain->onActorAdded(infoOf(1, 1));
        rig.step(30);
        rig.arrive(h);
        rig.step(2);
        ASSERT_TRUE(rig.brain->view(1)->mSeated);
        // the package stops passing its conditions: the next evaluation finds none
        rig.source.mFailing.insert(0x100);
        rig.step(30);
        const auto leaves = rig.writer.of(Call::Leave);
        ASSERT_EQ(leaves.size(), 1u);
        EXPECT_DOUBLE_EQ(leaves[0].mX, 550); // steps off at the entry point
        EXPECT_FALSE(rig.brain->view(1)->mSeated);
        EXPECT_EQ(rig.brain->view(1)->mSlotKey, 0u);
        // another actor can now take the bed
        rig.brain->onActorRemoved(h);
        rig.source.mFailing.clear();
        rig.brain->onActorAdded(infoOf(2, 2));
        rig.step(30);
        EXPECT_EQ(rig.brain->view(2)->mSlotKey, 1u);
    }

    TEST(SkyrimFurnitureBrain, aBlockedTripTriesTheNextBedAndNeverTheSameOneTwice)
    {
        Rig rig(PackageBehavior::Sleep);
        rig.furniture.mSlots
            = { slot(1, sdk::FurniturePose::Sleep, 500, 500), slot(2, sdk::FurniturePose::Sleep, 700, 500) };
        const sdk::ActorHandle h{ 1, 1 };
        rig.brain->onActorAdded(infoOf(1, 1));
        rig.step(30);
        ASSERT_EQ(rig.brain->view(1)->mSlotKey, 1u);
        rig.brain->onGoalFinished(h, sdk::GoalResult::NoPath);
        EXPECT_EQ(rig.brain->view(1)->mSlotKey, 0u); // released at once
        rig.step(50); // kFurnitureRetrySeconds
        EXPECT_EQ(rig.brain->view(1)->mSlotKey, 2u);
        rig.brain->onGoalFinished(h, sdk::GoalResult::Blocked);
        rig.step(50);
        // both are bad now: nothing to approach, the actor falls back to standing
        EXPECT_EQ(rig.brain->view(1)->mSlotKey, 0u);
        EXPECT_FALSE(rig.brain->view(1)->mSeated);
        EXPECT_EQ(rig.brain->stats().mFurnitureFailed, 2u);
        EXPECT_EQ(rig.writer.of(Call::Goto).size(), 2u);
    }

    TEST(SkyrimFurnitureBrain, withSnappingASleeperWhoCannotWalkThereIsPutOnTheBed)
    {
        for (const sdk::GoalResult failure : { sdk::GoalResult::NoPath, sdk::GoalResult::Blocked })
        {
            Rig rig(PackageBehavior::Sleep);
            PackageBrain::Options options;
            options.mSnapSleepers = true;
            rig.brain = std::make_unique<PackageBrain>(rig.source, options);
            rig.brain->setFurnitureSource(&rig.furniture);
            rig.furniture.mSlots = { slot(1, sdk::FurniturePose::Sleep, 500, 500) };
            rig.brain->onActorAdded(infoOf(1, 1));
            rig.step(30);
            rig.brain->onGoalFinished({ 1, 1 }, failure);
            rig.step(1);
            const auto uses = rig.writer.of(Call::Use);
            ASSERT_EQ(uses.size(), 1u);
            EXPECT_DOUBLE_EQ(uses[0].mX, 500);
            EXPECT_TRUE(rig.brain->view(1)->mSeated);
            EXPECT_EQ(rig.brain->stats().mSnapped, 1u);
            EXPECT_EQ(rig.brain->stats().mFurnitureFailed, 0u);
        }
    }

    TEST(SkyrimFurnitureBrain, snappingIsOffByDefaultAndNeverForEatersOrSandboxSitters)
    {
        {
            Rig rig(PackageBehavior::Sleep); // default options
            rig.furniture.mSlots = { slot(1, sdk::FurniturePose::Sleep, 500, 500) };
            rig.brain->onActorAdded(infoOf(1, 1));
            rig.step(30);
            rig.brain->onGoalFinished({ 1, 1 }, sdk::GoalResult::NoPath);
            rig.step(1);
            EXPECT_TRUE(rig.writer.of(Call::Use).empty());
        }
        {
            Rig rig(PackageBehavior::Eat);
            PackageBrain::Options options;
            options.mSnapSleepers = true;
            rig.brain = std::make_unique<PackageBrain>(rig.source, options);
            rig.brain->setFurnitureSource(&rig.furniture);
            rig.furniture.mSlots = { slot(3, sdk::FurniturePose::Sit, 100, 100) };
            rig.brain->onActorAdded(infoOf(1, 1));
            rig.step(30);
            rig.brain->onGoalFinished({ 1, 1 }, sdk::GoalResult::NoPath);
            rig.step(1);
            EXPECT_TRUE(rig.writer.of(Call::Use).empty());
            EXPECT_EQ(rig.brain->stats().mSnapped, 0u);
        }
    }

    TEST(SkyrimFurnitureBrain, aRefusedUseFreesTheBedAndIsNotRepeatedForever)
    {
        Rig rig(PackageBehavior::Sleep);
        rig.furniture.mSlots = { slot(1, sdk::FurniturePose::Sleep, 500, 500) };
        rig.writer.mAcceptUse = false; // an old core
        const sdk::ActorHandle h{ 1, 1 };
        rig.brain->onActorAdded(infoOf(1, 1));
        rig.step(30);
        rig.arrive(h);
        rig.step(100);
        EXPECT_FALSE(rig.brain->view(1)->mSeated);
        EXPECT_EQ(rig.writer.of(Call::Use).size(), 1u);
        EXPECT_EQ(rig.brain->view(1)->mSlotKey, 0u);
    }

    TEST(SkyrimFurnitureBrain, anEaterSitsAndStaysLikeASleeper)
    {
        Rig rig(PackageBehavior::Eat);
        rig.furniture.mSlots
            = { slot(3, sdk::FurniturePose::Sit, 100, 100), slot(4, sdk::FurniturePose::Sleep, 300, 300) };
        rig.brain->onActorAdded(infoOf(1, 1));
        rig.step(30);
        ASSERT_EQ(rig.writer.of(Call::Goto).size(), 1u);
        EXPECT_DOUBLE_EQ(rig.writer.of(Call::Goto)[0].mX, 150); // the chair, not the bed
        rig.arrive({ 1, 1 });
        rig.step(2);
        ASSERT_EQ(rig.writer.of(Call::Use).size(), 1u);
        EXPECT_EQ(rig.writer.of(Call::Use)[0].mPose, sdk::FurniturePose::Sit);
        rig.step(2000, 0.5f);
        EXPECT_TRUE(rig.brain->view(1)->mSeated);
    }

    TEST(SkyrimFurnitureBrain, aSandboxWithSittingOnSitsSometimeAndGetsUpAgain)
    {
        // bools: eating, sleeping, conversation, idle markers, sitting
        Rig rig(PackageBehavior::Sandbox, { 1, 1, 1, 1, 1, 1, 1, 0, 0 });
        rig.furniture.mSlots = { slot(5, sdk::FurniturePose::Sit, 100, 100) };
        rig.brain->onActorAdded(infoOf(1, 1));
        // The sandbox radius is the default (no location); walk legs are told they arrived at once.
        bool seated = false, stoodUp = false;
        for (int i = 0; i < 4000 && !stoodUp; ++i)
        {
            rig.step(1, 0.5f);
            const auto view = rig.brain->view(1);
            if (view->mPhase == PackageBrain::Phase::Walking || view->mPhase == PackageBrain::Phase::Approaching)
                rig.arrive({ 1, 1 });
            seated = seated || view->mSeated;
            stoodUp = !rig.writer.of(Call::Leave).empty();
        }
        EXPECT_TRUE(seated);
        EXPECT_TRUE(stoodUp);
        EXPECT_GE(rig.brain->stats().mSeatings, 1u);
        EXPECT_EQ(rig.brain->stats().mStandUps, rig.writer.of(Call::Leave).size());
    }

    TEST(SkyrimFurnitureBrain, aSandboxWithSittingOffNeverSitsAndNeverAsksForFurniture)
    {
        Rig rig(PackageBehavior::Sandbox, { 1, 1, 1, 1, 0, 1, 1, 0, 0 });
        rig.furniture.mSlots = { slot(5, sdk::FurniturePose::Sit, 100, 100) };
        rig.brain->onActorAdded(infoOf(1, 1));
        for (int i = 0; i < 2000; ++i)
        {
            rig.step(1, 0.5f);
            const auto view = rig.brain->view(1);
            if (view->mPhase == PackageBrain::Phase::Walking)
                rig.arrive({ 1, 1 });
        }
        EXPECT_TRUE(rig.writer.of(Call::Use).empty());
        EXPECT_EQ(rig.furniture.mAsks, 0);
    }

    TEST(SkyrimFurnitureBrain, aPackageWithTooFewInputsAllowsNoSitting)
    {
        Rig rig(PackageBehavior::Sandbox, { 1, 1 });
        rig.furniture.mSlots = { slot(5, sdk::FurniturePose::Sit, 100, 100) };
        rig.brain->onActorAdded(infoOf(1, 1));
        for (int i = 0; i < 1000; ++i)
        {
            rig.step(1, 0.5f);
            if (rig.brain->view(1)->mPhase == PackageBrain::Phase::Walking)
                rig.arrive({ 1, 1 });
        }
        EXPECT_EQ(rig.furniture.mAsks, 0);
    }

    TEST(SkyrimFurnitureBrain, aRemovedActorFreesItsSlot)
    {
        Rig rig(PackageBehavior::Sleep);
        rig.furniture.mSlots = { slot(1, sdk::FurniturePose::Sleep, 500, 500) };
        rig.brain->onActorAdded(infoOf(1, 1));
        rig.step(30);
        ASSERT_EQ(rig.brain->view(1)->mSlotKey, 1u);
        rig.brain->onActorRemoved({ 1, 1 });
        rig.brain->onActorAdded(infoOf(2, 2));
        rig.step(30);
        EXPECT_EQ(rig.brain->view(2)->mSlotKey, 1u);
    }

    TEST(SkyrimFurnitureBrain, theSearchIsAroundThePackageLocationWithAMinimumRadius)
    {
        Rig rig(PackageBehavior::Sleep);
        rig.brain->onActorAdded(infoOf(1, 1, 33, 44, 55));
        rig.step(30);
        EXPECT_GE(rig.furniture.mAsks, 1);
        EXPECT_DOUBLE_EQ(rig.furniture.mLastCenter[0], 33);
        EXPECT_DOUBLE_EQ(rig.furniture.mLastCenter[1], 44);
        EXPECT_GE(rig.furniture.mLastRadius, static_cast<double>(kMinFurnitureRadius));
    }

    TEST(SkyrimFurnitureBrain, aSleeperWithNoBedInRangeLooksInTheWholeNeighbourhoodAnAnEaterDoesNot)
    {
        Rig sleeper(PackageBehavior::Sleep);
        sleeper.brain->onActorAdded(infoOf(1, 1));
        sleeper.step(30);
        EXPECT_EQ(sleeper.furniture.mAsks, 2);
        EXPECT_DOUBLE_EQ(sleeper.furniture.mLastRadius, static_cast<double>(kMaxPackageRadius));
        Rig eater(PackageBehavior::Eat);
        eater.brain->onActorAdded(infoOf(1, 1));
        eater.step(30);
        EXPECT_EQ(eater.furniture.mAsks, 1);
    }

    TEST(SkyrimFurnitureBrain, theSameSeedGivesTheSameSittingTimes)
    {
        auto trace = []() {
            Rig rig(PackageBehavior::Sandbox, { 1, 1, 1, 1, 1 });
            rig.furniture.mSlots = { slot(5, sdk::FurniturePose::Sit, 100, 100) };
            rig.brain->onActorAdded(infoOf(1, 1));
            std::vector<int> events;
            for (int i = 0; i < 1500; ++i)
            {
                rig.step(1, 0.5f);
                const auto view = rig.brain->view(1);
                if (view->mPhase == PackageBrain::Phase::Walking || view->mPhase == PackageBrain::Phase::Approaching)
                    rig.arrive({ 1, 1 });
                if (rig.writer.of(Call::Use).size() + rig.writer.of(Call::Leave).size() > events.size())
                    events.push_back(i);
            }
            return events;
        };
        EXPECT_EQ(trace(), trace());
    }
}
