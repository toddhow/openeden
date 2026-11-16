// Task 0221: the harvest service over fake hooks and a stub bindings service: a harvest (the order of the steps, the
// ingredient, the seasons, the roll), the clock that grows plants back, and the namespace skyrimse.harvest.
#include <game_skyrimse/harvest_service.hpp>

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::harvest;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    constexpr FormId kCanisRootPlant = 0x0010dcc0; // a base
    constexpr FormId kLavenderPlant = 0x0010dcc1;
    constexpr FormId kTreeNoIngredient = 0x0010dcc2;
    constexpr FormId kCanisRoot = 0x00063b3e; // an item
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

    BindingValue num(std::int32_t i)
    {
        BindingValue v;
        v.type = BindingType::Int32;
        v.integer = i;
        return v;
    }

    struct HarvestServiceFixture : ::testing::Test
    {
        std::vector<std::string> mSteps; // the order of the hook calls
        std::map<FormId, PlantInfo> mPlants;
        std::map<FormId, std::int32_t> mPlayer;
        bool mPlayerFull = false;
        bool mHideOk = true;
        std::vector<std::string> mLog;
        std::vector<std::uint64_t> mSeeds;
        std::unique_ptr<HarvestService> mService;

        void SetUp() override
        {
            PlantInfo canis;
            canis.mIngredient = kCanisRoot;
            canis.mChances = { 100, 100, 100, 100 };
            canis.mActivateText = "Harvest";
            canis.mName = "Canis Root";
            mPlants[kCanisRootPlant] = canis;
            PlantInfo lavender;
            lavender.mIngredient = 0x00045c28;
            lavender.mChances = { 100, 100, 0, 0 };
            lavender.mName = "Lavender";
            mPlants[kLavenderPlant] = lavender;
            PlantInfo bare;
            bare.mName = "Stump";
            mPlants[kTreeNoIngredient] = bare;

            Hooks hooks;
            hooks.mPlant = [this](FormId base, PlantInfo& out) {
                const auto it = mPlants.find(base);
                if (it == mPlants.end())
                    return false;
                out = it->second;
                return true;
            };
            hooks.mPick = [this](FormId ingredient, std::uint64_t seed, FormId& item, std::int32_t& count) {
                mSteps.push_back("pick");
                mSeeds.push_back(seed);
                item = ingredient == kCanisRoot ? kCanisRoot : 0x00045c28;
                count = 1;
                return true;
            };
            hooks.mAddToPlayer = [this](FormId item, std::int32_t count) {
                mSteps.push_back("add");
                if (mPlayerFull)
                    return false;
                mPlayer[item] += count;
                return true;
            };
            hooks.mItemName = [](FormId item) { return item == kCanisRoot ? std::string("Canis Root") : std::string(); };
            hooks.mHide = [this](FormId) {
                mSteps.push_back("hide");
                return mHideOk;
            };
            hooks.mShow = [this](FormId placement) {
                mSteps.push_back("show:" + std::to_string(placement));
                return true;
            };
            hooks.mLog = [this](const std::string& line) { mLog.push_back(line); };
            mService = std::make_unique<HarvestService>(std::move(hooks));
        }
    };

    TEST_F(HarvestServiceFixture, aHarvestGivesTheIngredientThenHidesThePlant)
    {
        mService->setClock(1000.0, 6);
        EXPECT_EQ(mService->harvest(kPlacementA, kCanisRootPlant), HarvestStatus::Ok);
        EXPECT_EQ(mPlayer[kCanisRoot], 1);
        ASSERT_EQ(mSteps.size(), 3u);
        EXPECT_EQ(mSteps[0], "pick");
        EXPECT_EQ(mSteps[1], "add");
        EXPECT_EQ(mSteps[2], "hide");
        EXPECT_EQ(mService->state(), "harvested");
        EXPECT_EQ(mService->message(), "Harvested Canis Root.");
        EXPECT_TRUE(mService->book().has(kPlacementA));
    }

    TEST_F(HarvestServiceFixture, thePlantGrowsBackAfterTheRegrowTime)
    {
        mService->setClock(1000.0, 6);
        ASSERT_EQ(mService->harvest(kPlacementA, kCanisRootPlant), HarvestStatus::Ok);
        EXPECT_DOUBLE_EQ(mService->book().entries().at(kPlacementA), 1000.0 + kRegrowSeconds);
        EXPECT_EQ(mService->setClock(1000.0 + kRegrowSeconds - 1.0, 6), 0u);
        EXPECT_TRUE(mService->book().has(kPlacementA));
        mSteps.clear();
        EXPECT_EQ(mService->setClock(1000.0 + kRegrowSeconds, 6), 1u);
        ASSERT_EQ(mSteps.size(), 1u);
        EXPECT_EQ(mSteps[0], "show:" + std::to_string(kPlacementA));
        EXPECT_FALSE(mService->book().has(kPlacementA));
        EXPECT_EQ(mService->harvest(kPlacementA, kCanisRootPlant), HarvestStatus::Ok);
        EXPECT_EQ(mPlayer[kCanisRoot], 2);
    }

    TEST_F(HarvestServiceFixture, aHarvestedPlantGivesNothingTwice)
    {
        mService->setClock(1000.0, 6);
        ASSERT_EQ(mService->harvest(kPlacementA, kCanisRootPlant), HarvestStatus::Ok);
        EXPECT_EQ(mService->harvest(kPlacementA, kCanisRootPlant), HarvestStatus::AlreadyHarvested);
        EXPECT_EQ(mPlayer[kCanisRoot], 1);
        EXPECT_EQ(mService->state(), "already");
    }

    TEST_F(HarvestServiceFixture, aBaseWithNoIngredientIsNotHarvestable)
    {
        EXPECT_EQ(mService->harvest(kPlacementA, kTreeNoIngredient), HarvestStatus::NotHarvestable);
        EXPECT_EQ(mService->harvest(kPlacementA, 0x00999999), HarvestStatus::NotHarvestable);
        EXPECT_TRUE(mPlayer.empty());
        EXPECT_TRUE(mSteps.empty());
        EXPECT_FALSE(mService->book().has(kPlacementA));
    }

    TEST_F(HarvestServiceFixture, aSeasonWithZeroChanceGrowsNothingAndLeavesThePlant)
    {
        mService->setClock(1000.0, 10); // fall: lavender has 0
        EXPECT_EQ(mService->harvest(kPlacementA, kLavenderPlant), HarvestStatus::OutOfSeason);
        EXPECT_TRUE(mPlayer.empty());
        EXPECT_FALSE(mService->book().has(kPlacementA)) << "an out of season try leaves the plant as it was";
        EXPECT_EQ(mService->message(), "There is nothing to harvest.");
        mService->setClock(1000.0, 5); // summer
        EXPECT_EQ(mService->harvest(kPlacementA, kLavenderPlant), HarvestStatus::Ok);
    }

    TEST_F(HarvestServiceFixture, aFailedRollEmptiesThePlantAllTheSame)
    {
        mPlants[kCanisRootPlant].mChances = { 1, 1, 1, 1 };
        mService->setClock(1000.0, 6);
        int nothing = 0;
        for (FormId placement = 0x2000; placement < 0x2040; ++placement)
        {
            const HarvestStatus status = mService->harvest(placement, kCanisRootPlant);
            if (status == HarvestStatus::Nothing)
            {
                ++nothing;
                EXPECT_TRUE(mService->book().has(placement));
            }
        }
        EXPECT_GE(nothing, 55) << "1 percent: nearly every try fails";
    }

    TEST_F(HarvestServiceFixture, theSameSessionGivesTheSameRollsAndSeeds)
    {
        mPlants[kCanisRootPlant].mChances = { 50, 50, 50, 50 };
        auto run = [this]() {
            mSeeds.clear();
            mPlayer.clear();
            mService->book().clear();
            mService->setClock(1000.0, 6);
            std::vector<int> outcomes;
            for (FormId placement = 0x3000; placement < 0x3020; ++placement)
                outcomes.push_back(static_cast<int>(mService->harvest(placement, kCanisRootPlant)));
            return outcomes;
        };
        const std::vector<int> first = run();
        const std::vector<std::uint64_t> seedsFirst = mSeeds;
        const std::vector<int> second = run();
        EXPECT_EQ(first, second);
        EXPECT_EQ(seedsFirst, mSeeds);
    }

    TEST_F(HarvestServiceFixture, aPlayerWhoCannotCarryItKeepsThePlantAsItWas)
    {
        mPlayerFull = true;
        mService->setClock(1000.0, 6);
        EXPECT_EQ(mService->harvest(kPlacementA, kCanisRootPlant), HarvestStatus::Refused);
        EXPECT_FALSE(mService->book().has(kPlacementA));
        EXPECT_TRUE(mPlayer.empty());
        for (const std::string& step : mSteps)
            EXPECT_NE(step, "hide");
    }

    TEST_F(HarvestServiceFixture, aWorldThatCannotHideStillRemembersTheHarvest)
    {
        mHideOk = false;
        mService->setClock(1000.0, 6);
        EXPECT_EQ(mService->harvest(kPlacementA, kCanisRootPlant), HarvestStatus::Ok);
        EXPECT_TRUE(mService->book().has(kPlacementA));
        bool said = false;
        for (const std::string& line : mLog)
            said = said || line.find("stays in view") != std::string::npos;
        EXPECT_TRUE(said);
    }

    TEST_F(HarvestServiceFixture, aPlantThatIsNotHiddenIsNeverHidden)
    {
        mPlants[kCanisRootPlant].mHide = false;
        mService->setClock(1000.0, 6);
        EXPECT_EQ(mService->harvest(kPlacementA, kCanisRootPlant), HarvestStatus::Ok);
        for (const std::string& step : mSteps)
            EXPECT_NE(step, "hide");
        EXPECT_TRUE(mService->book().has(kPlacementA));
    }

    TEST_F(HarvestServiceFixture, withNoClockTheRegrowthWaits)
    {
        EXPECT_EQ(mService->harvest(kPlacementA, kCanisRootPlant), HarvestStatus::Ok);
        EXPECT_DOUBLE_EQ(mService->book().entries().at(kPlacementA), kRegrowSeconds);
        EXPECT_EQ(mService->setClock(-1.0, -1), 0u);
        EXPECT_TRUE(mService->book().has(kPlacementA));
    }

    TEST_F(HarvestServiceFixture, thePromptIsTheVerbAndTheName)
    {
        EXPECT_EQ(mService->promptOf(kCanisRootPlant), "Harvest Canis Root");
        EXPECT_EQ(mService->promptOf(kLavenderPlant), "Harvest Lavender");
        EXPECT_EQ(mService->promptOf(kTreeNoIngredient), "");
        EXPECT_EQ(mService->promptOf(0x00999999), "");
    }

    TEST_F(HarvestServiceFixture, theRevisionMovesWithEveryChange)
    {
        const std::uint32_t before = mService->revision();
        mService->setClock(1000.0, 6);
        mService->harvest(kPlacementA, kCanisRootPlant);
        const std::uint32_t after = mService->revision();
        EXPECT_NE(before, after);
        mService->setClock(1000.0 + kRegrowSeconds + 1.0, 6);
        EXPECT_NE(after, mService->revision());
    }

    TEST_F(HarvestServiceFixture, theBindingsAreDeclaredAsVersionOneZero)
    {
        StubBindings bindings;
        const openeden::sdk::DeclareResult declared = mService->declare(bindings);
        ASSERT_TRUE(declared.ok);
        EXPECT_EQ(bindings.mName, "skyrimse.harvest");
        EXPECT_EQ(bindings.mVersion.major, 1);
        EXPECT_EQ(bindings.mVersion.minor, 0);
        for (const char* name : { "state", "message", "revision", "prompt", "harvest", "set_clock" })
            EXPECT_EQ(bindings.mFunctions.count(name), 1u) << name;
        EXPECT_EQ(bindings.mFunctions["harvest"].access, openeden::sdk::BindingAccess::Mutate);
        EXPECT_EQ(bindings.mFunctions["set_clock"].access, openeden::sdk::BindingAccess::Mutate);
        EXPECT_EQ(bindings.mFunctions["prompt"].access, openeden::sdk::BindingAccess::Read);
    }

    TEST_F(HarvestServiceFixture, theBindingsHarvestAndReportThroughText)
    {
        StubBindings bindings;
        ASSERT_TRUE(mService->declare(bindings).ok);
        bindings.call("set_clock", { dbl(2000.0), num(6) });
        const auto prompt = bindings.call("prompt", { str("0010dcc0") });
        EXPECT_EQ(prompt.value.text, "Harvest Canis Root");
        bindings.call("harvest", { str("000a1001"), str("0010dcc0") });
        EXPECT_EQ(bindings.call("state").value.text, "harvested");
        EXPECT_EQ(bindings.call("message").value.text, "Harvested Canis Root.");
        EXPECT_GT(bindings.call("revision").value.integer, 0);
        EXPECT_EQ(mPlayer[kCanisRoot], 1);
    }

    TEST_F(HarvestServiceFixture, aBadIdIsABadArgumentAndChangesNothing)
    {
        StubBindings bindings;
        ASSERT_TRUE(mService->declare(bindings).ok);
        const auto result = bindings.call("harvest", { str("not hex"), str("0010dcc0") });
        EXPECT_EQ(result.status, openeden::sdk::BindingStatus::BadArgument);
        const auto tooBig = bindings.call("harvest", { str("1ffffffff"), str("0010dcc0") });
        EXPECT_EQ(tooBig.status, openeden::sdk::BindingStatus::BadArgument);
        EXPECT_TRUE(mPlayer.empty());
    }
}
