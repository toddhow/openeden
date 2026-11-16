// Task 0255 (horse riding, second slice): the camera package, getting off at a load door, and a save and load while
// riding, over a fake core. Written from the header before the code (the first run was against stubs, see the task's
// stage log). No plugin bytes, no game data.
#include <game_skyrimse/neutral_world.hpp>
#include <game_skyrimse/riding.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace openeden::sdk;
    using namespace openeden::sdk::game;

    constexpr float kHorseWalk = 125.1127f;
    constexpr float kHorseRun = 454.9714f;
    constexpr WorldId kRef = 77;
    constexpr WorldId kBase = 0x1234;

    class Core2 final : public ActorRequestWriter, public RidingWriter
    {
    public:
        bool setMovement(ActorHandle, float velX, float velY) override
        {
            ++mMovements;
            mVelX = velX;
            mVelY = velY;
            return true;
        }
        bool setHeading(ActorHandle, float) override { return true; }
        bool actorPosition(ActorHandle handle, double& x, double& y, double& z) const override
        {
            if (handle.reference != kRef)
                return false;
            x = 1000.0;
            y = 2000.0;
            z = 300.0;
            return true;
        }
        bool playerPosition(double& x, double& y, double& z) const override
        {
            if (!mHavePlayer)
                return false;
            x = mPlayerX;
            y = mPlayerY;
            z = mPlayerZ;
            return true;
        }
        bool riderControls(RiderControls& out) const override
        {
            out = mControls;
            return true;
        }
        bool carryPlayer(double, double, double, float) override
        {
            ++mCarries;
            return true;
        }
        bool releasePlayer(double x, double y, double z) override
        {
            ++mReleases;
            mReleaseX = x;
            mReleaseY = y;
            mReleaseZ = z;
            return true;
        }

        RiderControls mControls;
        bool mHavePlayer = true;
        double mPlayerX = 500.0, mPlayerY = 600.0, mPlayerZ = 700.0;
        int mMovements = 0, mCarries = 0, mReleases = 0;
        float mVelX = 9.f, mVelY = 9.f;
        double mReleaseX = 0, mReleaseY = 0, mReleaseZ = 0;
    };

    ActorHandle horse()
    {
        ActorHandle h;
        h.reference = kRef;
        h.session = 5;
        return h;
    }

    struct Rig2
    {
        Riding mRiding;
        Core2 mCore;
        std::vector<std::pair<WorldId, bool>> mBrain;
        std::vector<std::string> mLog;

        Rig2()
        {
            mRiding.setEnabled(true);
            mRiding.setTestOwned("*");
            mRiding.setHorseLookup([](WorldId base, std::uint32_t, HorseInfo& out) {
                if (base != kBase)
                    return false;
                out.mWalkSpeed = kHorseWalk;
                out.mRunSpeed = kHorseRun;
                return true;
            });
            mRiding.setBrainControl([this](WorldId reference, bool stands) { mBrain.push_back({ reference, stands }); });
            mRiding.setLog([this](const std::string& line) { mLog.push_back(line); });
            ActorInfo info;
            info.handle = horse();
            info.base = kBase;
            info.editorId = "Horse";
            mRiding.onActorAdded(info);
            mCore.mControls.valid = true;
        }
        void mount()
        {
            mRiding.onMountRequested(horse());
            mRiding.update(0.016f, mCore);
        }
    };

    class Memory final : public ChunkWriter, public ChunkReader
    {
    public:
        void write(const void* data, std::size_t size) override
        {
            const auto* bytes = static_cast<const unsigned char*>(data);
            mBytes.insert(mBytes.end(), bytes, bytes + size);
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
        std::vector<unsigned char> mBytes;
        std::size_t mPos = 0;
    };

    class FakeService final : public BindingsService
    {
    public:
        DeclareResult declareNamespace(const NamespaceDescriptor& descriptor) override
        {
            mName = std::string(descriptor.name);
            mVersion = descriptor.version;
            mFunctions.assign(descriptor.functions.begin(), descriptor.functions.end());
            return { true, {} };
        }
        const FunctionDescriptor* find(std::string_view name) const
        {
            for (const FunctionDescriptor& f : mFunctions)
                if (f.name == name)
                    return &f;
            return nullptr;
        }
        std::string mName;
        Version mVersion{ 0, 0 };
        std::vector<FunctionDescriptor> mFunctions;
    };

    // ---- the camera package ------------------------------------------------------------------------------------

    TEST(SkyrimRidingCamera, theNamespaceIsRidingVersionOneAndHasTwoReadFunctions)
    {
        Rig2 rig;
        RidingBindings bindings(rig.mRiding);
        FakeService service;
        const DeclareResult declared = bindings.declareTo(service);
        ASSERT_TRUE(declared.ok) << declared.message;
        EXPECT_EQ(service.mName, "skyrimse.riding");
        EXPECT_EQ(static_cast<int>(service.mVersion.major), 1);
        EXPECT_EQ(service.mFunctions.size(), 2u);
        for (const FunctionDescriptor& f : service.mFunctions)
        {
            EXPECT_EQ(f.access, BindingAccess::Read);
            EXPECT_NE(f.handler, nullptr);
        }
    }

    TEST(SkyrimRidingCamera, activeFollowsTheMountAndTheScaleIsTheConstant)
    {
        Rig2 rig;
        RidingBindings bindings(rig.mRiding);
        FakeService service;
        ASSERT_TRUE(bindings.declareTo(service).ok);
        const FunctionDescriptor* active = service.find("active");
        const FunctionDescriptor* scale = service.find("camera_scale");
        ASSERT_NE(active, nullptr);
        ASSERT_NE(scale, nullptr);
        EXPECT_FALSE(active->handler->call({}).value.boolean);
        rig.mount();
        EXPECT_TRUE(active->handler->call({}).value.boolean);
        const BindingResult s = scale->handler->call({});
        EXPECT_EQ(s.status, BindingStatus::Ok);
        EXPECT_NEAR(s.value.number, kRidingCameraScale, 1e-6);
        EXPECT_GT(kRidingCameraScale, 1.f);
    }

    TEST(SkyrimRidingCamera, aRefusedDeclarationIsReportedNotThrown)
    {
        struct Refusing final : BindingsService
        {
            DeclareResult declareNamespace(const NamespaceDescriptor&) override { return { false, "no" }; }
        } service;
        Rig2 rig;
        RidingBindings bindings(rig.mRiding);
        EXPECT_FALSE(bindings.declareTo(service).ok);
    }

    // ---- a load door --------------------------------------------------------------------------------------------

    TEST(SkyrimRidingLoadDoor, dismountNowPutsTheRiderOnHisFeetBesideTheHorseWithoutAnUpdate)
    {
        Rig2 rig;
        rig.mount();
        ASSERT_TRUE(rig.mRiding.riding());
        rig.mRiding.dismountNow(rig.mCore);
        EXPECT_FALSE(rig.mRiding.riding());
        EXPECT_EQ(rig.mCore.mReleases, 1);
        // beside the horse (it stood at 1000, 2000, 300), not the saddle
        EXPECT_NEAR(rig.mCore.mReleaseZ, 300.0, 1e-6);
        EXPECT_NEAR(std::hypot(rig.mCore.mReleaseX - 1000.0, rig.mCore.mReleaseY - 2000.0), kDismountSide, 1.0);
        EXPECT_EQ(rig.mRiding.dismounts(), 1u);
    }

    TEST(SkyrimRidingLoadDoor, theBrainTakesTheHorseBackAndTheNextUpdateStopsIt)
    {
        Rig2 rig;
        rig.mount();
        rig.mCore.mMovements = 0;
        rig.mRiding.dismountNow(rig.mCore);
        ASSERT_FALSE(rig.mBrain.empty());
        EXPECT_EQ(rig.mBrain.back().first, kRef);
        EXPECT_FALSE(rig.mBrain.back().second);
        rig.mCore.mVelX = 5.f;
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_EQ(rig.mCore.mMovements, 1); // the stop, once
        EXPECT_EQ(rig.mCore.mVelX, 0.f);
        EXPECT_EQ(rig.mCore.mVelY, 0.f);
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_EQ(rig.mCore.mMovements, 1);
        EXPECT_EQ(rig.mCore.mCarries, 1); // the mount's own frame only: nothing carries him after
    }

    TEST(SkyrimRidingLoadDoor, onFootItDoesNothing)
    {
        Rig2 rig;
        rig.mRiding.dismountNow(rig.mCore);
        EXPECT_EQ(rig.mCore.mReleases, 0);
        EXPECT_EQ(rig.mRiding.dismounts(), 0u);
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_EQ(rig.mCore.mMovements, 0);
    }

    TEST(SkyrimRidingLoadDoor, theRulesInterfaceReachesIt)
    {
        Rig2 rig;
        rig.mount();
        RidingRules& rules = rig.mRiding;
        rules.dismountNow(rig.mCore);
        EXPECT_FALSE(rig.mRiding.riding());
    }

    // ---- a save and a load ---------------------------------------------------------------------------------------

    TEST(SkyrimRidingSave, theStateRecordsTheMountAndItsHorse)
    {
        Rig2 rig;
        EXPECT_FALSE(rig.mRiding.savedState().mMounted);
        rig.mount();
        const RidingState state = rig.mRiding.savedState();
        EXPECT_TRUE(state.mMounted);
        EXPECT_EQ(state.mHorse, static_cast<std::uint32_t>(kRef));
        EXPECT_EQ(rig.mRiding.mountReference(), static_cast<std::uint32_t>(kRef));
        rig.mRiding.dismountNow(rig.mCore);
        EXPECT_FALSE(rig.mRiding.savedState().mMounted);
        EXPECT_EQ(rig.mRiding.mountReference(), 0u);
    }

    TEST(SkyrimRidingSave, aLoadPutsThePlayerOnHisFeetUnderTheSaddleAtTheNextUpdate)
    {
        Rig2 rig;
        RidingState saved;
        saved.mMounted = true;
        saved.mHorse = static_cast<std::uint32_t>(kRef);
        rig.mRiding.restoreState(saved);
        EXPECT_FALSE(rig.mRiding.riding());
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_EQ(rig.mCore.mReleases, 1);
        EXPECT_NEAR(rig.mCore.mReleaseX, 500.0, 1e-6);
        EXPECT_NEAR(rig.mCore.mReleaseY, 600.0, 1e-6);
        EXPECT_NEAR(rig.mCore.mReleaseZ, 700.0 - kSaddleZ, 1e-4); // the saddle was where he was saved
        EXPECT_FALSE(rig.mRiding.riding());
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_EQ(rig.mCore.mReleases, 1); // once
    }

    TEST(SkyrimRidingSave, aLoadWaitsForThePlayerToExist)
    {
        Rig2 rig;
        rig.mCore.mHavePlayer = false;
        RidingState saved;
        saved.mMounted = true;
        saved.mHorse = 3;
        rig.mRiding.restoreState(saved);
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_EQ(rig.mCore.mReleases, 0);
        rig.mCore.mHavePlayer = true;
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_EQ(rig.mCore.mReleases, 1);
    }

    TEST(SkyrimRidingSave, aStateOfAPlayerOnFootRestoresNothing)
    {
        Rig2 rig;
        rig.mRiding.restoreState(RidingState());
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_EQ(rig.mCore.mReleases, 0);
    }

    TEST(SkyrimRidingSave, theChunkWritesFiveBytesAndReadsThemBack)
    {
        RidingState held;
        held.mMounted = true;
        held.mHorse = 0x00090000;
        RidingState restored;
        RidingChunk chunk([&] { return held; }, [&](const RidingState& s) { restored = s; });
        EXPECT_EQ(chunk.chunkName(), "riding");
        Memory memory;
        chunk.write(memory);
        ASSERT_EQ(memory.mBytes.size(), 5u);
        EXPECT_EQ(memory.mBytes[0], 1u);
        EXPECT_TRUE(chunk.read(memory, RidingChunk::kVersion));
        EXPECT_EQ(restored, held);
    }

    TEST(SkyrimRidingSave, theChunkRefusesWrongSizesValuesAndVersionsAndChangesNothing)
    {
        RidingState restored;
        restored.mHorse = 9;
        int calls = 0;
        RidingChunk chunk([] { return RidingState(); }, [&](const RidingState& s) {
            restored = s;
            ++calls;
        });
        {
            Memory shortChunk;
            shortChunk.mBytes = { 1, 0, 0, 0 };
            EXPECT_FALSE(chunk.read(shortChunk, 1));
        }
        {
            Memory longChunk;
            longChunk.mBytes = { 1, 0, 0, 0, 0, 0 };
            EXPECT_FALSE(chunk.read(longChunk, 1));
        }
        {
            Memory badFlag;
            badFlag.mBytes = { 2, 0, 0, 0, 0 };
            EXPECT_FALSE(chunk.read(badFlag, 1));
        }
        {
            Memory goodBytes;
            goodBytes.mBytes = { 1, 5, 0, 0, 0 };
            EXPECT_FALSE(chunk.read(goodBytes, 2)); // a version that is not ours
        }
        EXPECT_EQ(calls, 0);
        EXPECT_EQ(restored.mHorse, 9u);
    }

    TEST(SkyrimRidingSave, aChunkSaysOnFootWithAHorseNumberIsStillOnFoot)
    {
        RidingState restored;
        RidingChunk chunk([] { return RidingState(); }, [&](const RidingState& s) { restored = s; });
        Memory memory;
        memory.mBytes = { 0, 7, 0, 0, 0 };
        EXPECT_TRUE(chunk.read(memory, 1));
        EXPECT_FALSE(restored.mMounted);
    }

    // ---- the rules the core finds ---------------------------------------------------------------------------------

    TEST(SkyrimRidingForward, theMountedHorseIsTheHorseHeSitsOnAndNobodyOnFoot)
    {
        Rig2 rig;
        EXPECT_FALSE(rig.mRiding.mountedHorse().valid());
        rig.mount();
        EXPECT_TRUE(rig.mRiding.mountedHorse() == horse());
        rig.mRiding.dismountNow(rig.mCore);
        EXPECT_FALSE(rig.mRiding.mountedHorse().valid());
    }

    TEST(SkyrimRidingForward, theCellSourceForwardsTheLoadDoorDismountAndTheMountedHorse)
    {
        SkyrimWorldView view;
        SkyrimCellSource source(view);
        source.riding().setEnabled(true);
        source.riding().setTestOwned("*");
        source.riding().setHorseLookup([](WorldId base, std::uint32_t, HorseInfo& out) {
            if (base != kBase)
                return false;
            out.mWalkSpeed = kHorseWalk;
            out.mRunSpeed = kHorseRun;
            return true;
        });
        WorldCellSource* asSource = &source;
        auto* actors = dynamic_cast<ActorRules*>(asSource);
        auto* rules = dynamic_cast<RidingRules*>(asSource);
        ASSERT_NE(actors, nullptr);
        ASSERT_NE(rules, nullptr);
        ActorInfo info;
        info.handle = horse();
        info.base = kBase;
        info.editorId = "Horse";
        actors->onActorAdded(info);
        Core2 core;
        core.mControls.valid = true;
        rules->onMountRequested(horse());
        actors->update(0.016f, core);
        ASSERT_TRUE(source.riding().riding());
        EXPECT_TRUE(rules->mountedHorse() == horse());
        rules->dismountNow(core);
        EXPECT_FALSE(source.riding().riding());
        EXPECT_EQ(core.mReleases, 1);
        EXPECT_FALSE(rules->mountedHorse().valid());
    }

    // ---- a load while a ride is in progress -------------------------------------------------------------------

    TEST(SkyrimRidingSave, aLoadOfAStateOnFootEndsARideInProgress)
    {
        Rig2 rig;
        rig.mount();
        ASSERT_TRUE(rig.mRiding.riding());
        const int carries = rig.mCore.mCarries;
        rig.mRiding.restoreState(RidingState());
        EXPECT_FALSE(rig.mRiding.riding());
        EXPECT_FALSE(rig.mBrain.empty());
        EXPECT_FALSE(rig.mBrain.back().second); // the brain has the horse back
        rig.mCore.mMovements = 0;
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_EQ(rig.mCore.mCarries, carries); // nothing carries him now
        EXPECT_EQ(rig.mCore.mReleases, 0); // the load put him where he is: no release
        EXPECT_EQ(rig.mCore.mMovements, 1); // the horse's movement request is cleared once
    }

    TEST(SkyrimRidingSave, aLoadOfARidingStateEndsTheOldRideAndPutsHimOnHisFeet)
    {
        Rig2 rig;
        rig.mount();
        RidingState saved;
        saved.mMounted = true;
        saved.mHorse = static_cast<std::uint32_t>(kRef);
        rig.mRiding.restoreState(saved);
        EXPECT_FALSE(rig.mRiding.riding());
        rig.mRiding.update(0.016f, rig.mCore);
        EXPECT_EQ(rig.mCore.mReleases, 1);
        EXPECT_FALSE(rig.mRiding.riding());
    }
}
