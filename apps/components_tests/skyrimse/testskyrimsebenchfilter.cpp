// Task 0213 (docs/tasks/0213-skyrim-clock-trespass-benches.md, section 3): the crafting global script answered the
// furniture event for ANY furniture, so a chair opened the crafting window. The script cannot read the result of
// `crafting.open` (a queued Mutate call), so the pack answers first: the Read binding `is_bench(base)` says "yes" for
// a bench, "no" for a chair or a bed and "pending" while the recipe table is not built. Written from the spec before
// the code and run red against the stubs (the binding did not exist). Generated plugin, no game data.
#include <game_skyrimse/crafting_service.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "testskyrimsecraftingdata.hpp"

namespace
{
    using namespace CraftingTestData;
    using namespace game_skyrimse;
    using namespace game_skyrimse::crafting;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    class StubBindings final : public openeden::sdk::BindingsService
    {
    public:
        openeden::sdk::DeclareResult declareNamespace(const openeden::sdk::NamespaceDescriptor& d) override
        {
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
        std::map<std::string, openeden::sdk::FunctionDescriptor> mFunctions;
    };

    BindingValue str(std::string_view s)
    {
        BindingValue v;
        v.type = BindingType::String;
        v.text = s;
        return v;
    }

    struct Bench
    {
        std::filesystem::path mDir;
        std::filesystem::path mPath;
        std::unique_ptr<inventory::InventoryService> mInventory;
        std::unique_ptr<CraftingService> mCrafting;
        StubBindings mBindings;

        void build(const std::string& name)
        {
            mDir = std::filesystem::temp_directory_path() / (std::string("oe_benchfilter_") + name);
            std::filesystem::create_directories(mDir);
            mPath = mDir / "test.esp";
            const Bytes all = testPlugin();
            std::ofstream(mPath, std::ios::binary)
                .write(all.str().data(), static_cast<std::streamsize>(all.str().size()));
            inventory::Hooks ih;
            ih.mInputs = [this](inventory::Inputs& out) { return inputs(out); };
            ih.mName = [](FormId) { return std::string(); };
            ih.mHealthOf = [](FormId, float, std::uint32_t) { return 0.f; };
            ih.mApplyHealth = [](float) { return true; };
            mInventory = std::make_unique<inventory::InventoryService>(std::move(ih), 100.f, 1);
            CraftingHooks ch;
            ch.mInputs = [this](inventory::Inputs& out) { return inputs(out); };
            ch.mName = [](FormId) { return std::string(); };
            ch.mEffectInfo = [](FormId) {
                EffectInfo info;
                info.mFound = true;
                return info;
            };
            ch.mSkill = [](std::string_view) { return 15.f; };
            ch.mSkillUsed = [](std::string_view, float) {};
            ch.mLog = [](const std::string&) {};
            mCrafting = std::make_unique<CraftingService>(*mInventory, std::move(ch), CraftingSettings(), false, false);
            EXPECT_TRUE(mInventory->declare(mBindings).ok);
            EXPECT_TRUE(mCrafting->declare(mBindings).ok);
        }
        ~Bench()
        {
            std::error_code ec;
            std::filesystem::remove_all(mDir, ec);
        }
        bool inputs(inventory::Inputs& out)
        {
            out.mPaths = { mPath };
            std::string err;
            out.mOrder.add("Skyrim.esm", false, err);
            out.mOrder.add("test.esp", false, err);
            return true;
        }
        void waitReady()
        {
            for (int i = 0; i < 4000 && !(mInventory->poll() && (mCrafting->poll(), mCrafting->ready())); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ASSERT_TRUE(mCrafting->ready());
        }
        std::string isBench(FormId furniture)
        {
            return std::string(mBindings.call("is_bench", { str(inventory::formatId(furniture)) }).value.text);
        }
    };

    TEST(SkyrimBenchFilter, theFunctionIsDeclared)
    {
        Bench b;
        b.build("declared");
        EXPECT_NE(b.mBindings.mFunctions.find("is_bench"), b.mBindings.mFunctions.end());
        EXPECT_EQ(b.mBindings.mFunctions["is_bench"].access, openeden::sdk::BindingAccess::Read);
    }

    TEST(SkyrimBenchFilter, everyCraftingBenchAnswersYes)
    {
        Bench b;
        b.build("yes");
        b.waitReady();
        for (const FormId furniture : { kFurnForge, kFurnSky, kFurnWheel, kFurnArmor, kFurnAlchemy, kFurnEnchant })
            EXPECT_EQ(b.isBench(furniture), "yes") << std::hex << furniture;
    }

    TEST(SkyrimBenchFilter, aChairWithNoCraftingKeywordAnswersNo)
    {
        Bench b;
        b.build("chair");
        b.waitReady();
        EXPECT_EQ(b.isBench(kFurnChair), "no");
    }

    TEST(SkyrimBenchFilter, aFormThePluginDoesNotKnowAnswersNo)
    {
        Bench b;
        b.build("unknown");
        b.waitReady();
        EXPECT_EQ(b.isBench(0x01009999), "no");
        EXPECT_EQ(b.isBench(0), "no");
    }

    TEST(SkyrimBenchFilter, whileTheTableIsBeingBuiltTheAnswerIsPending)
    {
        Bench b;
        b.build("pending");
        // no poll yet: the record reader has not run
        EXPECT_EQ(b.isBench(kFurnForge), "pending");
        EXPECT_EQ(b.isBench(kFurnChair), "pending");
    }

    TEST(SkyrimBenchFilter, aBadIdTextAnswersNo)
    {
        Bench b;
        b.build("badid");
        b.waitReady();
        EXPECT_EQ(std::string(b.mBindings.call("is_bench", { str("nope") }).value.text), "no");
    }

    TEST(SkyrimBenchFilter, askingDoesNotOpenASessionOrChangeTheState)
    {
        Bench b;
        b.build("readonly");
        b.waitReady();
        EXPECT_EQ(b.mCrafting->session(), CraftingService::State::None);
        (void)b.isBench(kFurnForge);
        (void)b.isBench(kFurnChair);
        EXPECT_EQ(b.mCrafting->session(), CraftingService::State::None);
    }
}
