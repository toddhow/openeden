// Task 0203: the lock picking and book service and its "skyrimse.lockbooks" bindings over a stub bindings service and
// the real inventory service. A generated plugin written to a temporary file; no game data. Written before the code
// and run red against a stub first.
#include <game_skyrimse/lockbooks_service.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
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
    using openeden::sdk::BindingAccess;
    using openeden::sdk::BindingStatus;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kPlain = kSelf | 0x100;
    constexpr FormId kSkillBook = kSelf | 0x101; // teaches Alchemy (actor value 16, slot 10)
    constexpr FormId kTomeA = kSelf | 0x102; // teaches Thunderbolt
    constexpr FormId kTomeB = kSelf | 0x103; // the same spell
    constexpr FormId kLocalised = kSelf | 0x104;
    constexpr FormId kThunderbolt = 0x0010F7EE;

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
    BindingValue dbl(double d)
    {
        BindingValue v;
        v.type = BindingType::Double;
        v.number = d;
        return v;
    }
    BindingValue flag(bool b)
    {
        BindingValue v;
        v.type = BindingType::Bool;
        v.boolean = b;
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

    struct LockBooksFixture : testing::Test
    {
        std::filesystem::path mDir;
        std::filesystem::path mPath;
        std::vector<std::string> mLog;
        std::vector<std::pair<std::size_t, float>> mSkillUses;
        std::vector<std::size_t> mRaised;
        std::vector<FormId> mLearned;
        bool mCanRaise = true;
        float mSkill = 0.f;
        std::unique_ptr<inventory::InventoryService> mInventory;
        std::unique_ptr<LockBooksService> mService;
        StubBindings mBindings;
        bool mLocalisedPlugin = false;

        Bytes pluginBytes()
        {
            Bytes all = tes4Header(1.7f, mLocalisedPlugin ? 0x80u : 0u, "A", "B", 4, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes books;
            if (mLocalisedPlugin)
            {
                Bytes subs = sub4("EDID", Bytes().zstring("Loc"));
                subs.append(sub4("FULL", Bytes().u32(0x1234)));
                subs.append(sub4("DESC", Bytes().u32(0x5678)));
                subs.append(sub4("DATA", Bytes().u8(0).u8(0).u16(0).u32(0).u32(1).f32(1.f)));
                books.append(record4("BOOK", 0, kLocalised, subs));
            }
            else
            {
                books.append(bookRecord(kPlain, "Plain", 0, 0, "A Plain Book", "<p>First</p><p>Second</p>"));
                books.append(
                    bookRecord(kSkillBook, "SkillBookAlchemy", BookFlags::TeachesSkill, 16, "Alchemy Book", "alch"));
                books.append(
                    bookRecord(kTomeA, "TomeA", BookFlags::TeachesSpell, kThunderbolt, "Tome of Thunderbolt", "a"));
                books.append(
                    bookRecord(kTomeB, "TomeB", BookFlags::TeachesSpell, kThunderbolt, "Other Thunderbolt", "b"));
            }
            all.append(group4(Content::fourCC("BOOK"), 0, books));
            all.append(group4(
                Content::fourCC("MISC"), 0, record4("MISC", 0, kLockpickItem, sub4("DATA", Bytes().u32(5).f32(0.f)))));
            all.append(group4(
                Content::fourCC("MISC"), 0, record4("MISC", 0, 0x0000000F, sub4("DATA", Bytes().u32(1).f32(0.f)))));
            return all;
        }

        void SetUp() override { build(); }

        void build()
        {
            mService.reset();
            mInventory.reset();
            const Bytes all = pluginBytes();
            mDir = std::filesystem::temp_directory_path()
                / (std::string("oe_lb_") + testing::UnitTest::GetInstance()->current_test_info()->name());
            std::filesystem::create_directories(mDir);
            mPath = mDir / "test.esp";
            std::ofstream(mPath, std::ios::binary)
                .write(all.str().data(), static_cast<std::streamsize>(all.str().size()));

            inventory::Hooks ih;
            ih.mInputs = [this](inventory::Inputs& out) {
                out.mPaths = { mPath };
                std::string err;
                EXPECT_TRUE(out.mOrder.add("Skyrim.esm", false, err));
                EXPECT_TRUE(out.mOrder.add("test.esp", false, err));
                return true;
            };
            ih.mName = [](FormId id) -> std::string {
                if (id == kLockpickItem)
                    return "Lockpick";
                if (id == kLocalised)
                    return "Fallback Name";
                return {};
            };
            mInventory = std::make_unique<inventory::InventoryService>(std::move(ih), 0.f, 1);

            ServiceHooks hooks;
            hooks.mInputs = [this](BookInputs& out) {
                out.mPaths = { mPath };
                std::string err;
                EXPECT_TRUE(out.mOrder.add("Skyrim.esm", false, err));
                EXPECT_TRUE(out.mOrder.add("test.esp", false, err));
                return true;
            };
            hooks.mResolve = [](const LString& text, std::uint32_t plugin, StringTableKind kind) -> std::string {
                EXPECT_EQ(plugin, 0u);
                if (!text.mLocalized)
                    return text.mText;
                if (kind == StringTableKind::DlStrings && text.mId == 0x5678)
                    return "<p>Localised</p><p>text</p>";
                if (kind == StringTableKind::Strings && text.mId == 0x1234)
                    return "Localised Title";
                return {};
            };
            hooks.mName = [](FormId) { return std::string(); };
            hooks.mSkill = [this]() { return mSkill; };
            hooks.mSkillUsed = [this](std::size_t slot, float amount) { mSkillUses.emplace_back(slot, amount); };
            hooks.mRaiseSkill = [this](std::size_t slot) {
                if (!mCanRaise)
                    return false;
                mRaised.push_back(slot);
                return true;
            };
            hooks.mSkillName
                = [](std::size_t slot) { return slot == 10 ? std::string("Alchemy") : std::string("Skill"); };
            hooks.mSpellName
                = [](FormId spell) { return spell == kThunderbolt ? std::string("Thunderbolt") : std::string(); };
            hooks.mSpellLearned = [this](FormId spell) { mLearned.push_back(spell); };
            hooks.mLog = [this](const std::string& line) { mLog.push_back(line); };
            mService = std::make_unique<LockBooksService>(*mInventory, std::move(hooks));
            ASSERT_TRUE(mService->declare(mBindings).ok);
        }

        void TearDown() override
        {
            mService.reset();
            mInventory.reset();
            std::error_code ec;
            std::filesystem::remove_all(mDir, ec);
        }

        void waitReady()
        {
            for (int i = 0; i < 4000 && !(mInventory->poll() && mService->poll()); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ASSERT_TRUE(mInventory->ready());
            ASSERT_TRUE(mService->ready());
        }

        void give(FormId item, int count)
        {
            ASSERT_EQ(mInventory->model().add(inventory::kPlayerOwner, item, count), inventory::TransferStatus::Ok);
        }
        int held(FormId item) { return mInventory->model().count(inventory::kPlayerOwner, item); }
        std::string state() { return std::string(mBindings.call("lock_state", {}).value.text); }

        // Drives the minigame through the bindings until the state leaves "picking".
        std::string drive(double angle, bool turning, int limit = 600)
        {
            std::string s = state();
            for (int i = 0; i < limit && s == "picking"; ++i)
            {
                (void)mBindings.call("pick_input", { dbl(angle), flag(turning), dbl(1000.0 / 60.0) });
                s = state();
            }
            return s;
        }

        std::vector<std::string> strings(const std::string& fn, std::vector<BindingValue> args = {})
        {
            const auto r = mBindings.call(fn, std::move(args));
            std::vector<std::string> out;
            for (std::size_t i = 0; i < r.value.count; ++i)
                out.emplace_back(r.value.elements[i].text);
            return out;
        }

        std::string joinedLog()
        {
            std::string out;
            for (const std::string& line : mLog)
                out += line + "\n";
            return out;
        }
    };

    TEST_F(LockBooksFixture, theNamespaceIsDeclaredWithReadAndMutateFunctions)
    {
        EXPECT_EQ(mBindings.mName, "skyrimse.lockbooks");
        EXPECT_EQ(mBindings.mVersion.major, 1);
        EXPECT_EQ(mBindings.mVersion.minor, 0);
        for (const char* read : { "ready", "revision", "picks", "lock_state", "lock_reason", "lock_tier", "lock_level",
                 "lock_progress", "lock_strain", "lock_window", "lock_placement", "lock_picked", "book_known",
                 "book_title", "book_kind", "book_teaches", "book_pages", "book_result", "book_read", "spells" })
        {
            ASSERT_EQ(mBindings.mFunctions.count(read), 1u) << read;
            EXPECT_EQ(mBindings.mFunctions[read].access, BindingAccess::Read) << read;
        }
        for (const char* mutate : { "pick_begin", "pick_input", "pick_cancel", "read_book" })
        {
            ASSERT_EQ(mBindings.mFunctions.count(mutate), 1u) << mutate;
            EXPECT_EQ(mBindings.mFunctions[mutate].access, BindingAccess::Mutate) << mutate;
            EXPECT_FALSE(mBindings.mFunctions[mutate].hasResult) << mutate;
        }
    }

    TEST_F(LockBooksFixture, nothingIsWaitedForBeforeTheDataIsRead)
    {
        EXPECT_FALSE(mBindings.call("ready", {}).value.boolean);
        EXPECT_FALSE(mService->pickBegin(5, 0));
        EXPECT_NE(mService->reason().find("not ready"), std::string::npos) << mService->reason();
        EXPECT_EQ(mService->readBook(kPlain), LockBooksService::ReadStatus::NotReady);
        EXPECT_EQ(state(), "none");
    }

    TEST_F(LockBooksFixture, pickingNeedsALockpick)
    {
        waitReady();
        EXPECT_EQ(mBindings.call("picks", {}).value.integer, 0);
        EXPECT_FALSE(mService->pickBegin(5, 0));
        EXPECT_NE(mService->reason().find("lockpick"), std::string::npos) << mService->reason();
        EXPECT_EQ(mBindings.call("lock_reason", {}).value.text, mService->reason());
        give(kLockpickItem, 3);
        EXPECT_EQ(mBindings.call("picks", {}).value.integer, 3);
        EXPECT_TRUE(mService->pickBegin(5, 0));
        EXPECT_EQ(state(), "picking");
        EXPECT_EQ(mBindings.call("lock_tier", {}).value.text, "novice");
        EXPECT_EQ(mBindings.call("lock_level", {}).value.integer, 0);
        EXPECT_EQ(mBindings.call("lock_placement", {}).value.text, "5");
        EXPECT_NEAR(mBindings.call("lock_window", {}).value.number, 20.0, 1e-4);
        EXPECT_EQ(held(kLockpickItem), 3); // starting does not use a pick up
    }

    TEST_F(LockBooksFixture, aLockThatNeedsItsKeyIsRefusedAndCostsNothing)
    {
        waitReady();
        give(kLockpickItem, 2);
        EXPECT_FALSE(mService->pickBegin(5, 255));
        EXPECT_NE(mService->reason().find("key"), std::string::npos) << mService->reason();
        EXPECT_EQ(state(), "refused");
        EXPECT_EQ(held(kLockpickItem), 2);
    }

    TEST_F(LockBooksFixture, theSkillWidensTheWindow)
    {
        waitReady();
        give(kLockpickItem, 2);
        mSkill = 40.f;
        ASSERT_TRUE(mService->pickBegin(5, 0));
        EXPECT_NEAR(mBindings.call("lock_window", {}).value.number, 22.0, 1e-4);
    }

    TEST_F(LockBooksFixture, aSuccessfulPickRecordsTheLockTrainsTheSkillAndKeepsTheLockpick)
    {
        waitReady();
        give(kLockpickItem, 2);
        ASSERT_TRUE(mService->pickBegin(185905, 50)); // adept
        const double sweet = sweetSpotOf(185905);
        EXPECT_EQ(drive(sweet, true), "opened");
        EXPECT_NEAR(mBindings.call("lock_progress", {}).value.number, 1.0, 1e-6);
        EXPECT_TRUE(mService->lockPicked(185905));
        EXPECT_TRUE(mBindings.call("lock_picked", { str("2d631") }).value.boolean); // 185905 in hex
        EXPECT_FALSE(mBindings.call("lock_picked", { str("1") }).value.boolean);
        EXPECT_EQ(held(kLockpickItem), 2);
        ASSERT_EQ(mSkillUses.size(), 1u);
        EXPECT_EQ(mSkillUses[0].first, 8u); // Lockpicking
        EXPECT_FLOAT_EQ(mSkillUses[0].second, 30.f); // adept: 10 x 3
        EXPECT_NE(joinedLog().find("LOCKBOOKS"), std::string::npos);
        // more input after the end changes nothing
        (void)mBindings.call("pick_input", { dbl(0), flag(true), dbl(16) });
        EXPECT_EQ(mSkillUses.size(), 1u);
    }

    TEST_F(LockBooksFixture, aBrokenPickCostsOneLockpickAndCounts)
    {
        waitReady();
        give(kLockpickItem, 2);
        ASSERT_TRUE(mService->pickBegin(7, 0));
        const double bad = sweetSpotOf(7) > 0 ? -90.0 : 90.0;
        EXPECT_EQ(drive(bad, true), "broke");
        EXPECT_EQ(held(kLockpickItem), 1);
        EXPECT_EQ(mService->model().broken(), 1u);
        EXPECT_FALSE(mService->lockPicked(7));
        EXPECT_TRUE(mSkillUses.empty());
        // it breaks once, not once per further input
        (void)mBindings.call("pick_input", { dbl(bad), flag(true), dbl(16) });
        EXPECT_EQ(held(kLockpickItem), 1);
        // the next attempt takes the next lockpick; the sweet spot is the same
        ASSERT_TRUE(mService->pickBegin(7, 0));
        EXPECT_EQ(state(), "picking");
        EXPECT_EQ(drive(bad, true), "broke");
        EXPECT_EQ(held(kLockpickItem), 0);
        EXPECT_FALSE(mService->pickBegin(7, 0)); // none left
        EXPECT_NE(mService->reason().find("lockpick"), std::string::npos);
    }

    TEST_F(LockBooksFixture, cancellingCostsNothing)
    {
        waitReady();
        give(kLockpickItem, 1);
        ASSERT_TRUE(mService->pickBegin(7, 0));
        (void)mBindings.call("pick_cancel", {});
        EXPECT_EQ(state(), "cancelled");
        EXPECT_EQ(held(kLockpickItem), 1);
        EXPECT_EQ(mService->model().broken(), 0u);
    }

    TEST_F(LockBooksFixture, everyBeginCountsSoAWindowCanTellItsAnswerFromAnOldRefusal)
    {
        waitReady();
        const int first = mBindings.call("lock_serial", {}).value.integer;
        EXPECT_FALSE(mService->pickBegin(5, 0)); // no lockpick: refused
        const int second = mBindings.call("lock_serial", {}).value.integer;
        EXPECT_NE(second, first);
        EXPECT_EQ(state(), "refused");
        give(kLockpickItem, 1);
        EXPECT_TRUE(mService->pickBegin(5, 0)); // the next begin is a new answer, the refusal is gone
        EXPECT_NE(mBindings.call("lock_serial", {}).value.integer, second);
        EXPECT_EQ(state(), "picking");
        EXPECT_TRUE(mService->reason().empty());
        EXPECT_FALSE(mService->pickBegin(5, 255)); // a key-only lock: refused again, counted again
        EXPECT_EQ(state(), "refused");
    }

    TEST_F(LockBooksFixture, aLockThatWasPickedIsNotPickedAgain)
    {
        waitReady();
        give(kLockpickItem, 1);
        ASSERT_TRUE(mService->pickBegin(7, 0));
        EXPECT_EQ(drive(sweetSpotOf(7), true), "opened");
        EXPECT_FALSE(mService->pickBegin(7, 0));
        EXPECT_NE(mService->reason().find("already"), std::string::npos) << mService->reason();
    }

    TEST_F(LockBooksFixture, badPickArgumentsAreRefusedAndLoggedNotCrashes)
    {
        waitReady();
        give(kLockpickItem, 1);
        mLog.clear();
        (void)mBindings.call("pick_begin", { str("zzz"), num(0) });
        (void)mBindings.call("pick_begin", { str(""), num(0) });
        EXPECT_EQ(state(), "none");
        EXPECT_NE(joinedLog().find("refused"), std::string::npos) << joinedLog();
        // input with no attempt does nothing
        (void)mBindings.call("pick_input", { dbl(1), flag(true), dbl(16) });
        EXPECT_EQ(state(), "none");
        EXPECT_EQ(mBindings.call("pick_input", { dbl(std::nan("")), flag(true), dbl(16) }).status, BindingStatus::Ok);
    }

    TEST_F(LockBooksFixture, aPlainBookShowsItsTextAndChangesNothing)
    {
        waitReady();
        give(kPlain, 1);
        EXPECT_TRUE(mBindings.call("book_known", { str(formatId(kPlain)) }).value.boolean);
        EXPECT_FALSE(mBindings.call("book_known", { str(formatId(kLockpickItem)) }).value.boolean);
        EXPECT_EQ(mBindings.call("book_title", { str(formatId(kPlain)) }).value.text, "A Plain Book");
        EXPECT_EQ(mBindings.call("book_kind", { str(formatId(kPlain)) }).value.text, "book");
        EXPECT_EQ(strings("book_pages", { str(formatId(kPlain)) }), std::vector<std::string>{ "First\n\nSecond" });
        EXPECT_EQ(mBindings.call("book_teaches", { str(formatId(kPlain)) }).value.text, "");
        const int revision = mBindings.call("revision", {}).value.integer;
        (void)mBindings.call("read_book", { str(formatId(kPlain)) });
        EXPECT_EQ(mBindings.call("book_result", { str(formatId(kPlain)) }).value.text, "");
        EXPECT_TRUE(mSkillUses.empty() && mRaised.empty() && mLearned.empty());
        EXPECT_EQ(held(kPlain), 1);
        (void)revision;
    }

    TEST_F(LockBooksFixture, aSkillBookRaisesItsSkillOnceAndStays)
    {
        waitReady();
        give(kSkillBook, 1);
        EXPECT_EQ(mBindings.call("book_kind", { str(formatId(kSkillBook)) }).value.text, "skill");
        EXPECT_EQ(mBindings.call("book_teaches", { str(formatId(kSkillBook)) }).value.text, "Alchemy");
        EXPECT_FALSE(mBindings.call("book_read", { str(formatId(kSkillBook)) }).value.boolean);
        (void)mBindings.call("read_book", { str(formatId(kSkillBook)) });
        EXPECT_EQ(mRaised, std::vector<std::size_t>{ 10 });
        EXPECT_NE(
            mBindings.call("book_result", { str(formatId(kSkillBook)) }).value.text.find("Alchemy"), std::string::npos);
        EXPECT_TRUE(mBindings.call("book_read", { str(formatId(kSkillBook)) }).value.boolean);
        EXPECT_EQ(held(kSkillBook), 1); // the book is kept
        (void)mBindings.call("read_book", { str(formatId(kSkillBook)) });
        EXPECT_EQ(mRaised.size(), 1u); // once
        EXPECT_NE(
            mBindings.call("book_result", { str(formatId(kSkillBook)) }).value.text.find("already"), std::string::npos);
    }

    TEST_F(LockBooksFixture, aSkillBookDoesNotCountAsReadWhenTheSkillCannotRise)
    {
        waitReady();
        give(kSkillBook, 1);
        mCanRaise = false;
        (void)mBindings.call("read_book", { str(formatId(kSkillBook)) });
        EXPECT_FALSE(mBindings.call("book_read", { str(formatId(kSkillBook)) }).value.boolean);
        EXPECT_FALSE(mBindings.call("book_result", { str(formatId(kSkillBook)) }).value.text.empty());
        mCanRaise = true;
        (void)mBindings.call("read_book", { str(formatId(kSkillBook)) });
        EXPECT_EQ(mRaised.size(), 1u);
        EXPECT_TRUE(mBindings.call("book_read", { str(formatId(kSkillBook)) }).value.boolean);
    }

    TEST_F(LockBooksFixture, aSpellTomeTeachesItsSpellAndIsUsedUp)
    {
        waitReady();
        give(kTomeA, 1);
        give(kTomeB, 1);
        EXPECT_EQ(mBindings.call("book_kind", { str(formatId(kTomeA)) }).value.text, "spell");
        EXPECT_EQ(mBindings.call("book_teaches", { str(formatId(kTomeA)) }).value.text, "Thunderbolt");
        (void)mBindings.call("read_book", { str(formatId(kTomeA)) });
        EXPECT_EQ(mLearned, std::vector<FormId>{ kThunderbolt });
        EXPECT_EQ(held(kTomeA), 0);
        EXPECT_TRUE(mService->model().knowsSpell(kThunderbolt));
        EXPECT_NE(
            mBindings.call("book_result", { str(formatId(kTomeA)) }).value.text.find("Thunderbolt"), std::string::npos);
        EXPECT_EQ(strings("spells"), std::vector<std::string>{ formatId(kThunderbolt) });
        // another tome of the same spell: read, nothing learned, kept
        (void)mBindings.call("read_book", { str(formatId(kTomeB)) });
        EXPECT_EQ(mLearned.size(), 1u);
        EXPECT_EQ(held(kTomeB), 1);
        EXPECT_NE(
            mBindings.call("book_result", { str(formatId(kTomeB)) }).value.text.find("already"), std::string::npos);
    }

    TEST_F(LockBooksFixture, readingNeedsTheBookInTheInventoryAndABookRecord)
    {
        waitReady();
        EXPECT_EQ(mService->readBook(kSkillBook), LockBooksService::ReadStatus::NotHeld);
        EXPECT_TRUE(mRaised.empty());
        give(kLockpickItem, 1);
        EXPECT_EQ(mService->readBook(kLockpickItem), LockBooksService::ReadStatus::NotABook);
        EXPECT_EQ(mService->readBook(kSelf | 0x999), LockBooksService::ReadStatus::NotABook);
        give(kPlain, 1);
        EXPECT_EQ(mService->readBook(kPlain), LockBooksService::ReadStatus::Ok);
        (void)mBindings.call("read_book", { str("nothex") });
        EXPECT_NE(joinedLog().find("refused"), std::string::npos);
    }

    TEST_F(LockBooksFixture, theStateSurvivesASaveAndALoadInANewService)
    {
        waitReady();
        give(kLockpickItem, 1);
        give(kSkillBook, 1);
        give(kTomeA, 1);
        ASSERT_TRUE(mService->pickBegin(185905, 0));
        ASSERT_EQ(drive(sweetSpotOf(185905), true), "opened");
        (void)mBindings.call("read_book", { str(formatId(kSkillBook)) });
        (void)mBindings.call("read_book", { str(formatId(kTomeA)) });

        struct W final : openeden::sdk::ChunkWriter
        {
            void write(const void* d, std::size_t n) override
            {
                const auto* b = static_cast<const unsigned char*>(d);
                bytes.insert(bytes.end(), b, b + n);
            }
            std::vector<unsigned char> bytes;
        } writer;
        mService->chunk().write(writer);

        build(); // a new session
        mRaised.clear();
        mLearned.clear();
        waitReady();
        EXPECT_FALSE(mService->lockPicked(185905));
        struct R final : openeden::sdk::ChunkReader
        {
            explicit R(const std::vector<unsigned char>& b)
                : bytes(b)
            {
            }
            bool read(void* d, std::size_t n) override
            {
                if (n > remaining())
                    return false;
                std::memcpy(d, bytes.data() + pos, n);
                pos += n;
                return true;
            }
            std::size_t remaining() const override { return bytes.size() - pos; }
            const std::vector<unsigned char>& bytes;
            std::size_t pos = 0;
        } reader(writer.bytes);
        ASSERT_TRUE(mService->chunk().read(reader, 1)) << mService->chunk().refusal();
        EXPECT_TRUE(mService->lockPicked(185905));
        EXPECT_TRUE(mBindings.call("book_read", { str(formatId(kSkillBook)) }).value.boolean);
        EXPECT_TRUE(mService->model().knowsSpell(kThunderbolt));
        // the restored session does not raise the skill again
        give(kSkillBook, 1);
        (void)mBindings.call("read_book", { str(formatId(kSkillBook)) });
        EXPECT_TRUE(mRaised.empty());
    }

    TEST_F(LockBooksFixture, aLocalisedBookTakesItsTitleAndTextThroughTheResolver)
    {
        mLocalisedPlugin = true;
        build();
        waitReady();
        give(kLocalised, 1);
        EXPECT_EQ(mBindings.call("book_title", { str(formatId(kLocalised)) }).value.text, "Localised Title");
        EXPECT_EQ(
            strings("book_pages", { str(formatId(kLocalised)) }), std::vector<std::string>{ "Localised\n\ntext" });
    }
}
