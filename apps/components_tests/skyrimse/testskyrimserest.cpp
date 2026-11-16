// Task 0210: waiting and sleeping in a Skyrim SE session (game_packs/skyrimse rest.*): the rules, the Rested state, its
// save chunk and the bindings `skyrimse.rest`.
#include <game_skyrimse/rest.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse::rest;
    namespace EntryBit = game_skyrimse::EntryBit;
    namespace MarkerType = game_skyrimse::MarkerType;
    using game_skyrimse::FurnitureMarker;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
    constexpr double kInf = std::numeric_limits<double>::infinity();

    // ---- the rules --------------------------------------------------------------------------------------------------

    TEST(SkyrimRestRules, theFirstReasonThatHoldsWinsInTheDocumentedOrder)
    {
        EXPECT_EQ(decideRest(true, false, false), Refusal::None);
        EXPECT_EQ(decideRest(false, true, true), Refusal::NotReady);
        EXPECT_EQ(decideRest(true, true, true), Refusal::InCombat);
        EXPECT_EQ(decideRest(true, false, true), Refusal::EnemiesNear);
    }

    TEST(SkyrimRestRules, refusalsHaveStableLowerCaseNames)
    {
        EXPECT_EQ(refusalName(Refusal::None), "");
        EXPECT_EQ(refusalName(Refusal::NotReady), "not_ready");
        EXPECT_EQ(refusalName(Refusal::InCombat), "combat");
        EXPECT_EQ(refusalName(Refusal::EnemiesNear), "enemies");
    }

    TEST(SkyrimRestRules, hoursAreOneToTwentyFour)
    {
        EXPECT_EQ(clampHours(0), 1);
        EXPECT_EQ(clampHours(-5), 1);
        EXPECT_EQ(clampHours(3), 3);
        EXPECT_EQ(clampHours(3.9), 3);
        EXPECT_EQ(clampHours(24), 24);
        EXPECT_EQ(clampHours(100), 24);
        EXPECT_EQ(clampHours(kNan), 1);
        EXPECT_EQ(clampHours(kInf), 1);
    }

    TEST(SkyrimRestRules, gameHoursBecomeRealSecondsByTheTimeScale)
    {
        EXPECT_DOUBLE_EQ(regenSeconds(3, 20), 540.0);
        EXPECT_DOUBLE_EQ(regenSeconds(1, 30), 120.0);
        // a scale that is no number or not above 0 counts as 1
        EXPECT_DOUBLE_EQ(regenSeconds(2, 0), 7200.0);
        EXPECT_DOUBLE_EQ(regenSeconds(2, -3), 7200.0);
        EXPECT_DOUBLE_EQ(regenSeconds(2, kNan), 7200.0);
        // bad hours give nothing; the result is capped
        EXPECT_DOUBLE_EQ(regenSeconds(kNan, 20), 0.0);
        EXPECT_DOUBLE_EQ(regenSeconds(-1, 20), 0.0);
        EXPECT_DOUBLE_EQ(regenSeconds(1e9, 1), kMaxRegenSeconds);
    }

    TEST(SkyrimRestRules, bedStatesHaveNames)
    {
        EXPECT_EQ(bedStateName(BedState::NotABed), "none");
        EXPECT_EQ(bedStateName(BedState::Free), "free");
        EXPECT_EQ(bedStateName(BedState::Owned), "owned");
    }

    TEST(SkyrimRestBeds, aModelWithASleepMarkerIsABed)
    {
        FurnitureMarker sit;
        sit.mType = MarkerType::Sit;
        sit.mEntry = EntryBit::Front;
        FurnitureMarker sleep;
        sleep.mType = MarkerType::Sleep;
        sleep.mEntry = EntryBit::Left | EntryBit::Right;
        EXPECT_FALSE(hasSleepMarker({}));
        EXPECT_FALSE(hasSleepMarker({ sit }));
        EXPECT_TRUE(hasSleepMarker({ sleep }));
        EXPECT_TRUE(hasSleepMarker({ sit, sleep }));
        FurnitureMarker lean = sit;
        lean.mType = 3;
        EXPECT_FALSE(hasSleepMarker({ lean }));
    }

    TEST(SkyrimRestBeds, theStateIsNotABedFreeOrOwned)
    {
        EXPECT_EQ(bedStateOf(false, false), BedState::NotABed);
        EXPECT_EQ(bedStateOf(false, true), BedState::NotABed);
        EXPECT_EQ(bedStateOf(true, false), BedState::Free);
        EXPECT_EQ(bedStateOf(true, true), BedState::Owned);
    }

    // ---- Rested -----------------------------------------------------------------------------------------------------

    TEST(SkyrimRested, startsInactive)
    {
        RestedState r;
        EXPECT_FALSE(r.active(0));
        EXPECT_DOUBLE_EQ(r.hoursLeft(0), 0.0);
        EXPECT_FLOAT_EQ(r.skillGain(0), 1.f);
    }

    TEST(SkyrimRested, sleepGrantsEightHoursAndTheGainWhileItLasts)
    {
        RestedState r;
        ASSERT_TRUE(r.grant(1000.0, 1));
        EXPECT_TRUE(r.active(1000.0));
        EXPECT_TRUE(r.active(1000.0 + kRestedSeconds - 1.0));
        EXPECT_FALSE(r.active(1000.0 + kRestedSeconds));
        EXPECT_NEAR(r.hoursLeft(1000.0 + 3600.0), 7.0, 1e-9);
        EXPECT_FLOAT_EQ(r.skillGain(1000.0), kRestedSkillGain);
        EXPECT_FLOAT_EQ(r.skillGain(1000.0 + kRestedSeconds + 1.0), 1.f);
    }

    TEST(SkyrimRested, aSleepShorterThanTheMinimumGrantsNothingAndBadTimesAreRefused)
    {
        RestedState r;
        EXPECT_FALSE(r.grant(1000.0, 0));
        EXPECT_FALSE(r.grant(kNan, 8));
        EXPECT_FALSE(r.grant(kInf, 8));
        EXPECT_FALSE(r.active(1000.0));
        EXPECT_EQ(r.revision(), 0u);
    }

    TEST(SkyrimRested, aSecondSleepRestartsTheCountAndDoesNotAdd)
    {
        RestedState r;
        ASSERT_TRUE(r.grant(0.0, 8));
        ASSERT_TRUE(r.grant(3600.0, 8));
        EXPECT_NEAR(r.expiresAt(), 3600.0 + kRestedSeconds, 1e-9);
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

    std::vector<unsigned char> doubleBytes(double v)
    {
        std::vector<unsigned char> bytes(8);
        std::memcpy(bytes.data(), &v, 8);
        return bytes;
    }

    TEST(SkyrimRestChunk, theExpiryRoundTrips)
    {
        RestedState state;
        ASSERT_TRUE(state.grant(5000.0, 8));
        RestChunk chunk(state);
        MemoryWriter writer;
        chunk.write(writer);
        EXPECT_EQ(writer.mBytes.size(), 8u);
        RestedState fresh;
        RestChunk readBack(fresh);
        std::string restored;
        readBack.setRestoredSink([&](const std::string& s) { restored = s; });
        MemoryReader reader(writer.mBytes);
        ASSERT_TRUE(readBack.read(reader, RestChunk::kVersion));
        EXPECT_DOUBLE_EQ(fresh.expiresAt(), state.expiresAt());
        EXPECT_FALSE(restored.empty());
    }

    TEST(SkyrimRestChunk, aWrongVersionSizeOrNumberIsRefusedAndChangesNothing)
    {
        RestedState state;
        ASSERT_TRUE(state.grant(5000.0, 8));
        const double before = state.expiresAt();
        RestChunk chunk(state);
        std::string reason;
        chunk.setRefusalSink([&](const std::string& r) { reason = r; });
        const auto expectRefused = [&](const std::vector<unsigned char>& bytes, std::uint32_t version) {
            MemoryReader reader(bytes);
            EXPECT_FALSE(chunk.read(reader, version));
            EXPECT_FALSE(reason.empty());
            reason.clear();
            EXPECT_DOUBLE_EQ(state.expiresAt(), before);
        };
        expectRefused(doubleBytes(1.0), 2);
        expectRefused({}, 1);
        expectRefused(std::vector<unsigned char>(7, 0), 1);
        expectRefused(std::vector<unsigned char>(9, 0), 1);
        expectRefused(doubleBytes(kNan), 1);
        expectRefused(doubleBytes(kInf), 1);
    }

    TEST(SkyrimRestChunk, aNegativeExpiryMeansNoRestedState)
    {
        RestedState state;
        ASSERT_TRUE(state.grant(5000.0, 8));
        RestChunk chunk(state);
        const std::vector<unsigned char> bytes = doubleBytes(-1.0);
        MemoryReader reader(bytes);
        ASSERT_TRUE(chunk.read(reader, 1));
        EXPECT_FALSE(state.active(5000.0));
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

    struct RestFixture : testing::Test
    {
        bool mReady = true;
        bool mCombat = false;
        bool mEnemies = false;
        BedState mBed = BedState::Free;
        struct Applied
        {
            double mSeconds;
            int mHours;
            bool mSleeping;
        };
        std::vector<Applied> mApplied;
        std::uint64_t mAskedPlacement = 0, mAskedBase = 0;
        std::unique_ptr<RestService> mService;
        FakeBindings mBindings;

        void SetUp() override
        {
            Hooks hooks;
            hooks.mReady = [this] { return mReady; };
            hooks.mInCombat = [this] { return mCombat; };
            hooks.mEnemiesNear = [this] { return mEnemies; };
            hooks.mBed = [this](std::uint64_t placement, std::uint64_t base) {
                mAskedPlacement = placement;
                mAskedBase = base;
                return mBed;
            };
            hooks.mApply = [this](double seconds, int hours, bool sleeping) {
                mApplied.push_back({ seconds, hours, sleeping });
            };
            mService = std::make_unique<RestService>(std::move(hooks));
            ASSERT_TRUE(mService->declare(mBindings).ok);
        }
    };

    TEST_F(RestFixture, theNamespaceIsDeclaredWithItsVersionAndFunctions)
    {
        EXPECT_EQ(mBindings.mName, "skyrimse.rest");
        EXPECT_EQ(mBindings.mVersion.major, 1);
        for (const char* name : { "ready", "max_hours", "can_rest", "bed_state", "rested", "rested_hours_left", "rest",
                 "set_clock" })
            EXPECT_TRUE(mBindings.mFunctions.count(name)) << name;
        EXPECT_EQ(mBindings.mFunctions["rest"].access, openeden::sdk::BindingAccess::Mutate);
        EXPECT_EQ(mBindings.mFunctions["set_clock"].access, openeden::sdk::BindingAccess::Mutate);
        EXPECT_EQ(mBindings.mFunctions["can_rest"].access, openeden::sdk::BindingAccess::Read);
    }

    TEST_F(RestFixture, theReadFunctionsAnswerFromTheHooks)
    {
        EXPECT_TRUE(mBindings.call("ready").value.boolean);
        EXPECT_EQ(mBindings.call("max_hours").value.integer, kMaxWaitHours);
        EXPECT_EQ(std::string(mBindings.call("can_rest").value.text), "");
        mCombat = true;
        EXPECT_EQ(std::string(mBindings.call("can_rest").value.text), "combat");
        mCombat = false;
        mEnemies = true;
        EXPECT_EQ(std::string(mBindings.call("can_rest").value.text), "enemies");
        mReady = false;
        EXPECT_EQ(std::string(mBindings.call("can_rest").value.text), "not_ready");
        EXPECT_FALSE(mBindings.call("ready").value.boolean);
    }

    TEST_F(RestFixture, aBedIsAskedForByHexIdsAndAnswersByName)
    {
        mBed = BedState::Owned;
        EXPECT_EQ(std::string(mBindings.call("bed_state", { str("0009cafb"), str("000a1b2c") }).value.text), "owned");
        EXPECT_EQ(mAskedPlacement, 0x9cafbu);
        EXPECT_EQ(mAskedBase, 0xa1b2cu);
        mBed = BedState::NotABed;
        EXPECT_EQ(std::string(mBindings.call("bed_state", { str("1"), str("2") }).value.text), "none");
        // text that is not hex is no bed and never reaches the hook
        mAskedPlacement = 77;
        EXPECT_EQ(std::string(mBindings.call("bed_state", { str("zz"), str("2") }).value.text), "none");
        EXPECT_EQ(mAskedPlacement, 77u);
        EXPECT_EQ(std::string(mBindings.call("bed_state", { str(""), str("2") }).value.text), "none");
    }

    TEST_F(RestFixture, aWaitAppliesTheRegenerationOnceAndGrantsNothing)
    {
        mBindings.call("set_clock", { dbl(1000.0) });
        mBindings.call("rest", { i32(3), dbl(540.0), flag(false) });
        ASSERT_EQ(mApplied.size(), 1u);
        EXPECT_EQ(mApplied[0].mHours, 3);
        EXPECT_DOUBLE_EQ(mApplied[0].mSeconds, 540.0);
        EXPECT_FALSE(mApplied[0].mSleeping);
        EXPECT_FALSE(mBindings.call("rested").value.boolean);
        EXPECT_EQ(mService->performed(), 1u);
    }

    TEST_F(RestFixture, sleepingGrantsRestedFromTheClockTheScriptPassedIn)
    {
        mBindings.call("set_clock", { dbl(1000.0) });
        mBindings.call("rest", { i32(8), dbl(1440.0), flag(true) });
        ASSERT_EQ(mApplied.size(), 1u);
        EXPECT_TRUE(mApplied[0].mSleeping);
        EXPECT_TRUE(mBindings.call("rested").value.boolean);
        EXPECT_NEAR(mBindings.call("rested_hours_left").value.number, 8.0, 1e-9);
        // the clock moves on: the state ends after eight game hours
        mBindings.call("set_clock", { dbl(1000.0 + 3600.0 * 5) });
        EXPECT_NEAR(mBindings.call("rested_hours_left").value.number, 3.0, 1e-9);
        mBindings.call("set_clock", { dbl(1000.0 + 3600.0 * 9) });
        EXPECT_FALSE(mBindings.call("rested").value.boolean);
    }

    TEST_F(RestFixture, hoursAndSecondsAreCutToTheirRangesBeforeTheHook)
    {
        mBindings.call("rest", { i32(99), dbl(1e12), flag(false) });
        mBindings.call("rest", { i32(-4), dbl(-5.0), flag(false) });
        mBindings.call("rest", { i32(2), dbl(kNan), flag(false) });
        ASSERT_EQ(mApplied.size(), 3u);
        EXPECT_EQ(mApplied[0].mHours, kMaxWaitHours);
        EXPECT_DOUBLE_EQ(mApplied[0].mSeconds, kMaxRegenSeconds);
        EXPECT_EQ(mApplied[1].mHours, kMinWaitHours);
        EXPECT_DOUBLE_EQ(mApplied[1].mSeconds, 0.0);
        EXPECT_DOUBLE_EQ(mApplied[2].mSeconds, 0.0);
    }

    TEST_F(RestFixture, aRestIsRefusedInCombatWithEnemiesNearOrBeforeTheDataIsReadAndChangesNothing)
    {
        mCombat = true;
        mBindings.call("rest", { i32(3), dbl(10.0), flag(true) });
        mCombat = false;
        mEnemies = true;
        mBindings.call("rest", { i32(3), dbl(10.0), flag(true) });
        mEnemies = false;
        mReady = false;
        mBindings.call("rest", { i32(3), dbl(10.0), flag(true) });
        EXPECT_TRUE(mApplied.empty());
        EXPECT_EQ(mService->refused(), 3u);
        EXPECT_EQ(mService->performed(), 0u);
        mReady = true;
        EXPECT_FALSE(mBindings.call("rested").value.boolean);
    }

    TEST_F(RestFixture, aClockThatIsNotANumberIsIgnored)
    {
        mBindings.call("set_clock", { dbl(500.0) });
        mBindings.call("set_clock", { dbl(kNan) });
        EXPECT_DOUBLE_EQ(mService->now(), 500.0);
    }

    TEST(SkyrimRestService, withoutHooksNothingIsReadyAndNothingHappens)
    {
        RestService service{ Hooks() };
        FakeBindings bindings;
        ASSERT_TRUE(service.declare(bindings).ok);
        EXPECT_FALSE(bindings.call("ready").value.boolean);
        EXPECT_EQ(std::string(bindings.call("can_rest").value.text), "not_ready");
        bindings.call("rest", { i32(3), dbl(10.0), flag(true) });
        EXPECT_EQ(service.performed(), 0u);
        EXPECT_EQ(std::string(bindings.call("bed_state", { str("1"), str("2") }).value.text), "none");
    }
}
