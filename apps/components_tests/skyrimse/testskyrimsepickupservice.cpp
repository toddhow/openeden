// Task 0208: the pickup service over fake hooks and a stub bindings service: picking up a placed item (the order of the
// steps, theft, a script's OnActivate, the saved set) and a pickpocket try (the seeded roll, training, a caught try).
#include <game_skyrimse/pickup_service.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::pickup;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    constexpr FormId kSword = 0x00012eb7;
    constexpr FormId kGold = 0x0000000f;
    constexpr FormId kPlacement = 0x0009cafb;
    constexpr std::uint64_t kHulda = 0x00b80803;

    class StubBindings final : public openeden::sdk::BindingsService
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
            if (it == mFunctions.end())
                return {};
            EXPECT_EQ(args.size(), it->second.params.size()) << name;
            return it->second.handler->call(args);
        }
        std::string mName;
        openeden::sdk::Version mVersion{ 0, 0 };
        std::map<std::string, openeden::sdk::FunctionDescriptor> mFunctions;
    };

    BindingValue str(std::string_view s)
    {
        BindingValue v;
        v.type = BindingType::String;
        v.text = s;
        return v;
    }

    BindingValue num(std::int32_t i)
    {
        BindingValue v;
        v.type = BindingType::Int32;
        v.integer = i;
        return v;
    }

    struct Fixture : ::testing::Test
    {
        std::vector<std::string> mSteps; // the order of the hook calls
        std::map<FormId, std::pair<std::int32_t, float>> mItems{ { kSword, { 100, 8.f } }, { kGold, { 1, 0.f } } };
        std::int32_t mXcnt = 0;
        bool mPlayerFull = false;
        bool mDisableOk = true;
        std::map<FormId, std::int32_t> mPlayer;
        std::map<FormId, std::int32_t> mHulda{ { kSword, 2 }, { kGold, 40 } };
        float mSkill = 50.f;
        float mTrained = 0.f;
        int mCaughtCalls = 0;
        std::int32_t mCaughtCount = 0;
        std::vector<std::string> mLog;
        std::unique_ptr<PickupService> mService;

        Hooks hooks()
        {
            Hooks h;
            h.mItem = [this](FormId item, std::int32_t& value, float& weight) {
                const auto it = mItems.find(item);
                if (it == mItems.end())
                    return false;
                value = it->second.first;
                weight = it->second.second;
                return true;
            };
            h.mCount = [this](FormId) { return mXcnt; };
            h.mAddToPlayer = [this](FormId item, std::int32_t count) {
                mSteps.push_back("add");
                if (mPlayerFull)
                    return false;
                mPlayer[item] += count;
                return true;
            };
            h.mDisable = [this](FormId) {
                mSteps.push_back("disable");
                return mDisableOk;
            };
            h.mActivated = [this](FormId) { mSteps.push_back("activated"); };
            // the owner check is the pack's: an unowned placement makes this hook do nothing
            h.mTaken = [](FormId, FormId, std::int32_t) {};
            h.mHeldBy = [this](std::uint64_t, FormId item) {
                const auto it = mHulda.find(item);
                return it == mHulda.end() ? 0 : it->second;
            };
            h.mMoveToPlayer = [this](std::uint64_t, FormId item, std::int32_t count) {
                mHulda[item] -= count;
                mPlayer[item] += count;
                return true;
            };
            h.mSkill = [this]() { return mSkill; };
            h.mTrain = [this](float amount) { mTrained += amount; };
            h.mCaught = [this](std::uint64_t, FormId, std::int32_t count) {
                ++mCaughtCalls;
                mCaughtCount = count;
            };
            h.mLog = [this](const std::string& line) { mLog.push_back(line); };
            return h;
        }

        void SetUp() override { mService = std::make_unique<PickupService>(hooks()); }
    };

    // ---- pickup -----------------------------------------------------------------------------------------------------

    TEST_F(Fixture, aPickupGivesTheItemRemovesTheObjectAndRemembersIt)
    {
        EXPECT_EQ(mService->pickup(kPlacement, kSword), PickupStatus::Ok);
        EXPECT_EQ(mPlayer[kSword], 1);
        EXPECT_TRUE(mService->book().taken(kPlacement));
        EXPECT_EQ(mService->state(), "taken");
        const std::vector<std::string> expect{ "add", "activated", "disable" };
        EXPECT_EQ(mSteps, expect); // an unowned item: no theft step
    }

    TEST_F(Fixture, aPlacedStackKeepsItsCount)
    {
        mXcnt = 25;
        EXPECT_EQ(mService->pickup(kPlacement, kGold), PickupStatus::Ok);
        EXPECT_EQ(mPlayer[kGold], 25);
    }

    TEST_F(Fixture, aSecondPickupOfOnePlacementIsRefused)
    {
        EXPECT_EQ(mService->pickup(kPlacement, kSword), PickupStatus::Ok);
        mSteps.clear();
        EXPECT_EQ(mService->pickup(kPlacement, kSword), PickupStatus::AlreadyTaken);
        EXPECT_EQ(mPlayer[kSword], 1);
        EXPECT_TRUE(mSteps.empty());
    }

    TEST_F(Fixture, aFormThatIsNoItemIsRefusedAndNothingChanges)
    {
        EXPECT_EQ(mService->pickup(kPlacement, 0x1234), PickupStatus::NotAnItem);
        EXPECT_FALSE(mService->book().taken(kPlacement));
        EXPECT_TRUE(mSteps.empty());
    }

    TEST_F(Fixture, aPlayerThatCannotReceiveLeavesTheObjectInTheWorld)
    {
        mPlayerFull = true;
        EXPECT_EQ(mService->pickup(kPlacement, kSword), PickupStatus::Refused);
        EXPECT_FALSE(mService->book().taken(kPlacement));
        EXPECT_EQ(mSteps, std::vector<std::string>{ "add" });
    }

    TEST_F(Fixture, aFailedDisableIsStillRememberedSoTheItemIsNotTakenTwice)
    {
        mDisableOk = false;
        EXPECT_EQ(mService->pickup(kPlacement, kSword), PickupStatus::Ok);
        EXPECT_TRUE(mService->book().taken(kPlacement));
        EXPECT_EQ(mService->pickup(kPlacement, kSword), PickupStatus::AlreadyTaken);
    }

    TEST_F(Fixture, anOwnedItemIsTheftBeforeItLeavesTheWorld)
    {
        Hooks h = hooks();
        h.mTaken = [this](FormId placement, FormId item, std::int32_t count) {
            mSteps.push_back("theft");
            EXPECT_EQ(placement, kPlacement);
            EXPECT_EQ(item, kSword);
            EXPECT_EQ(count, 1);
        };
        mService = std::make_unique<PickupService>(h);
        EXPECT_EQ(mService->pickup(kPlacement, kSword), PickupStatus::Ok);
        const std::vector<std::string> expect{ "add", "theft", "activated", "disable" };
        EXPECT_EQ(mSteps, expect);
    }

    TEST_F(Fixture, aMissingHookRefusesInsteadOfCrashing)
    {
        Hooks h;
        PickupService bare(h);
        EXPECT_EQ(bare.pickup(kPlacement, kSword), PickupStatus::NotAnItem);
        EXPECT_EQ(bare.take(kHulda, kSword, 1), TryStatus::Refused);
        EXPECT_EQ(bare.chance(kHulda, kSword, 1), 0.f);
    }

    TEST_F(Fixture, theRevisionMovesAfterAPickup)
    {
        const std::uint32_t before = mService->revision();
        EXPECT_EQ(mService->pickup(kPlacement, kSword), PickupStatus::Ok);
        EXPECT_NE(mService->revision(), before);
    }

    TEST_F(Fixture, theSavedSetSurvivesAChunkRoundTrip)
    {
        EXPECT_EQ(mService->pickup(kPlacement, kSword), PickupStatus::Ok);
        struct Out final : openeden::sdk::ChunkWriter
        {
            void write(const void* d, std::size_t n) override
            {
                const auto* p = static_cast<const unsigned char*>(d);
                bytes.insert(bytes.end(), p, p + n);
            }
            std::vector<unsigned char> bytes;
        } out;
        mService->chunk().write(out);
        struct In final : openeden::sdk::ChunkReader
        {
            bool read(void* d, std::size_t n) override
            {
                if (n > remaining())
                    return false;
                std::memcpy(d, bytes.data() + pos, n);
                pos += n;
                return true;
            }
            std::size_t remaining() const override { return bytes.size() - pos; }
            std::vector<unsigned char> bytes;
            std::size_t pos = 0;
        } in;
        in.bytes = out.bytes;
        PickupService other(hooks());
        ASSERT_TRUE(other.chunk().read(in, 1));
        EXPECT_TRUE(other.book().taken(kPlacement));
        EXPECT_EQ(other.pickup(kPlacement, kSword), PickupStatus::AlreadyTaken);
    }

    // ---- pickpocket -------------------------------------------------------------------------------------------------

    TEST_F(Fixture, theChanceFollowsTheRulesOverTheHooks)
    {
        // skill 50, value 100, weight 8: 25 + 30 - 30 - 12 = 13
        EXPECT_NEAR(mService->chance(kHulda, kSword, 1), 13.f, 1e-3f);
        EXPECT_EQ(mService->chance(kHulda, 0x4321, 1), 0.f); // unknown item
    }

    TEST_F(Fixture, everyTryTrainsThePickpocketSkillWhateverItsOutcome)
    {
        const TryStatus first = mService->take(kHulda, kGold, 5);
        EXPECT_TRUE(first == TryStatus::Success || first == TryStatus::Caught);
        EXPECT_FLOAT_EQ(mTrained, 5.f);
        (void)mService->take(kHulda, kGold, 1);
        EXPECT_FLOAT_EQ(mTrained, 6.f);
    }

    TEST_F(Fixture, aSuccessMovesTheItemsAndATryCountsAsAnAttempt)
    {
        mSkill = 100.f; // gold: 25 + 60 - 3 * sqrt(1) = 82%: pick the first attempt that succeeds, deterministically
        int successes = 0;
        for (int i = 0; i < 40; ++i)
        {
            const std::int32_t before = mPlayer[kGold];
            const TryStatus status = mService->take(kHulda, kGold, 1);
            if (status == TryStatus::Success)
            {
                ++successes;
                EXPECT_EQ(mPlayer[kGold], before + 1);
                EXPECT_EQ(mService->state(), "success");
            }
            else
            {
                EXPECT_EQ(status, TryStatus::Caught);
                EXPECT_EQ(mPlayer[kGold], before); // a caught try takes nothing
                EXPECT_EQ(mService->state(), "caught");
            }
        }
        EXPECT_EQ(mService->book().attempts(), 40u);
        EXPECT_GT(successes, 25);
        EXPECT_LT(successes, 40);
        EXPECT_EQ(mCaughtCalls, 40 - successes);
    }

    TEST_F(Fixture, theOutcomesAreTheSameInEveryRun)
    {
        const auto run = [this]() {
            mHulda = { { kSword, 2 }, { kGold, 40 } };
            mPlayer.clear();
            PickupService service(hooks());
            std::string out;
            for (int i = 0; i < 30; ++i)
                out += service.take(kHulda, kGold, 1) == TryStatus::Success ? 'S' : 'C';
            return out;
        };
        EXPECT_EQ(run(), run());
    }

    TEST_F(Fixture, aCaughtTryTellsTheCrimeHookTheVictimAndTheCount)
    {
        mSkill = 0.f;
        for (int i = 0; i < 60 && mCaughtCalls == 0; ++i)
            (void)mService->take(kHulda, kSword, 2);
        ASSERT_GT(mCaughtCalls, 0); // 5% (the floor) per try: caught within 60 tries by the seeded rolls
        EXPECT_EQ(mCaughtCount, 2);
        EXPECT_FALSE(mService->message().empty());
    }

    TEST_F(Fixture, aBadCountIsRefusedWithoutARollOrTraining)
    {
        EXPECT_EQ(mService->take(kHulda, kSword, 0), TryStatus::BadCount);
        EXPECT_EQ(mService->take(kHulda, kSword, -1), TryStatus::BadCount);
        EXPECT_EQ(mService->take(kHulda, kSword, 3), TryStatus::BadCount); // she holds 2
        EXPECT_EQ(mService->take(kHulda, 0x9999, 1), TryStatus::BadCount); // she holds none
        EXPECT_EQ(mService->book().attempts(), 0u);
        EXPECT_FLOAT_EQ(mTrained, 0.f);
    }

    // ---- bindings ---------------------------------------------------------------------------------------------------

    TEST_F(Fixture, theBindingsAreDeclaredAndDriveTheService)
    {
        StubBindings bindings;
        ASSERT_TRUE(mService->declare(bindings).ok);
        EXPECT_EQ(bindings.mName, "skyrimse.pickup");
        EXPECT_EQ(bindings.mVersion.major, 1);
        EXPECT_EQ(bindings.mVersion.minor, 0);
        for (const char* name : { "state", "message", "revision", "chance", "pickup", "take" })
            EXPECT_EQ(bindings.mFunctions.count(name), 1u) << name;
        EXPECT_EQ(bindings.mFunctions.at("pickup").access, openeden::sdk::BindingAccess::Mutate);
        EXPECT_EQ(bindings.mFunctions.at("take").access, openeden::sdk::BindingAccess::Mutate);
        EXPECT_EQ(bindings.mFunctions.at("chance").access, openeden::sdk::BindingAccess::Read);

        bindings.call("pickup", { str("0009cafb"), str("00012eb7") });
        EXPECT_EQ(mPlayer[kSword], 1);
        EXPECT_EQ(std::string(bindings.call("state").value.text), "taken");
        const auto chance = bindings.call("chance", { str("00b80803"), str("00012eb7") });
        EXPECT_EQ(chance.value.integer, 13);
        bindings.call("take", { str("00b80803"), str("0000000f"), num(1) });
        EXPECT_EQ(mService->book().attempts(), 1u);
    }

    TEST_F(Fixture, aBadIdArgumentIsAnErrorAndChangesNothing)
    {
        StubBindings bindings;
        ASSERT_TRUE(mService->declare(bindings).ok);
        const auto r = bindings.call("pickup", { str("zz"), str("00012eb7") });
        EXPECT_NE(r.status, openeden::sdk::BindingStatus::Ok);
        EXPECT_FALSE(mService->book().taken(kPlacement));
        EXPECT_TRUE(mPlayer.empty());
    }
}
