// Task 0204: the crime service and its "skyrimse.crime" bindings over a stub bindings service and fake hooks. Written
// after the service (the pure rules in testskyrimsecrime.cpp were written first); see the stage log of the task.
#include <game_skyrimse/crime_service.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::crime;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;
    using openeden::sdk::game::ActorHandle;
    using openeden::sdk::game::CombatRequestWriter;
    using openeden::sdk::game::CombatSubject;
    using openeden::sdk::game::SubjectState;

    constexpr FormId kHold = 0x01000100;
    constexpr FormId kSword = 0x01000300;

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

    class Writer final : public CombatRequestWriter
    {
    public:
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (!subject.player)
                out.x = 100.0; // the guard is next to the player
            return true;
        }
    };

    struct ChunkOut final : openeden::sdk::ChunkWriter
    {
        void write(const void* d, std::size_t n) override
        {
            const auto* p = static_cast<const unsigned char*>(d);
            bytes.insert(bytes.end(), p, p + n);
        }
        std::vector<unsigned char> bytes;
    };

    struct ChunkIn final : openeden::sdk::ChunkReader
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
    };

    struct ServiceTest : ::testing::Test
    {
        CrimeData mData;
        std::vector<CrimeActor> mActors;
        std::int32_t mGold = 200;
        std::int32_t mSwords = 1;
        bool mJailKnown = true;
        std::vector<std::string> mLog;
        std::unique_ptr<CrimeService> mService;
        StubBindings mBindings;
        Writer mWriter;

        void SetUp() override
        {
            mData.mPresent = true;
            mData.mStealMultiplier = 0.5f;
            mData.mMurder = 1000;
            mData.mAssault = 40;
            CrimeActor guard;
            guard.mHandle = { 10, 1 };
            guard.mBase = 10;
            guard.mGuard = true;
            guard.mCrimeFaction = kHold;
            guard.mName = "guard";
            mActors.push_back(guard);
            CrimeActor witness;
            witness.mHandle = { 11, 1 };
            witness.mBase = 11;
            witness.mCrimeFaction = kHold;
            mActors.push_back(witness);
            ServiceHooks hooks;
            hooks.mActors
                = [this](std::vector<CrimeActor>& out) { out.insert(out.end(), mActors.begin(), mActors.end()); };
            hooks.mActor = [this](ActorHandle h, CrimeActor& out) {
                for (const CrimeActor& a : mActors)
                    if (a.mHandle == h)
                    {
                        out = a;
                        return true;
                    }
                return false;
            };
            hooks.mCrimeData = [this](FormId f) { return f == kHold ? &mData : nullptr; };
            hooks.mFactionName = [](FormId) { return std::string("Whiterun"); };
            hooks.mItemValue = [](FormId) { return 100; };
            hooks.mGold = [this] { return mGold; };
            hooks.mTakeGold = [this](std::int32_t n) {
                if (n > mGold)
                    return false;
                mGold -= n;
                return true;
            };
            hooks.mTakeItem = [this](FormId, std::int32_t n) {
                const std::int32_t taken = std::min(n, mSwords);
                mSwords -= taken;
                return taken;
            };
            hooks.mItemCount = [this](FormId item) { return item == kSword ? mSwords : 0; };
            hooks.mJail = [this](FormId f, JailDestination& out) {
                if (!mJailKnown || f != kHold)
                    return false;
                out.mValid = true;
                out.mCell = "WhiterunJail";
                out.mX = 1.0;
                out.mY = 2.0;
                out.mZ = 3.0;
                out.mHeading = 0.5;
                return true;
            };
            hooks.mLog = [this](const std::string& s) { mLog.push_back(s); };
            mService = std::make_unique<CrimeService>(std::move(hooks));
            ASSERT_TRUE(mService->declare(mBindings).ok);
        }
        std::string text(const std::string& name) { return std::string(mBindings.call(name).value.text); }
        std::int32_t num(const std::string& name) { return mBindings.call(name).value.integer; }
        void run(int seconds)
        {
            for (int i = 0; i < seconds * 2; ++i)
                mService->system().update(0.5f, mWriter);
        }
        void commitAndOffer()
        {
            mService->theft(kSword, 1);
            run(1);
            ASSERT_EQ(mService->ledger().bounty(kHold), 50);
            run(2);
            ASSERT_EQ(text("state"), "offer");
        }
    };

    TEST_F(ServiceTest, theNamespaceIsDeclaredWithItsFunctions)
    {
        EXPECT_EQ(mBindings.mName, "skyrimse.crime");
        EXPECT_EQ(mBindings.mVersion.major, 1u);
        for (const char* name : { "state", "bounty", "offer_bounty", "guard_name", "can_pay", "message", "stolen_count",
                 "revision", "jail_available", "jail_cell", "jail_x", "jail_y", "jail_z", "jail_heading",
                 "jail_seconds", "choose", "released" })
            EXPECT_NE(mBindings.mFunctions.find(name), mBindings.mFunctions.end()) << name;
    }

    TEST_F(ServiceTest, aTheftShowsInTheReadFunctions)
    {
        EXPECT_EQ(text("state"), "none");
        EXPECT_EQ(num("bounty"), 0);
        mService->theft(kSword, 1);
        EXPECT_EQ(num("stolen_count"), 1);
        run(1);
        EXPECT_EQ(num("bounty"), 50);
    }

    TEST_F(ServiceTest, theRevisionMovesWithTheLedgerAndTheState)
    {
        const std::int32_t r0 = num("revision");
        mService->theft(kSword, 1);
        EXPECT_NE(num("revision"), r0);
    }

    TEST_F(ServiceTest, aGuardOffersAndPayThroughTheBindingTakesGold)
    {
        commitAndOffer();
        EXPECT_EQ(text("guard_name"), "guard");
        EXPECT_EQ(num("offer_bounty"), 50);
        EXPECT_TRUE(mBindings.call("can_pay").value.boolean);
        EXPECT_FALSE(text("message").empty());
        mBindings.call("choose", { str("pay") });
        EXPECT_EQ(mGold, 150);
        EXPECT_EQ(num("bounty"), 0);
        EXPECT_EQ(mSwords, 0) << "the stolen sword is confiscated";
        EXPECT_EQ(text("state"), "none");
    }

    TEST_F(ServiceTest, aRefusedChoiceIsLogged)
    {
        mBindings.call("choose", { str("pay") });
        bool refused = false;
        for (const std::string& line : mLog)
            if (line.find("refused") != std::string::npos)
                refused = true;
        EXPECT_TRUE(refused);
    }

    TEST_F(ServiceTest, jailReportsTheDestinationAndReleasedEndsIt)
    {
        commitAndOffer();
        EXPECT_TRUE(mBindings.call("jail_available").value.boolean);
        mBindings.call("choose", { str("jail") });
        EXPECT_EQ(text("state"), "jailed");
        EXPECT_EQ(text("jail_cell"), "WhiterunJail");
        EXPECT_DOUBLE_EQ(mBindings.call("jail_z").value.number, 3.0);
        EXPECT_DOUBLE_EQ(mBindings.call("jail_heading").value.number, 0.5);
        EXPECT_GT(mBindings.call("jail_seconds").value.number, 0.0);
        mBindings.call("released");
        EXPECT_EQ(text("state"), "none");
        EXPECT_EQ(num("bounty"), 0);
    }

    TEST_F(ServiceTest, aFactionWithNoJailClearsTheBountyAndSaysWhy)
    {
        mJailKnown = false;
        commitAndOffer();
        EXPECT_FALSE(mBindings.call("jail_available").value.boolean);
        mBindings.call("choose", { str("jail") });
        EXPECT_EQ(text("state"), "none");
        EXPECT_EQ(num("bounty"), 0);
        EXPECT_NE(text("message").find("no jail"), std::string::npos);
    }

    TEST_F(ServiceTest, stolenHeldNeverExceedsWhatThePlayerHolds)
    {
        mService->ledger().markStolen(kSword, 5);
        EXPECT_EQ(mService->stolenHeld(kSword), 1);
        mSwords = 0;
        EXPECT_EQ(mService->stolenHeld(kSword), 0);
        EXPECT_EQ(mService->stolenHeld(kSword + 1), 0);
    }

    TEST_F(ServiceTest, theSavedLedgerComesBackThroughTheChunk)
    {
        mService->ledger().addBounty(kHold, 77);
        mService->ledger().markStolen(kSword, 2);
        ChunkOut w;
        mService->chunk().write(w);
        ChunkIn r;
        r.bytes = w.bytes;
        mService->ledger().clear();
        ASSERT_TRUE(mService->chunk().read(r, 1));
        EXPECT_EQ(mService->ledger().bounty(kHold), 77);
        EXPECT_EQ(mService->ledger().stolen(kSword), 2);
    }
}
