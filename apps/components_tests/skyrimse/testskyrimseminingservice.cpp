// Task 0224: the mining service over fake hooks and a stub bindings service: the tool check, a strike's yield, the
// depleting drop, regrowth by the clock, the chopping block, the displayed name and the namespace skyrimse.mining.
#include <game_skyrimse/mining_service.hpp>

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::mining;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    constexpr FormId kIronVein = 0x0010dcc9; // an ACTI base
    constexpr FormId kGemVein = 0x0010dcca; // an ACTI base with a gem list
    constexpr FormId kBlock = 0x0007022e; // the chopping block base
    constexpr FormId kBareActivator = 0x0010dccb; // not a resource
    constexpr FormId kIronOre = 0x0005ace4;
    constexpr FormId kFirewood = 0x0006f993;
    constexpr FormId kToolList = 0x000e3c16;
    constexpr FormId kAxeList = 0x00070230;
    constexpr FormId kGemList = 0x000f0000;
    constexpr FormId kGarnet = 0x0006851f;
    constexpr FormId kFailMsg = 0x000a2bdf;
    constexpr FormId kPlacementA = 0x000a1001;
    constexpr FormId kPlacementB = 0x000a1002;

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

    BindingValue dbl(double d)
    {
        BindingValue v;
        v.type = BindingType::Double;
        v.number = d;
        return v;
    }

    struct MiningServiceFixture : ::testing::Test
    {
        std::map<FormId, ResourceInfo> mResources;
        std::map<FormId, std::int32_t> mPlayer;
        std::map<FormId, int> mToolCounts; // by list form
        bool mPlayerFull = false;
        bool mMessagesReadable = false;
        std::vector<std::string> mLog;
        std::vector<std::uint64_t> mSeeds;
        std::unique_ptr<MiningService> mService;

        void SetUp() override
        {
            ResourceInfo iron;
            iron.mKind = ResourceKind::Ore;
            iron.mItem = kIronOre;
            iron.mToolList = kToolList;
            iron.mFailureMessage = kFailMsg;
            mResources[kIronVein] = iron;
            ResourceInfo gems = iron;
            gems.mGems = kGemList;
            gems.mTotal = 2;
            gems.mPerDrop = 2;
            mResources[kGemVein] = gems;
            ResourceInfo block;
            block.mKind = ResourceKind::Wood;
            block.mItem = kFirewood;
            block.mToolList = kAxeList;
            mResources[kBlock] = block;

            Hooks hooks;
            hooks.mResource = [this](FormId base, ResourceInfo& out, std::string& name, std::string& verb) {
                const auto it = mResources.find(base);
                if (it == mResources.end())
                    return false;
                out = it->second;
                name = base == kBlock ? "Wood Chopping Block" : "Iron Ore Vein";
                verb = base == kBlock ? "" : "Mine";
                return true;
            };
            hooks.mToolCount = [this](FormId list) {
                const auto it = mToolCounts.find(list);
                return it == mToolCounts.end() ? 0 : it->second;
            };
            hooks.mPick = [this](FormId leveled, std::uint64_t seed, FormId& item, std::int32_t& count) {
                mSeeds.push_back(seed);
                if (leveled != kGemList)
                    return false;
                item = kGarnet;
                count = 1;
                return true;
            };
            hooks.mAddToPlayer = [this](FormId item, std::int32_t count) {
                if (mPlayerFull)
                    return false;
                mPlayer[item] += count;
                return true;
            };
            hooks.mItemName = [](FormId item) {
                return item == kIronOre ? std::string("Iron Ore") : item == kFirewood ? std::string("Firewood") : std::string();
            };
            hooks.mMessage = [this](FormId message) {
                return mMessagesReadable && message == kFailMsg ? std::string("Need a pickaxe, friend.") : std::string();
            };
            hooks.mLog = [this](const std::string& line) { mLog.push_back(line); };
            mService = std::make_unique<MiningService>(std::move(hooks));
        }
    };

    TEST_F(MiningServiceFixture, withoutAToolThePlayerIsRefusedWithTheDefaultLine)
    {
        EXPECT_EQ(mService->activate(kPlacementA, kIronVein), MiningStatus::NeedTool);
        EXPECT_EQ(mService->state(), "need tool");
        EXPECT_EQ(mService->message(), std::string(kDefaultNeedPickaxe));
        EXPECT_TRUE(mPlayer.empty());
        EXPECT_FALSE(mService->book().depleted(kPlacementA, 0.0));
        EXPECT_EQ(mService->book().state(kPlacementA).mStrikes, 0); // a refused strike counts nothing
    }

    TEST_F(MiningServiceFixture, theGamesOwnFailureMessageWinsWhenItCanBeRead)
    {
        mMessagesReadable = true;
        (void)mService->activate(kPlacementA, kIronVein);
        EXPECT_EQ(mService->message(), "Need a pickaxe, friend.");
    }

    TEST_F(MiningServiceFixture, aToolListThatCannotBeReadYetRefusesForNowAndCostsNothing)
    {
        mToolCounts[kToolList] = -1;
        EXPECT_EQ(mService->activate(kPlacementA, kIronVein), MiningStatus::Refused);
        EXPECT_EQ(mService->state(), "refused");
        EXPECT_EQ(mService->message(), std::string(kNotReady));
        EXPECT_TRUE(mPlayer.empty());
        EXPECT_EQ(mService->book().state(kPlacementA).mStrikes, 0);
    }

    TEST_F(MiningServiceFixture, theFirstClockTickWarmsTheListsOnceAndOnlyOnce)
    {
        int warmed = 0;
        Hooks hooks;
        hooks.mWarm = [&warmed] { ++warmed; };
        MiningService service(std::move(hooks));
        service.setClock(1.0);
        service.setClock(2.0);
        EXPECT_EQ(warmed, 1);
    }

    TEST_F(MiningServiceFixture, aPickaxeLetsAStrikeGiveOneOre)
    {
        mToolCounts[kToolList] = 1;
        EXPECT_EQ(mService->activate(kPlacementA, kIronVein), MiningStatus::Ok);
        EXPECT_EQ(mPlayer[kIronOre], 1);
        EXPECT_EQ(mService->state(), "mined");
        EXPECT_EQ(mService->message(), "Mined Iron Ore.");
    }

    TEST_F(MiningServiceFixture, theThirdStrikeDepletesTheVeinAndSaysSo)
    {
        mToolCounts[kToolList] = 1;
        mService->setClock(100.0);
        (void)mService->activate(kPlacementA, kIronVein);
        (void)mService->activate(kPlacementA, kIronVein);
        EXPECT_FALSE(mService->book().depleted(kPlacementA, 100.0));
        (void)mService->activate(kPlacementA, kIronVein);
        EXPECT_EQ(mPlayer[kIronOre], 3);
        EXPECT_TRUE(mService->book().depleted(kPlacementA, 100.0));
        EXPECT_EQ(mService->state(), "depleted");
        EXPECT_EQ(mService->message(), std::string(kDefaultDepleted));
        EXPECT_EQ(mService->book().state(kPlacementA).mDue, 100.0 + kRegrowSeconds);
    }

    TEST_F(MiningServiceFixture, aDepletedVeinGivesNothingAndSaysDepleted)
    {
        mToolCounts[kToolList] = 1;
        for (int i = 0; i < 3; ++i)
            (void)mService->activate(kPlacementA, kIronVein);
        EXPECT_EQ(mService->activate(kPlacementA, kIronVein), MiningStatus::Depleted);
        EXPECT_EQ(mPlayer[kIronOre], 3);
        EXPECT_EQ(mService->state(), "depleted");
    }

    TEST_F(MiningServiceFixture, aDepletedVeinIsFullAgainAfterTheRegrowthTime)
    {
        mToolCounts[kToolList] = 1;
        mService->setClock(0.0);
        for (int i = 0; i < 3; ++i)
            (void)mService->activate(kPlacementA, kIronVein);
        EXPECT_EQ(mService->setClock(kRegrowSeconds - 1.0), 0u);
        EXPECT_TRUE(mService->book().depleted(kPlacementA, mService->now()));
        EXPECT_EQ(mService->setClock(kRegrowSeconds), 1u);
        EXPECT_EQ(mService->activate(kPlacementA, kIronVein), MiningStatus::Ok);
        EXPECT_EQ(mPlayer[kIronOre], 4);
    }

    TEST_F(MiningServiceFixture, theDepletedVeinsNameSaysDepletedAndAFullOnesIsThePrompt)
    {
        mToolCounts[kToolList] = 1;
        EXPECT_EQ(mService->nameOf(kPlacementA, kIronVein), "Mine Iron Ore Vein");
        for (int i = 0; i < 3; ++i)
            (void)mService->activate(kPlacementA, kIronVein);
        EXPECT_EQ(mService->nameOf(kPlacementA, kIronVein), "Iron Ore Vein (depleted)");
        EXPECT_EQ(mService->nameOf(kPlacementB, kIronVein), "Mine Iron Ore Vein");
    }

    TEST_F(MiningServiceFixture, theBlockAndNonResourcesHaveNoOverrideName)
    {
        EXPECT_EQ(mService->nameOf(kPlacementA, kBlock), "");
        EXPECT_EQ(mService->nameOf(kPlacementA, kBareActivator), "");
    }

    TEST_F(MiningServiceFixture, aDropGivesPerDropItemsAndOneGemRoll)
    {
        mToolCounts[kToolList] = 1;
        (void)mService->activate(kPlacementA, kGemVein);
        EXPECT_EQ(mPlayer[kIronOre], 2);
        EXPECT_EQ(mPlayer[kGarnet], 1);
        ASSERT_EQ(mSeeds.size(), 1u);
        EXPECT_EQ(mSeeds[0], gemSeed(kPlacementA, 0));
        (void)mService->activate(kPlacementA, kGemVein);
        ASSERT_EQ(mSeeds.size(), 2u);
        EXPECT_EQ(mSeeds[1], gemSeed(kPlacementA, 1));
    }

    TEST_F(MiningServiceFixture, aPlayerWhoCannotCarryTheOreCostsTheVeinNothing)
    {
        mToolCounts[kToolList] = 1;
        mPlayerFull = true;
        EXPECT_EQ(mService->activate(kPlacementA, kIronVein), MiningStatus::Refused);
        EXPECT_EQ(mService->book().state(kPlacementA).mDrops, 0);
        EXPECT_EQ(mService->book().state(kPlacementA).mStrikes, 0);
    }

    TEST_F(MiningServiceFixture, theChoppingBlockNeedsAnAxe)
    {
        EXPECT_EQ(mService->activate(kPlacementA, kBlock), MiningStatus::NeedTool);
        EXPECT_EQ(mService->message(), std::string(kDefaultNeedAxe));
        EXPECT_TRUE(mPlayer.empty());
    }

    TEST_F(MiningServiceFixture, aWoodAxeGivesAWholeSessionOfFirewoodEveryTime)
    {
        mToolCounts[kAxeList] = 1;
        EXPECT_EQ(mService->activate(kPlacementA, kBlock), MiningStatus::Ok);
        EXPECT_EQ(mPlayer[kFirewood], 6);
        EXPECT_EQ(mService->state(), "chopped");
        EXPECT_EQ(mService->message(), "Chopped 6 Firewood.");
        // a block never depletes
        EXPECT_EQ(mService->activate(kPlacementA, kBlock), MiningStatus::Ok);
        EXPECT_EQ(mPlayer[kFirewood], 12);
        EXPECT_TRUE(mService->book().entries().empty());
    }

    TEST_F(MiningServiceFixture, aSessionEndsWhenTheRunningCountReachesTheMaximum)
    {
        mToolCounts[kAxeList] = 1;
        mResources[kBlock].mPerDrop = 2; // the data's block: 2 a chop, 6 at most: 3 chops, 6 pieces
        mResources[kBlock].mMaxPerUse = 6;
        (void)mService->activate(kPlacementA, kBlock);
        EXPECT_EQ(mPlayer[kFirewood], 6);
        mResources[kBlock].mPerDrop = 4; // 4, then 8 >= 6: two chops
        (void)mService->activate(kPlacementA, kBlock);
        EXPECT_EQ(mPlayer[kFirewood], 14);
    }

    TEST_F(MiningServiceFixture, aBlockWithNoToolListNeedsNoTool)
    {
        mResources[kBlock].mToolList = kNullFormId;
        EXPECT_EQ(mService->activate(kPlacementA, kBlock), MiningStatus::Ok);
        EXPECT_EQ(mPlayer[kFirewood], 6);
    }

    TEST_F(MiningServiceFixture, aBaseThatIsNoResourceIsNotHandled)
    {
        EXPECT_EQ(mService->activate(kPlacementA, kBareActivator), MiningStatus::NotResource);
        EXPECT_TRUE(mPlayer.empty());
    }

    TEST_F(MiningServiceFixture, theTraceNamesEachStrikeAndIsStable)
    {
        mToolCounts[kToolList] = 1;
        (void)mService->activate(kPlacementA, kIronVein);
        bool found = false;
        for (const std::string& line : mLog)
            found = found || (line.rfind("MINING ", 0) == 0 && line.find("000a1001") != std::string::npos);
        EXPECT_TRUE(found);
    }

    TEST_F(MiningServiceFixture, theRevisionMovesAfterEveryTryAndEveryRegrowth)
    {
        mToolCounts[kToolList] = 1;
        const auto r0 = mService->revision();
        (void)mService->activate(kPlacementA, kIronVein);
        const auto r1 = mService->revision();
        EXPECT_NE(r0, r1);
        (void)mService->activate(kPlacementA, kBareActivator); // refused: a note, no change
        mService->setClock(0.0);
        for (int i = 0; i < 2; ++i)
            (void)mService->activate(kPlacementA, kIronVein);
        const auto r2 = mService->revision();
        mService->setClock(kRegrowSeconds);
        EXPECT_NE(mService->revision(), r2);
    }

    TEST_F(MiningServiceFixture, theNamespaceIsDeclaredAndItsCallsWork)
    {
        mToolCounts[kToolList] = 1;
        StubBindings bindings;
        ASSERT_TRUE(mService->declare(bindings).ok);
        EXPECT_EQ(bindings.mName, "skyrimse.mining");
        EXPECT_EQ(bindings.mVersion.major, 1);
        EXPECT_EQ(bindings.mVersion.minor, 0);
        for (const char* name : { "state", "message", "revision", "activate", "set_clock" })
            EXPECT_NE(bindings.mFunctions.count(name), 0u) << name;
        EXPECT_EQ(bindings.mFunctions["activate"].access, openeden::sdk::BindingAccess::Mutate);
        EXPECT_EQ(bindings.mFunctions["set_clock"].access, openeden::sdk::BindingAccess::Mutate);
        EXPECT_EQ(bindings.mFunctions["state"].access, openeden::sdk::BindingAccess::Read);
        (void)bindings.call("set_clock", { dbl(10.0) });
        EXPECT_EQ(mService->now(), 10.0);
        (void)bindings.call("activate", { str("000a1001"), str("0010dcc9") });
        EXPECT_EQ(mPlayer[kIronOre], 1);
        EXPECT_EQ(bindings.call("state").value.text, "mined");
        EXPECT_EQ(bindings.call("message").value.text, "Mined Iron Ore.");
        EXPECT_GT(bindings.call("revision").value.integer, 0);
    }

    TEST_F(MiningServiceFixture, aBadIdIsRefusedByTheBinding)
    {
        StubBindings bindings;
        ASSERT_TRUE(mService->declare(bindings).ok);
        const auto result = bindings.call("activate", { str("zz"), str("0010dcc9") });
        EXPECT_EQ(result.status, openeden::sdk::BindingStatus::BadArgument);
        EXPECT_TRUE(mPlayer.empty());
    }
}
