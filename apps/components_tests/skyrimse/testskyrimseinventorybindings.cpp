// Task 0182: the inventory service and its "skyrimse.inventory" bindings over a stub bindings service. Generated plugin
// written to a temporary file; no game data. Written before the code and run red against a stub first.
#include <game_skyrimse/inventory_service.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using namespace game_skyrimse::inventory;
    using openeden::sdk::BindingAccess;
    using openeden::sdk::BindingStatus;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kMisc = kSelf | 0x100;
    constexpr FormId kWeap = kSelf | 0x101;
    constexpr FormId kCont = kSelf | 0x200;
    constexpr FormId kContLeveled = kSelf | 0x201;

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
        openeden::sdk::BindingResult call(const std::string& name, std::vector<BindingValue> args)
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
    BindingValue num(std::int32_t n)
    {
        BindingValue v;
        v.type = BindingType::Int32;
        v.integer = n;
        return v;
    }

    struct Fixture : testing::Test
    {
        std::filesystem::path mDir;
        std::filesystem::path mPath;
        std::vector<std::string> mLog;
        std::unique_ptr<InventoryService> mService;
        StubBindings mBindings;

        void SetUp() override
        {
            Bytes all = tes4Header(1.7f, 0, "A", "B", 4, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes misc = sub4("EDID", Bytes().zstring("Pebble"));
            misc.append(sub4("DATA", Bytes().u32(25).f32(0.5f)));
            all.append(group4(Content::fourCC("MISC"), 0, record4("MISC", 0, kMisc, misc)));
            all.append(group4(Content::fourCC("WEAP"), 0,
                record4("WEAP", 0, kWeap, sub4("DATA", Bytes().u32(25).f32(9.f).u16(7)))));
            Bytes gold = sub4("DATA", Bytes().u32(1).f32(0.f));
            all.append(group4(Content::fourCC("MISC"), 0, record4("MISC", 0, 0x0000000F, gold)));
            Bytes cont;
            cont.append(sub4("CNTO", Bytes().u32(kMisc).u32(5)));
            cont.append(sub4("CNTO", Bytes().u32(kWeap).u32(1)));
            all.append(group4(Content::fourCC("CONT"), 0, record4("CONT", 0, kCont, cont)));
            // the file is named test.esp: a plugin finds its own load order slot by its file name
            mDir = std::filesystem::temp_directory_path() / (std::string("oe_inv_") + testing::UnitTest::GetInstance()->current_test_info()->name());
            std::filesystem::create_directories(mDir);
            mPath = mDir / "test.esp";
            std::ofstream(mPath, std::ios::binary).write(all.str().data(), static_cast<std::streamsize>(all.str().size()));

            Hooks hooks;
            hooks.mInputs = [this](Inputs& out) {
                out.mPaths = { mPath };
                std::string err;
                EXPECT_TRUE(out.mOrder.add("Skyrim.esm", false, err));
                EXPECT_TRUE(out.mOrder.add("test.esp", false, err));
                return true;
            };
            hooks.mName = [](FormId id) -> std::string {
                if (id == kMisc)
                    return "Pebble";
                if (id == kWeap)
                    return "Sword";
                if (id == kGoldItem)
                    return "Gold";
                return {};
            };
            hooks.mLog = [this](const std::string& line) { mLog.push_back(line); };
            mService = std::make_unique<InventoryService>(std::move(hooks), 100.f, 1);
            ASSERT_TRUE(mService->declare(mBindings).ok);
        }

        void TearDown() override
        {
            mService.reset();
            std::error_code ec;
            std::filesystem::remove_all(mDir, ec);
        }

        void waitReady()
        {
            for (int i = 0; i < 4000 && !mService->poll(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ASSERT_TRUE(mService->ready());
        }

        std::vector<std::string> items(const std::string& owner)
        {
            const auto r = mBindings.call("items", { str(owner) });
            std::vector<std::string> out;
            for (std::size_t i = 0; i < r.value.count; ++i)
                out.emplace_back(r.value.elements[i].text);
            return out;
        }
    };

    TEST_F(Fixture, theNamespaceIsDeclaredWithReadAndMutateFunctions)
    {
        EXPECT_EQ(mBindings.mName, "skyrimse.inventory");
        EXPECT_EQ(mBindings.mVersion.major, 1);
        for (const char* read : { "ready", "revision", "known", "items", "count", "name", "category", "weight", "value",
                 "gold_item", "carried", "capacity" })
        {
            ASSERT_EQ(mBindings.mFunctions.count(read), 1u) << read;
            EXPECT_EQ(mBindings.mFunctions[read].access, BindingAccess::Read) << read;
        }
        for (const char* mutate : { "open", "transfer", "take_all" })
        {
            ASSERT_EQ(mBindings.mFunctions.count(mutate), 1u) << mutate;
            EXPECT_EQ(mBindings.mFunctions[mutate].access, BindingAccess::Mutate) << mutate;
            EXPECT_FALSE(mBindings.mFunctions[mutate].hasResult) << mutate;
        }
    }

    TEST_F(Fixture, idsAreHexTextAndTheOwnerIsPlayerOrHex)
    {
        EXPECT_EQ(formatId(0x1A2B), "00001a2b");
        EXPECT_EQ(formatId(0xFE000801), "fe000801");
        std::uint64_t v = 0;
        EXPECT_TRUE(parseId("fe000801", v));
        EXPECT_EQ(v, 0xFE000801ull);
        EXPECT_FALSE(parseId("", v));
        EXPECT_FALSE(parseId("xyz", v));
        EXPECT_FALSE(parseId("00000000000000000", v)); // 17 digits
        EXPECT_TRUE(parseOwner("player", v));
        EXPECT_EQ(v, kPlayerOwner);
        EXPECT_FALSE(parseOwner("0", v)); // 0 is the player: say "player"
    }

    TEST_F(Fixture, thePlayerStartsWithGoldBeforeTheTableIsReady)
    {
        EXPECT_FALSE(mBindings.call("ready", {}).value.boolean);
        const auto count = mBindings.call("count", { str("player"), str(formatId(kGoldItem)) });
        EXPECT_EQ(count.value.integer, 100);
        EXPECT_EQ(items("player"), std::vector<std::string>{ formatId(kGoldItem) });
        EXPECT_EQ(mBindings.call("gold_item", {}).value.text, formatId(kGoldItem));
    }

    TEST_F(Fixture, openRegistersAContainerOnceTheTableIsBuilt)
    {
        EXPECT_FALSE(mService->open(0x500, kCont, "container")); // not ready: nothing is waited for
        EXPECT_FALSE(mBindings.call("known", { str("500") }).value.boolean);
        waitReady();
        EXPECT_TRUE(mService->open(0x500, kCont, "container"));
        EXPECT_TRUE(mBindings.call("known", { str("500") }).value.boolean);
        // sorted by category (weapon before misc), then name
        EXPECT_EQ(items("500"), (std::vector<std::string>{ formatId(kWeap), formatId(kMisc) }));
        EXPECT_EQ(mBindings.call("count", { str("500"), str(formatId(kMisc)) }).value.integer, 5);
        EXPECT_FALSE(mService->open(0x501, kSelf | 0x999, "container")); // not a container
        EXPECT_FALSE(mService->open(0x502, kCont, "chest")); // not a kind
        EXPECT_FALSE(mService->open(kPlayerOwner, kCont, "container"));
    }

    TEST_F(Fixture, itemFactsComeFromTheRecords)
    {
        waitReady();
        EXPECT_EQ(mBindings.call("name", { str(formatId(kMisc)) }).value.text, "Pebble");
        EXPECT_EQ(mBindings.call("category", { str(formatId(kWeap)) }).value.text, "weapon");
        EXPECT_DOUBLE_EQ(mBindings.call("weight", { str(formatId(kWeap)) }).value.number, 9.0);
        EXPECT_EQ(mBindings.call("value", { str(formatId(kMisc)) }).value.integer, 25);
        // a form with no name shows its id
        EXPECT_EQ(mBindings.call("name", { str(formatId(kSelf | 0x777)) }).value.text, "<" + formatId(kSelf | 0x777) + ">");
    }

    TEST_F(Fixture, aTransferThroughTheMutateFunctionMovesItemsAndMovesTheRevision)
    {
        waitReady();
        ASSERT_TRUE(mService->open(0x500, kCont, "container"));
        const int before = mBindings.call("revision", {}).value.integer;
        (void)mBindings.call("transfer", { str("500"), str("player"), str(formatId(kMisc)), num(2) });
        EXPECT_EQ(mBindings.call("count", { str("player"), str(formatId(kMisc)) }).value.integer, 2);
        EXPECT_EQ(mBindings.call("count", { str("500"), str(formatId(kMisc)) }).value.integer, 3);
        const int after = mBindings.call("revision", {}).value.integer;
        EXPECT_NE(before, after);
        // put one back
        (void)mBindings.call("transfer", { str("player"), str("500"), str(formatId(kMisc)), num(1) });
        EXPECT_EQ(mBindings.call("count", { str("500"), str(formatId(kMisc)) }).value.integer, 4);
        // carried weight: 1 pebble of 0.5
        EXPECT_DOUBLE_EQ(mBindings.call("carried", {}).value.number, 0.5);
        EXPECT_DOUBLE_EQ(mBindings.call("capacity", {}).value.number, kBaseCarryWeight);
        // take all
        (void)mBindings.call("take_all", { str("500"), str("player") });
        EXPECT_TRUE(items("500").empty());
        EXPECT_EQ(mBindings.call("count", { str("player"), str(formatId(kMisc)) }).value.integer, 5);
    }

    TEST_F(Fixture, aRefusedTransferChangesNothingAndIsLogged)
    {
        waitReady();
        ASSERT_TRUE(mService->open(0x500, kCont, "container"));
        const int before = mBindings.call("revision", {}).value.integer;
        mLog.clear();
        (void)mBindings.call("transfer", { str("500"), str("player"), str(formatId(kMisc)), num(99) });
        (void)mBindings.call("transfer", { str("nobody"), str("player"), str(formatId(kMisc)), num(1) });
        EXPECT_EQ(mBindings.call("revision", {}).value.integer, before);
        EXPECT_EQ(mBindings.call("count", { str("500"), str(formatId(kMisc)) }).value.integer, 5);
        std::string joined;
        for (const std::string& line : mLog)
            joined += line + "\n";
        EXPECT_NE(joined.find("not enough"), std::string::npos) << joined;
        EXPECT_NE(joined.find("refused"), std::string::npos) << joined;
    }

    TEST_F(Fixture, badArgumentsAreBadArgumentsNotCrashes)
    {
        EXPECT_EQ(mBindings.call("count", { str("zzz"), str("00000001") }).status, BindingStatus::BadArgument);
        EXPECT_EQ(mBindings.call("name", { str("") }).status, BindingStatus::BadArgument);
        EXPECT_EQ(mBindings.call("items", { str("0") }).status, BindingStatus::BadArgument);
    }

    TEST_F(Fixture, theSaveChunkReopensAnUntouchedContainerTheSameAndKeepsATouchedOne)
    {
        waitReady();
        ASSERT_TRUE(mService->open(0x500, kCont, "container"));
        (void)mBindings.call("transfer", { str("500"), str("player"), str(formatId(kMisc)), num(2) });
        class W final : public openeden::sdk::ChunkWriter
        {
        public:
            void write(const void* d, std::size_t n) override
            {
                const auto* b = static_cast<const unsigned char*>(d);
                bytes.insert(bytes.end(), b, b + n);
            }
            std::vector<unsigned char> bytes;
        } writer;
        mService->chunk().write(writer);
        EXPECT_EQ(mService->chunk().chunkName(), "inventory");
        EXPECT_EQ(mService->chunk().chunkVersion(), 1u);
        EXPECT_GT(writer.bytes.size(), 8u);
    }
}
