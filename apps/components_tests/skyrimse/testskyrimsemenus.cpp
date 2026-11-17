// Task 0207 (plan section 4.24): the LSCR records, the seeded tip pick and the "skyrimse.menus" bindings.
// Generated plugins; no game data.
#include <game_skyrimse/menus.hpp>

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using namespace game_skyrimse::menus;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    constexpr FormId kSelf = 0x01000000;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes ctda(std::uint16_t function)
    {
        Bytes b;
        b.u8(0).u8(0).u8(0).u8(0).f32(1.f);
        b.u16(function).u16(0).u32(0).u32(0).u32(0).u32(0).u32(0);
        return sub4("CTDA", b);
    }

    Bytes screen(FormId id, const char* editorId, const Bytes& desc, std::uint32_t recordFlags = 0,
        bool withCondition = false, bool badCondition = false)
    {
        Bytes data;
        data.append(sub4("EDID", Bytes().zstring(editorId)));
        if (desc.size() > 0)
            data.append(sub4("DESC", desc));
        if (withCondition)
            data.append(ctda(300));
        if (badCondition)
            data.append(sub4("CTDA", Bytes().u32(1)));
        data.append(sub4("NNAM", Bytes().u32(0x100)));
        data.append(sub4("SNAM", Bytes().f32(1.f)));
        return record4("LSCR", recordFlags, id, data);
    }

    Bytes testPlugin(bool localized = false)
    {
        const auto text
            = [&](const char* s, std::uint32_t id) { return localized ? Bytes().u32(id) : Bytes().zstring(s); };
        Bytes records;
        records.append(screen(kSelf | 0x10, "TipA", text("Dragons are old.", 5)));
        records.append(screen(kSelf | 0x11, "TipB", text("Mind the bears.", 6), 0, true));
        records.append(screen(kSelf | 0x12, "NoText", Bytes()));
        records.append(screen(kSelf | 0x13, "Gone", text("deleted", 7), 0x20));
        records.append(screen(kSelf | 0x14, "BadCond", text("never", 8), 0, false, true));
        Bytes all = tes4Header(1.7f, localized ? 0x80 : 0, "A", "B", 4, { { "Skyrim.esm", 0 } }, 24, 44);
        all.append(group4(Content::fourCC("LSCR"), 0, records));
        return all;
    }

    LoadingScreenRecords parse(const Bytes& bytes, bool& ok)
    {
        Content::Error error;
        const auto plugin = Content::Plugin::open(
            std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
        LoadingScreenRecords out;
        ok = plugin != nullptr && parseLoadingScreens(*plugin, order(), LoadingScreenLimits(), out);
        return out;
    }

    LoadingScreen make(FormId id, const char* text, bool conditionsOk = true)
    {
        LoadingScreen s;
        s.mCommon.mId = id;
        s.mText.mPresent = true;
        s.mText.mText = text;
        s.mConditionsOk = conditionsOk;
        return s;
    }

    // ---- the parse ----------------------------------------------------------------------------------------------

    TEST(SkyrimMenusParse, readsTheTextTheConditionsAndTheObjectOfEachScreen)
    {
        bool ok = false;
        const LoadingScreenRecords r = parse(testPlugin(), ok);
        ASSERT_TRUE(ok);
        ASSERT_EQ(r.mScreens.size(), 5u);
        EXPECT_EQ(r.mScreens[0].mCommon.mId, kSelf | 0x10);
        EXPECT_EQ(r.mScreens[0].mCommon.mEditorId, "TipA");
        EXPECT_EQ(r.mScreens[0].mText.mText, "Dragons are old.");
        EXPECT_FALSE(r.mScreens[0].mText.mLocalized);
        EXPECT_EQ(r.mScreens[0].mObject, 0x100u);
        EXPECT_TRUE(r.mScreens[0].mConditions.empty());
        EXPECT_EQ(r.mScreens[1].mConditions.size(), 1u);
        EXPECT_EQ(r.mScreens[1].mConditions[0].mFunction, 300);
        EXPECT_TRUE(r.mScreens[1].mConditionsOk);
    }

    TEST(SkyrimMenusParse, aLocalisedTextIsAStringIdNotAString)
    {
        bool ok = false;
        const LoadingScreenRecords r = parse(testPlugin(true), ok);
        ASSERT_TRUE(ok);
        ASSERT_GE(r.mScreens.size(), 1u);
        EXPECT_TRUE(r.mScreens[0].mText.mLocalized);
        EXPECT_EQ(r.mScreens[0].mText.mId, 5u);
        EXPECT_TRUE(r.mScreens[0].mText.mText.empty());
    }

    TEST(SkyrimMenusParse, aScreenWithNoTextIsKeptWithAnAbsentText)
    {
        bool ok = false;
        const LoadingScreenRecords r = parse(testPlugin(), ok);
        ASSERT_TRUE(ok);
        EXPECT_FALSE(r.mScreens[2].mText.mPresent);
    }

    TEST(SkyrimMenusParse, aDamagedConditionMakesTheScreenNeverPass)
    {
        bool ok = false;
        const LoadingScreenRecords r = parse(testPlugin(), ok);
        ASSERT_TRUE(ok);
        EXPECT_FALSE(r.mScreens[4].mConditionsOk);
        EXPECT_TRUE(r.mScreens[3].mConditionsOk);
    }

    TEST(SkyrimMenusParse, aDeletedRecordIsListedAsDeleted)
    {
        bool ok = false;
        const LoadingScreenRecords r = parse(testPlugin(), ok);
        ASSERT_TRUE(ok);
        EXPECT_TRUE(r.mScreens[3].mCommon.deleted());
    }

    TEST(SkyrimMenusParse, aPluginThatIsNotSkyrimSeIsRefused)
    {
        Bytes all = tes4Header(1.3f, 0, "A", "B", 0, {}, 16, 0);
        bool ok = true;
        const LoadingScreenRecords r = parse(all, ok);
        EXPECT_FALSE(ok);
        EXPECT_TRUE(r.mScreens.empty());
    }

    // ---- the index ----------------------------------------------------------------------------------------------

    TEST(SkyrimMenusIndex, aLaterPluginReplacesAnEarlierRecordAndADeletedOneRemovesIt)
    {
        const LoadOrder o = order();
        const Content::Header header{};
        LoadingScreenIndex index;
        LoadingScreenRecords a;
        a.mScreens = { make(10, "old"), make(11, "stays"), make(12, "removed") };
        index.add(std::move(a), FormIdResolver(header, "Skyrim.esm", o));
        LoadingScreenRecords b;
        b.mScreens = { make(10, "new"), make(12, "x") };
        b.mScreens[1].mCommon.mFlags = RecordFlags::Deleted;
        index.add(std::move(b), FormIdResolver(header, "test.esp", o));
        const auto list = index.screens();
        ASSERT_EQ(list.size(), 2u);
        EXPECT_EQ(list[0]->mCommon.mId, 10u);
        EXPECT_EQ(list[0]->mText.mText, "new");
        EXPECT_EQ(list[0]->mPlugin, 1u);
        EXPECT_EQ(list[1]->mCommon.mId, 11u);
        EXPECT_EQ(list[1]->mPlugin, 0u);
    }

    TEST(SkyrimMenusIndex, theScreensComeInFormIdOrder)
    {
        const LoadOrder o = order();
        const Content::Header header{};
        LoadingScreenIndex index;
        LoadingScreenRecords a;
        a.mScreens = { make(30, "c"), make(10, "a"), make(20, "b") };
        index.add(std::move(a), FormIdResolver(header, "Skyrim.esm", o));
        const auto list = index.screens();
        ASSERT_EQ(list.size(), 3u);
        EXPECT_EQ(list[0]->mCommon.mId, 10u);
        EXPECT_EQ(list[1]->mCommon.mId, 20u);
        EXPECT_EQ(list[2]->mCommon.mId, 30u);
    }

    // ---- the pick -----------------------------------------------------------------------------------------------

    struct SkyrimMenusPickFixture : testing::Test
    {
        LoadingScreenIndex index;
        std::set<FormId> failing;

        void SetUp() override
        {
            const LoadOrder o = order();
            const Content::Header header{};
            LoadingScreenRecords a;
            a.mScreens = { make(1, "one"), make(2, "two"), make(3, ""), make(4, "four", false), make(5, "five") };
            a.mScreens[2].mText.mPresent = false;
            index.add(std::move(a), FormIdResolver(header, "Skyrim.esm", o));
        }

        TipInputs inputs()
        {
            TipInputs in;
            in.mScreens = &index;
            in.mPasses = [this](const LoadingScreen& s) { return failing.count(s.mCommon.mId) == 0; };
            in.mText = [](const LoadingScreen& s) { return s.mText.mText; };
            return in;
        }
    };

    TEST_F(SkyrimMenusPickFixture, onlyScreensWithTextWholeConditionsAndPassingConditionsAreCandidates)
    {
        failing = { 5 };
        std::set<std::string> seen;
        for (std::uint64_t seed = 0; seed < 200; ++seed)
        {
            Tip tip;
            ASSERT_TRUE(pickTip(inputs(), seed, tip));
            seen.insert(tip.mText);
            EXPECT_EQ(tip.mCandidates, 2u);
        }
        EXPECT_EQ(seen, (std::set<std::string>{ "one", "two" }));
    }

    TEST_F(SkyrimMenusPickFixture, theSameSeedGivesTheSameTipAndManySeedsCoverEveryCandidate)
    {
        Tip a, b;
        ASSERT_TRUE(pickTip(inputs(), 42, a));
        ASSERT_TRUE(pickTip(inputs(), 42, b));
        EXPECT_EQ(a.mText, b.mText);
        EXPECT_EQ(a.mScreen, b.mScreen);
        std::set<std::string> seen;
        for (std::uint64_t seed = 0; seed < 200; ++seed)
        {
            Tip tip;
            ASSERT_TRUE(pickTip(inputs(), seed, tip));
            seen.insert(tip.mText);
        }
        EXPECT_EQ(seen.size(), 3u); // one, two, five
    }

    TEST_F(SkyrimMenusPickFixture, noCandidateGivesNoTip)
    {
        failing = { 1, 2, 5 };
        Tip tip;
        EXPECT_FALSE(pickTip(inputs(), 7, tip));
        EXPECT_EQ(tip.mCandidates, 0u);
    }

    TEST_F(SkyrimMenusPickFixture, aTextThatResolvesToNothingIsNotACandidate)
    {
        TipInputs in = inputs();
        in.mText = [](const LoadingScreen& s) { return s.mCommon.mId == 1 ? std::string("only") : std::string(); };
        for (std::uint64_t seed = 0; seed < 20; ++seed)
        {
            Tip tip;
            ASSERT_TRUE(pickTip(in, seed, tip));
            EXPECT_EQ(tip.mText, "only");
        }
    }

    TEST_F(SkyrimMenusPickFixture, theConditionsAreNotAskedForAScreenThatCannotBeChosen)
    {
        std::set<FormId> asked;
        TipInputs in = inputs();
        in.mPasses = [&asked](const LoadingScreen& s) {
            asked.insert(s.mCommon.mId);
            return true;
        };
        Tip tip;
        ASSERT_TRUE(pickTip(in, 1, tip));
        EXPECT_EQ(asked, (std::set<FormId>{ 1, 2, 5 })); // not 3 (no text) and not 4 (damaged conditions)
    }

    TEST_F(SkyrimMenusPickFixture, aLongTipIsCutAtACharacterBoundary)
    {
        TipInputs in = inputs();
        std::string long_;
        for (std::size_t i = 0; i < kMaxTipBytes; ++i)
            long_ += 'a';
        long_ += "\xC3\xA9"; // an e acute straddling the limit is not possible here: the limit falls before it
        in.mText = [&long_](const LoadingScreen&) { return long_; };
        Tip tip;
        ASSERT_TRUE(pickTip(in, 3, tip));
        EXPECT_EQ(tip.mText.size(), kMaxTipBytes);

        std::string odd(kMaxTipBytes - 1, 'a');
        odd += "\xC3\xA9"; // the two bytes straddle the limit: both go
        in.mText = [&odd](const LoadingScreen&) { return odd; };
        ASSERT_TRUE(pickTip(in, 3, tip));
        EXPECT_EQ(tip.mText.size(), kMaxTipBytes - 1);
    }

    // ---- the service and its bindings ---------------------------------------------------------------------------

    class FakeBindings final : public openeden::sdk::BindingsService
    {
    public:
        openeden::sdk::DeclareResult declareNamespace(const openeden::sdk::NamespaceDescriptor& d) override
        {
            mName = std::string(d.name);
            mVersion = d.version;
            mMenuSafe = d.menuSafe;
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
        bool mMenuSafe = false;
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

    struct SkyrimMenusServiceFixture : testing::Test
    {
        LoadingScreenIndex mIndex;
        std::unique_ptr<MenusService> mService;
        FakeBindings mBindings;
        bool mReady = true;
        int mAccessCalls = 0;
        std::vector<std::string> mLog;

        void SetUp() override
        {
            const LoadOrder o = order();
            const Content::Header header{};
            LoadingScreenRecords a;
            a.mScreens = { make(1, "alpha"), make(2, "beta") };
            mIndex.add(std::move(a), FormIdResolver(header, "Skyrim.esm", o));
            Hooks hooks;
            hooks.mAccess = [this](Access& out) {
                ++mAccessCalls;
                if (!mReady)
                    return false;
                out.mScreens = &mIndex;
                out.mEvaluate = [](const std::vector<survey::Ctda>&, const FormIdResolver&) { return true; };
                out.mText = [](const LString& s, std::uint32_t, StringTableKind) { return s.mText; };
                return true;
            };
            hooks.mLog = [this](const std::string& line) { mLog.push_back(line); };
            mService = std::make_unique<MenusService>(std::move(hooks));
            ASSERT_TRUE(mService->declare(mBindings).ok);
        }
    };

    TEST_F(SkyrimMenusServiceFixture, theNamespaceIsMenuSafeAndHasItsFunctions)
    {
        EXPECT_EQ(mBindings.mName, "skyrimse.menus");
        EXPECT_EQ(mBindings.mVersion.major, 1);
        EXPECT_EQ(mBindings.mVersion.minor, 0);
        EXPECT_TRUE(mBindings.mMenuSafe);
        for (const char* name : { "ready", "tip_count", "tip" })
            EXPECT_TRUE(mBindings.mFunctions.count(name)) << name;
    }

    TEST_F(SkyrimMenusServiceFixture, nothingIsReadyUntilTheDataIsThereAndATipIsEmptyUntilThen)
    {
        mReady = false;
        EXPECT_FALSE(mBindings.call("ready").value.boolean);
        EXPECT_EQ(mBindings.call("tip_count").value.integer, 0);
        EXPECT_TRUE(mBindings.call("tip", { i32(1) }).value.text.empty());
        mReady = true;
        EXPECT_TRUE(mBindings.call("ready").value.boolean);
        EXPECT_EQ(mBindings.call("tip_count").value.integer, 2);
    }

    TEST_F(SkyrimMenusServiceFixture, aTipIsOneOfTheTextsAndTheSameSeedGivesTheSame)
    {
        std::set<std::string> seen;
        for (int seed = 0; seed < 50; ++seed)
        {
            const std::string a(mBindings.call("tip", { i32(seed) }).value.text);
            const std::string b(mBindings.call("tip", { i32(seed) }).value.text);
            EXPECT_EQ(a, b);
            seen.insert(a);
        }
        EXPECT_EQ(seen, (std::set<std::string>{ "alpha", "beta" }));
    }

    TEST_F(SkyrimMenusServiceFixture, theDataIsAskedForOnlyUntilItIsThereAndTheReadyLineIsLoggedOnce)
    {
        mReady = false;
        (void)mBindings.call("ready");
        (void)mBindings.call("ready");
        EXPECT_EQ(mAccessCalls, 2);
        mReady = true;
        (void)mBindings.call("ready");
        (void)mBindings.call("ready");
        (void)mBindings.call("tip", { i32(1) });
        EXPECT_EQ(mAccessCalls, 3);
        ASSERT_GE(mLog.size(), 1u);
        EXPECT_NE(mLog[0].find("MENUS ready"), std::string::npos);
        EXPECT_NE(mLog[0].find("2"), std::string::npos);
        int readyLines = 0;
        for (const std::string& line : mLog)
            if (line.find("MENUS ready") != std::string::npos)
                ++readyLines;
        EXPECT_EQ(readyLines, 1);
    }

    TEST_F(SkyrimMenusServiceFixture, aNegativeSeedIsAValidSeed)
    {
        EXPECT_FALSE(mBindings.call("tip", { i32(-5) }).value.text.empty());
    }
}
