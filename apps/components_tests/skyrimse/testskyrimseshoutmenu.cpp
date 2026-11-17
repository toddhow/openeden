// Task 0266 (the Skyrim shout menu): the model behind the window (ShoutService::menu, menuUnlockNext, menuEquip), the
// TNAM translation of a word and the script functions that carry them. Generated records; no game data. Written
// first and run red against stubs (see the stage log of the task).
#include <game_skyrimse/shouts.hpp>
#include <game_skyrimse/shouts_service.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using namespace game_skyrimse::shouts;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    constexpr FormId kFus = 0x13E08, kRo = 0x13E09, kDah = 0x13E0A, kUnrelenting = 0x13E07;
    constexpr FormId kYol = 0x20E08, kToor = 0x20E09, kShul = 0x20E0A, kFire = 0x20E07;

    ShoutRecord makeMenuShout(FormId id, const char* name, FormId w1, FormId w2, FormId w3)
    {
        ShoutRecord s;
        s.mCommon.mId = id;
        s.mCommon.mEditorId = name;
        s.mHasData = true;
        s.mWordCount = 3;
        s.mWords[0].mWord = w1;
        s.mWords[1].mWord = w2;
        s.mWords[2].mWord = w3;
        return s;
    }

    WordRecord makeMenuWord(FormId id, const char* name, std::uint32_t nameId, std::uint32_t translationId)
    {
        WordRecord w;
        w.mCommon.mId = id;
        w.mCommon.mEditorId = name;
        w.mName.mPresent = true;
        w.mName.mLocalized = true;
        w.mName.mId = nameId;
        w.mTranslation.mPresent = true;
        w.mTranslation.mLocalized = true;
        w.mTranslation.mId = translationId;
        return w;
    }

    class MenuStubBindings final : public openeden::sdk::BindingsService
    {
    public:
        openeden::sdk::DeclareResult declareNamespace(const openeden::sdk::NamespaceDescriptor& d) override
        {
            mName = std::string(d.name);
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
        std::map<std::string, openeden::sdk::FunctionDescriptor> mFunctions;
    };

    BindingValue menuNum(std::int32_t i)
    {
        BindingValue v;
        v.type = BindingType::Int32;
        v.integer = i;
        return v;
    }

    BindingValue menuStr(std::string_view s)
    {
        BindingValue v;
        v.type = BindingType::String;
        v.text = s;
        return v;
    }

    class MenuMemoryWriter final : public openeden::sdk::ChunkWriter
    {
    public:
        void write(const void* data, std::size_t size) override
        {
            const auto* bytes = static_cast<const unsigned char*>(data);
            mBytes.insert(mBytes.end(), bytes, bytes + size);
        }
        std::vector<unsigned char> mBytes;
    };

    class MenuMemoryReader final : public openeden::sdk::ChunkReader
    {
    public:
        explicit MenuMemoryReader(const std::vector<unsigned char>& bytes)
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

    struct ShoutMenuTest : ::testing::Test
    {
        ShoutIndex mIndex;
        bool mReady = true;
        bool mWithText = true;
        std::map<std::uint32_t, std::string> mTexts{ { 1, "Fus" }, { 2, "Force" }, { 3, "Ro" }, { 4, "Balance" },
            { 5, "Dah" }, { 6, "Push" }, { 7, "Yol" }, { 8, "Fire" } };
        std::unique_ptr<ShoutService> mService;

        void SetUp() override
        {
            mIndex.add({ makeMenuShout(kUnrelenting, "UnrelentingForceShout", kFus, kRo, kDah),
                           makeMenuShout(kFire, "FireBreathShout", kYol, kToor, kShul) },
                { makeMenuWord(kFus, "WordFus", 1, 2), makeMenuWord(kRo, "WordRo", 3, 4),
                    makeMenuWord(kDah, "WordDah", 5, 6), makeMenuWord(kYol, "WordYol", 7, 8),
                    makeMenuWord(kToor, "WordToor", 90, 91), makeMenuWord(kShul, "WordShul", 92, 93) });
            Hooks hooks;
            hooks.mIndex = [this]() -> const ShoutIndex* { return mReady ? &mIndex : nullptr; };
            hooks.mLog = [](const std::string&) {};
            hooks.mText = [this](const LString& text, FormId) -> std::string {
                if (!mWithText)
                    return {};
                const auto it = mTexts.find(text.mId);
                return it == mTexts.end() ? std::string() : it->second;
            };
            mService = std::make_unique<ShoutService>(std::move(hooks));
        }

        std::string lastNotice() const
        {
            const std::uint64_t n = mService->state().noticeCount();
            return n == 0 ? std::string() : mService->state().noticeAt(n);
        }
    };

    // ---- the record --------------------------------------------------------------------------------------------------

    TEST(SkyrimShoutMenuRecords, aWordKeepsItsTnamTranslation)
    {
        LoadOrder order;
        std::string err;
        ASSERT_TRUE(order.add("Skyrim.esm", false, err));
        ASSERT_TRUE(order.add("test.esp", false, err));
        Content::Error error;
        auto plugin = Content::Plugin::open(
            std::make_unique<std::istringstream>(
                tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44).str(), std::ios::binary),
            "test.esp", error);
        ASSERT_TRUE(plugin != nullptr);
        ExteriorLimits limits;
        DecodeNotes notes;
        FormIdResolver resolver{ plugin->header(), "test.esp", order };
        const DecodeContext context{ resolver, limits, notes, true }; // a localised plugin: the strings are ids
        Bytes woop;
        woop.append(sub4("EDID", Bytes().zstring("WordFus")));
        woop.append(sub4("FULL", Bytes().u32(0x10)));
        woop.append(sub4("TNAM", Bytes().u32(0x11)));
        Content::RawRecord record;
        record.mType = Content::fourCC("WOOP");
        record.mFormId = 0x13E08;
        WordRecord w;
        std::string why;
        const std::string& bytes = woop.str();
        ASSERT_TRUE(decodeWord(
            { reinterpret_cast<const std::byte*>(bytes.data()), bytes.size() }, record, context, w, why))
            << why;
        EXPECT_TRUE(w.mName.mPresent);
        EXPECT_EQ(w.mName.mId, 0x10u);
        EXPECT_TRUE(w.mTranslation.mPresent);
        EXPECT_TRUE(w.mTranslation.mLocalized);
        EXPECT_EQ(w.mTranslation.mId, 0x11u);
    }

    // ---- the model ---------------------------------------------------------------------------------------------------

    TEST_F(ShoutMenuTest, theMenuIsEmptyBeforeTheRecordsAreReadAndBeforeAnyWordIsLearned)
    {
        mReady = false;
        EXPECT_TRUE(mService->menu().empty());
        mReady = true;
        EXPECT_TRUE(mService->menu().empty());
    }

    TEST_F(ShoutMenuTest, onlyShoutsWithALearnedWordAreListedByFormId)
    {
        ASSERT_TRUE(mService->teachWord(kYol));
        ASSERT_TRUE(mService->teachWord(kFus));
        const std::vector<MenuShout> menu = mService->menu();
        ASSERT_EQ(menu.size(), 2u);
        EXPECT_EQ(menu[0].mId, "UnrelentingForceShout"); // 0x13E07 comes before 0x20E07
        EXPECT_EQ(menu[1].mId, "FireBreathShout");
        EXPECT_EQ(menu[0].mName, "Unrelenting Force");
        EXPECT_EQ(menu[1].mName, "Fire Breath");
        EXPECT_EQ(menu[0].mWords.size(), 3u);
    }

    TEST_F(ShoutMenuTest, aWordShowsItsTextAndItsStateAndAnUnknownWordShowsNothing)
    {
        ASSERT_TRUE(mService->teachWord(kFus));
        ASSERT_TRUE(mService->teachWord(kRo));
        mService->state().addSouls(1);
        ASSERT_TRUE(mService->state().unlock(kFus));
        const MenuShout shout = mService->menu().at(0);
        EXPECT_EQ(shout.mWords[0].mName, "Fus");
        EXPECT_EQ(shout.mWords[0].mTranslation, "Force");
        EXPECT_EQ(shout.mWords[0].mState, "unlocked");
        EXPECT_EQ(shout.mWords[1].mName, "Ro");
        EXPECT_EQ(shout.mWords[1].mTranslation, "Balance");
        EXPECT_EQ(shout.mWords[1].mState, "learned");
        EXPECT_EQ(shout.mWords[2].mName, "");
        EXPECT_EQ(shout.mWords[2].mTranslation, "");
        EXPECT_EQ(shout.mWords[2].mState, "unknown");
    }

    TEST_F(ShoutMenuTest, withoutTheTextHookAWordIsNamedByItsEditorIdAndHasNoTranslation)
    {
        mWithText = false;
        ASSERT_TRUE(mService->teachWord(kFus));
        const MenuShout shout = mService->menu().at(0);
        EXPECT_EQ(shout.mWords[0].mName, "Fus"); // WordFus without its "Word"
        EXPECT_EQ(shout.mWords[0].mTranslation, "");
    }

    TEST_F(ShoutMenuTest, aWordWhoseStringCannotBeReadFallsBackToItsEditorId)
    {
        ASSERT_TRUE(mService->teachWord(kToor)); // texts 90 and 91 are not in the map
        const MenuShout shout = mService->menu().at(0);
        EXPECT_EQ(shout.mId, "FireBreathShout");
        EXPECT_EQ(shout.mWords[1].mName, "Toor");
        EXPECT_EQ(shout.mWords[1].mTranslation, "");
    }

    TEST_F(ShoutMenuTest, canUnlockNeedsTheNextWordLearnedAndASoulAndCanEquipNeedsAnUnlockedWord)
    {
        ASSERT_TRUE(mService->teachWord(kFus));
        MenuShout shout = mService->menu().at(0);
        EXPECT_FALSE(shout.mCanUnlock); // no soul
        EXPECT_FALSE(shout.mCanEquip); // nothing unlocked
        mService->state().addSouls(1);
        shout = mService->menu().at(0);
        EXPECT_TRUE(shout.mCanUnlock);
        ASSERT_TRUE(mService->state().unlock(kFus));
        mService->state().addSouls(1);
        shout = mService->menu().at(0);
        EXPECT_TRUE(shout.mCanEquip);
        EXPECT_FALSE(shout.mCanUnlock); // Ro is not learned
    }

    TEST_F(ShoutMenuTest, theEquippedShoutIsMarked)
    {
        ASSERT_TRUE(mService->teachWord(kFus));
        ASSERT_TRUE(mService->teachWord(kYol));
        mService->state().equip(kFire);
        const std::vector<MenuShout> menu = mService->menu();
        ASSERT_EQ(menu.size(), 2u);
        EXPECT_FALSE(menu[0].mEquipped);
        EXPECT_TRUE(menu[1].mEquipped);
    }

    // ---- unlocking ---------------------------------------------------------------------------------------------------

    TEST_F(ShoutMenuTest, anUnlockSpendsOneSoulOnTheNextWordInOrderAndTellsThePlayer)
    {
        ASSERT_TRUE(mService->teachWord(kFus));
        ASSERT_TRUE(mService->teachWord(kRo));
        mService->state().addSouls(2);
        EXPECT_EQ(mService->menuUnlockNext("UnrelentingForceShout"), MenuResult::Done);
        EXPECT_TRUE(mService->state().unlocked(kFus));
        EXPECT_FALSE(mService->state().unlocked(kRo));
        EXPECT_EQ(mService->state().souls(), 1u);
        EXPECT_EQ(lastNotice(), "Word of Power unlocked: Fus");
        EXPECT_EQ(mService->menuUnlockNext("unrelentingforceshout"), MenuResult::Done); // not case sensitive
        EXPECT_TRUE(mService->state().unlocked(kRo));
        EXPECT_EQ(mService->state().souls(), 0u);
    }

    TEST_F(ShoutMenuTest, anUnlockWithNoSoulOrAnUnlearnedNextWordOrAnUnknownShoutChangesNothing)
    {
        ASSERT_TRUE(mService->teachWord(kFus));
        EXPECT_EQ(mService->menuUnlockNext("UnrelentingForceShout"), MenuResult::NoSoul);
        EXPECT_FALSE(mService->state().unlocked(kFus));
        EXPECT_EQ(lastNotice(), "You need a dragon soul to unlock a word.");
        mService->state().addSouls(5);
        EXPECT_EQ(mService->menuUnlockNext("UnrelentingForceShout"), MenuResult::Done);
        EXPECT_EQ(mService->menuUnlockNext("UnrelentingForceShout"), MenuResult::NotLearned); // Ro never learned
        EXPECT_EQ(mService->state().souls(), 4u);
        EXPECT_EQ(lastNotice(), "You have not learned the next word of Unrelenting Force.");
        EXPECT_EQ(mService->menuUnlockNext("NoSuchShout"), MenuResult::NoShout);
        EXPECT_EQ(mService->menuUnlockNext(""), MenuResult::NoShout);
        EXPECT_EQ(mService->state().souls(), 4u);
    }

    TEST_F(ShoutMenuTest, aShoutWithAllThreeWordsUnlockedRefusesAnotherUnlock)
    {
        for (const FormId w : { kFus, kRo, kDah })
            ASSERT_TRUE(mService->unlockWord(w));
        mService->state().addSouls(3);
        EXPECT_EQ(mService->menuUnlockNext("UnrelentingForceShout"), MenuResult::AllUnlocked);
        EXPECT_EQ(mService->state().souls(), 3u);
    }

    TEST_F(ShoutMenuTest, anUnlockBeforeTheRecordsAreReadIsRefused)
    {
        mReady = false;
        EXPECT_EQ(mService->menuUnlockNext("UnrelentingForceShout"), MenuResult::NoShout);
        EXPECT_EQ(mService->menuEquip("UnrelentingForceShout"), MenuResult::NoShout);
    }

    // ---- equipping ---------------------------------------------------------------------------------------------------

    TEST_F(ShoutMenuTest, equippingNeedsAnUnlockedWordAndChangesTheShoutTheKeyUses)
    {
        ASSERT_TRUE(mService->teachWord(kFus));
        EXPECT_EQ(mService->menuEquip("UnrelentingForceShout"), MenuResult::NothingUnlocked);
        EXPECT_EQ(mService->state().equipped(), kNullFormId);
        mService->state().addSouls(1);
        ASSERT_EQ(mService->menuUnlockNext("UnrelentingForceShout"), MenuResult::Done);
        EXPECT_EQ(mService->menuEquip("UnrelentingForceShout"), MenuResult::Done);
        EXPECT_EQ(mService->state().equipped(), kUnrelenting);
        EXPECT_EQ(mService->equippedName(), "Unrelenting Force");
        EXPECT_EQ(mService->equippedWords(), 1u);
        EXPECT_EQ(lastNotice(), "Unrelenting Force equipped");
        EXPECT_EQ(mService->menuEquip("Missing"), MenuResult::NoShout);
        EXPECT_EQ(mService->state().equipped(), kUnrelenting);
    }

    TEST_F(ShoutMenuTest, theEquipChoiceSurvivesTheShoutsChunk)
    {
        ASSERT_TRUE(mService->unlockWord(kYol));
        ASSERT_EQ(mService->menuEquip("FireBreathShout"), MenuResult::Done);
        MenuMemoryWriter writer;
        ShoutChunk chunk(mService->state());
        chunk.write(writer);
        ShoutState back;
        MenuMemoryReader reader(writer.mBytes);
        ShoutChunk backChunk(back);
        ASSERT_TRUE(backChunk.read(reader, ShoutChunk::kVersion));
        EXPECT_EQ(back.equipped(), kFire);
        EXPECT_TRUE(back.unlocked(kYol));
    }

    // ---- the script functions ----------------------------------------------------------------------------------------

    TEST_F(ShoutMenuTest, theScriptFunctionsReadTheMenuAndTheMutatingOnesTakeTheShoutsEditorId)
    {
        MenuStubBindings bindings;
        ASSERT_TRUE(mService->declare(bindings).ok);
        EXPECT_EQ(bindings.call("menu_count").value.integer, 0);
        ASSERT_TRUE(mService->teachWord(kFus));
        ASSERT_TRUE(mService->teachWord(kYol));
        mService->state().addSouls(1);
        EXPECT_EQ(bindings.call("menu_count").value.integer, 2);
        EXPECT_EQ(bindings.call("menu_id", { menuNum(0) }).value.text, "UnrelentingForceShout");
        EXPECT_EQ(bindings.call("menu_name", { menuNum(1) }).value.text, "Fire Breath");
        EXPECT_FALSE(bindings.call("menu_equipped", { menuNum(0) }).value.boolean);
        EXPECT_FALSE(bindings.call("menu_can_equip", { menuNum(0) }).value.boolean);
        EXPECT_TRUE(bindings.call("menu_can_unlock", { menuNum(0) }).value.boolean);
        EXPECT_EQ(bindings.call("menu_word_count", { menuNum(0) }).value.integer, 3);
        EXPECT_EQ(bindings.call("menu_word_name", { menuNum(0), menuNum(0) }).value.text, "Fus");
        EXPECT_EQ(bindings.call("menu_word_translation", { menuNum(0), menuNum(0) }).value.text, "Force");
        EXPECT_EQ(bindings.call("menu_word_state", { menuNum(0), menuNum(1) }).value.text, "unknown");
        // out of range answers empty, never throws
        EXPECT_EQ(bindings.call("menu_id", { menuNum(9) }).value.text, "");
        EXPECT_EQ(bindings.call("menu_word_count", { menuNum(-1) }).value.integer, 0);
        EXPECT_EQ(bindings.call("menu_word_name", { menuNum(0), menuNum(7) }).value.text, "");
        EXPECT_FALSE(bindings.call("menu_can_unlock", { menuNum(9) }).value.boolean);
        // the mutating functions
        (void)bindings.call("menu_unlock", { menuStr("UnrelentingForceShout") });
        EXPECT_TRUE(mService->state().unlocked(kFus));
        (void)bindings.call("menu_equip", { menuStr("UnrelentingForceShout") });
        EXPECT_EQ(mService->state().equipped(), kUnrelenting);
        ASSERT_TRUE(bindings.mFunctions.count("menu_unlock") == 1 && bindings.mFunctions.count("menu_equip") == 1
            && bindings.mFunctions.count("menu_count") == 1);
        EXPECT_EQ(bindings.mFunctions.at("menu_unlock").access, openeden::sdk::BindingAccess::Mutate);
        EXPECT_EQ(bindings.mFunctions.at("menu_equip").access, openeden::sdk::BindingAccess::Mutate);
        EXPECT_EQ(bindings.mFunctions.at("menu_count").access, openeden::sdk::BindingAccess::Read);
    }

    TEST_F(ShoutMenuTest, theMenuChangesTheRevisionTheWindowWatches)
    {
        ASSERT_TRUE(mService->teachWord(kFus));
        const auto before = mService->state().revision();
        mService->state().addSouls(1);
        ASSERT_EQ(mService->menuUnlockNext("UnrelentingForceShout"), MenuResult::Done);
        EXPECT_GT(mService->state().revision(), before);
        const auto mid = mService->state().revision();
        ASSERT_EQ(mService->menuEquip("UnrelentingForceShout"), MenuResult::Done);
        EXPECT_GT(mService->state().revision(), mid);
    }
}
