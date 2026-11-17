// Task 0234: books lying in the world (game_skyrimse/worldbooks.hpp): the read of a placed book over the lock picking
// and
// book service (task 0203) and its "skyrimse.worldbooks" bindings, over a stub bindings service and the real inventory
// service. A generated plugin written to a temporary file; no game data. Written before the code and run red against
// stubs first (see the task's stage log).
#include <game_skyrimse/worldbooks.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
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
    using namespace game_skyrimse::lockbooks;
    using game_skyrimse::inventory::formatId;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kNote = kSelf | 0x100; // a plain note
    constexpr FormId kSkillBook = kSelf | 0x101; // teaches Alchemy (actor value 16, slot 10)
    constexpr FormId kTome = kSelf | 0x102; // teaches Thunderbolt
    constexpr FormId kBlackBook = kSelf | 0x103; // flag 2: cannot be taken
    constexpr FormId kThunderbolt = 0x0010F7EE;
    constexpr std::uint64_t kPlacementA = 0xA001;
    constexpr std::uint64_t kPlacementB = 0xA002;

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

    Bytes bookRecord(FormId id, const std::string& edid, std::uint8_t flags, std::uint32_t teaches,
        const std::string& name, const std::string& desc)
    {
        Bytes subs = sub4("EDID", Bytes().zstring(edid));
        subs.append(sub4("FULL", Bytes().zstring(name)));
        subs.append(sub4("DESC", Bytes().zstring(desc)));
        subs.append(sub4("DATA", Bytes().u8(flags).u8(0).u16(0).u32(teaches).u32(100).f32(1.f)));
        return record4("BOOK", 0, id, subs);
    }

    struct WorldBooksFx : testing::Test
    {
        std::filesystem::path mDir;
        std::filesystem::path mPath;
        std::vector<std::string> mLog;
        std::vector<std::size_t> mRaised;
        std::vector<FormId> mLearned;
        std::vector<std::uint64_t> mReads; ///< the placements the Papyrus hook was told about
        std::unique_ptr<inventory::InventoryService> mInventory;
        std::unique_ptr<LockBooksService> mBooks;
        std::unique_ptr<WorldBooksService> mWorld;
        StubBindings mBindings;

        void SetUp() override
        {
            Bytes all = tes4Header(1.7f, 0, "A", "B", 4, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes books;
            books.append(bookRecord(kNote, "Note", 0, 0, "A Note", "<p>Meet me at dawn.</p>"));
            books.append(
                bookRecord(kSkillBook, "SkillBookAlchemy", BookFlags::TeachesSkill, 16, "Alchemy Book", "alch"));
            books.append(bookRecord(kTome, "Tome", BookFlags::TeachesSpell, kThunderbolt, "Tome of Thunderbolt", "t"));
            books.append(bookRecord(kBlackBook, "BlackBook", 0x02, 0, "Black Book", "dark"));
            all.append(group4(Content::fourCC("BOOK"), 0, books));
            all.append(group4(
                Content::fourCC("MISC"), 0, record4("MISC", 0, 0x0000000F, sub4("DATA", Bytes().u32(1).f32(0.f)))));
            mDir = std::filesystem::temp_directory_path()
                / (std::string("oe_wb_") + testing::UnitTest::GetInstance()->current_test_info()->name());
            std::filesystem::create_directories(mDir);
            mPath = mDir / "test.esp";
            std::ofstream(mPath, std::ios::binary)
                .write(all.str().data(), static_cast<std::streamsize>(all.str().size()));

            inventory::Hooks ih;
            ih.mInputs = [this](inventory::Inputs& out) { return inputs(out.mPaths, out.mOrder); };
            ih.mName = [](FormId) { return std::string(); };
            mInventory = std::make_unique<inventory::InventoryService>(std::move(ih), 0.f, 1);

            ServiceHooks hooks;
            hooks.mInputs = [this](BookInputs& out) { return inputs(out.mPaths, out.mOrder); };
            hooks.mResolve = [](const LString& text, std::uint32_t, StringTableKind) { return text.mText; };
            hooks.mName = [](FormId) { return std::string(); };
            hooks.mSkill = []() { return 15.f; };
            hooks.mSkillUsed = [](std::size_t, float) {};
            hooks.mRaiseSkill = [this](std::size_t slot) {
                mRaised.push_back(slot);
                return true;
            };
            hooks.mSkillName
                = [](std::size_t slot) { return slot == 10 ? std::string("Alchemy") : std::string("Skill"); };
            hooks.mSpellName
                = [](FormId spell) { return spell == kThunderbolt ? std::string("Thunderbolt") : std::string(); };
            hooks.mSpellLearned = [this](FormId spell) { mLearned.push_back(spell); };
            hooks.mLog = [this](const std::string& line) { mLog.push_back(line); };
            mBooks = std::make_unique<LockBooksService>(*mInventory, std::move(hooks));

            WorldBooksHooks wh;
            wh.mRead = [this](std::uint64_t placement) { mReads.push_back(placement); };
            wh.mLog = [this](const std::string& line) { mLog.push_back(line); };
            mWorld = std::make_unique<WorldBooksService>(*mBooks, std::move(wh));
            ASSERT_TRUE(mWorld->declare(mBindings).ok);
        }

        bool inputs(std::vector<std::filesystem::path>& paths, LoadOrder& order)
        {
            paths = { mPath };
            std::string err;
            EXPECT_TRUE(order.add("Skyrim.esm", false, err));
            EXPECT_TRUE(order.add("test.esp", false, err));
            return true;
        }

        void TearDown() override
        {
            mWorld.reset();
            mBooks.reset();
            mInventory.reset();
            std::error_code ec;
            std::filesystem::remove_all(mDir, ec);
        }

        void waitReady()
        {
            for (int i = 0; i < 4000 && !(mInventory->poll() && mBooks->poll()); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ASSERT_TRUE(mBooks->ready());
        }
        int held(FormId item) { return mInventory->model().count(inventory::kPlayerOwner, item); }
        bool boolOf(const std::string& fn, FormId item)
        {
            return mBindings.call(fn, { str(formatId(item)) }).value.boolean;
        }
        void read(std::uint64_t placement, FormId item)
        {
            (void)mBindings.call("read", { str(formatId(placement)), str(formatId(item)) });
        }
    };

    TEST_F(WorldBooksFx, theNamespaceIsSkyrimseWorldbooksOnePointZero)
    {
        EXPECT_EQ(mBindings.mName, "skyrimse.worldbooks");
        EXPECT_EQ(mBindings.mVersion.major, 1);
        EXPECT_EQ(mBindings.mVersion.minor, 0);
        for (const char* fn : { "is_book", "can_take", "read_count", "read" })
        {
            EXPECT_EQ(mBindings.mFunctions.count(fn), 1u) << fn;
        }
        EXPECT_EQ(mBindings.mFunctions["read"].access, openeden::sdk::BindingAccess::Mutate);
        EXPECT_EQ(mBindings.mFunctions["is_book"].access, openeden::sdk::BindingAccess::Read);
    }

    TEST_F(WorldBooksFx, nothingIsABookAndNothingReadsUntilTheIndexIsReady)
    {
        EXPECT_FALSE(boolOf("is_book", kNote));
        EXPECT_FALSE(mWorld->read(kPlacementA, kNote));
        EXPECT_TRUE(mReads.empty()) << "the scripts are told nothing about a read that did not happen";
        waitReady();
        EXPECT_TRUE(boolOf("is_book", kNote));
    }

    TEST_F(WorldBooksFx, isBookIsFalseForAnythingThatIsNotABookRecord)
    {
        waitReady();
        EXPECT_FALSE(boolOf("is_book", kLockpickItem));
        EXPECT_FALSE(boolOf("is_book", kSelf | 0x999));
        EXPECT_FALSE(mWorld->read(kPlacementA, kLockpickItem));
        EXPECT_TRUE(mReads.empty());
        EXPECT_EQ(mWorld->readCount(), 0u);
    }

    TEST_F(WorldBooksFx, aBookWithTheCannotBeTakenFlagCannotBeTaken)
    {
        waitReady();
        EXPECT_TRUE(boolOf("can_take", kNote));
        EXPECT_TRUE(boolOf("can_take", kSkillBook));
        EXPECT_FALSE(boolOf("can_take", kBlackBook));
        EXPECT_FALSE(boolOf("can_take", kSelf | 0x999)) << "not a book: nothing to take through this path";
    }

    TEST_F(WorldBooksFx, readingAPlacedNoteNeedsNoInventoryShowsItAndTellsTheScripts)
    {
        waitReady();
        ASSERT_EQ(held(kNote), 0);
        read(kPlacementA, kNote);
        EXPECT_EQ(mReads, std::vector<std::uint64_t>{ kPlacementA });
        EXPECT_EQ(mWorld->readCount(), 1u);
        EXPECT_EQ(held(kNote), 0) << "reading in place does not take the book";
        EXPECT_TRUE(mRaised.empty());
        EXPECT_TRUE(mLearned.empty());
        // a second read of a note tells the scripts again (a script keeps its own state)
        read(kPlacementA, kNote);
        EXPECT_EQ(mReads.size(), 2u);
        EXPECT_EQ(mWorld->readCount(), 2u);
    }

    TEST_F(WorldBooksFx, aSkillBookReadInTheWorldRaisesItsSkillOnceWhereverItIsReadAgain)
    {
        waitReady();
        read(kPlacementA, kSkillBook);
        ASSERT_EQ(mRaised.size(), 1u);
        EXPECT_EQ(mRaised[0], 10u);
        EXPECT_EQ(mBooks->bookResult(kSkillBook), "Alchemy skill increased by one level.");
        read(kPlacementB, kSkillBook); // another copy of the same book in the world
        EXPECT_EQ(mRaised.size(), 1u);
        EXPECT_EQ(mBooks->bookResult(kSkillBook), "You have already read this book.");
        // the same read-once set as the inventory's: reading it from the inventory afterwards raises nothing either
        ASSERT_EQ(mInventory->model().add(inventory::kPlayerOwner, kSkillBook, 1), inventory::TransferStatus::Ok);
        EXPECT_EQ(mBooks->readBook(kSkillBook), LockBooksService::ReadStatus::Ok);
        EXPECT_EQ(mRaised.size(), 1u);
        EXPECT_EQ(mReads.size(), 2u) << "the inventory read does not tell the scripts (OnRead is for placed books)";
    }

    TEST_F(WorldBooksFx, aSkillBookReadFirstInTheInventoryIsAlreadyReadInTheWorld)
    {
        waitReady();
        ASSERT_EQ(mInventory->model().add(inventory::kPlayerOwner, kSkillBook, 1), inventory::TransferStatus::Ok);
        ASSERT_EQ(mBooks->readBook(kSkillBook), LockBooksService::ReadStatus::Ok);
        ASSERT_EQ(mRaised.size(), 1u);
        read(kPlacementA, kSkillBook);
        EXPECT_EQ(mRaised.size(), 1u);
    }

    TEST_F(WorldBooksFx, aSpellTomeReadInTheWorldTeachesItsSpellOnceAndStaysWhereItLies)
    {
        waitReady();
        read(kPlacementA, kTome);
        ASSERT_EQ(mLearned.size(), 1u);
        EXPECT_EQ(mLearned[0], kThunderbolt);
        EXPECT_EQ(mBooks->bookResult(kTome), "Learned Thunderbolt.");
        EXPECT_EQ(held(kTome), 0);
        read(kPlacementA, kTome);
        EXPECT_EQ(mLearned.size(), 1u);
        EXPECT_EQ(mBooks->bookResult(kTome), "You already know Thunderbolt.");
        EXPECT_EQ(mReads.size(), 2u);
    }

    TEST_F(WorldBooksFx, aTomeTakenAfterItsSpellWasLearnedTeachesNothingMore)
    {
        waitReady();
        read(kPlacementA, kTome);
        ASSERT_EQ(mLearned.size(), 1u);
        ASSERT_EQ(mInventory->model().add(inventory::kPlayerOwner, kTome, 1), inventory::TransferStatus::Ok);
        EXPECT_EQ(mBooks->readBook(kTome), LockBooksService::ReadStatus::Ok);
        EXPECT_EQ(mLearned.size(), 1u);
        EXPECT_EQ(held(kTome), 1) << "an already known tome is read and kept (task 0203)";
    }

    TEST_F(WorldBooksFx, theReadMutateRefusesBadIdsWithoutTellingTheScripts)
    {
        waitReady();
        (void)mBindings.call("read", { str("zz"), str(formatId(kNote)) });
        (void)mBindings.call("read", { str(formatId(kPlacementA)), str("not hex") });
        (void)mBindings.call("read", { str(formatId(kPlacementA)), str("123456789") }); // wider than a form id
        EXPECT_TRUE(mReads.empty());
        EXPECT_EQ(mWorld->readCount(), 0u);
    }

    TEST_F(WorldBooksFx, readCountFollowsTheReadsThroughTheBinding)
    {
        waitReady();
        EXPECT_EQ(mBindings.call("read_count", {}).value.integer, 0);
        read(kPlacementA, kNote);
        read(kPlacementB, kNote);
        EXPECT_EQ(mBindings.call("read_count", {}).value.integer, 2);
    }
}
