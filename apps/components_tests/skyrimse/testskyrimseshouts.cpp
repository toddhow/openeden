// Task 0205 (milestone M-S7): the pure half of the shouts (game_skyrimse/shouts.hpp, shouts_service.hpp): the SHOU and
// WOOP decoders, the index, the hold rule, the player's shout state, its save chunk and the service. No game data.
// Every number is derived by hand; the hold rule (word n at (n - 1) x fShoutTime2, a minimum of fShoutTime1) is OURS
// and
// pinned here.
#include <game_skyrimse/shouts.hpp>
#include <game_skyrimse/shouts_service.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>
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

    constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
    constexpr FormId kFus = 0x13E08, kRo = 0x13E09, kDah = 0x13E0A, kShout = 0x13E07, kOther = 0x20000;

    // ---- the hold rule ---------------------------------------------------------------------------------------------

    TEST(SkyrimShoutsHold, aHoldShorterThanTheMinimumReachesNothing)
    {
        const Settings s;
        EXPECT_EQ(wordsReached(0.f, 3, s), 0u);
        EXPECT_EQ(wordsReached(0.19f, 3, s), 0u);
    }

    TEST(SkyrimShoutsHold, eachFurtherWordTakesTheWordTime)
    {
        const Settings s; // 0.2 and 0.9
        EXPECT_EQ(wordsReached(0.2f, 3, s), 1u);
        EXPECT_EQ(wordsReached(0.89f, 3, s), 1u);
        EXPECT_EQ(wordsReached(0.91f, 3, s), 2u);
        EXPECT_EQ(wordsReached(1.79f, 3, s), 2u);
        EXPECT_EQ(wordsReached(1.81f, 3, s), 3u);
        EXPECT_EQ(wordsReached(60.f, 3, s), 3u);
    }

    TEST(SkyrimShoutsHold, theAvailableWordsCapTheCount)
    {
        const Settings s;
        EXPECT_EQ(wordsReached(60.f, 2, s), 2u);
        EXPECT_EQ(wordsReached(60.f, 1, s), 1u);
        EXPECT_EQ(wordsReached(60.f, 0, s), 0u);
        EXPECT_EQ(wordsReached(60.f, 9, s), kShoutWordCount);
    }

    TEST(SkyrimShoutsHold, anUnusableHoldOrSettingReachesNothing)
    {
        const Settings s;
        EXPECT_EQ(wordsReached(kNan, 3, s), 0u);
        EXPECT_EQ(wordsReached(-1.f, 3, s), 0u);
        Settings bad;
        bad.mWordTime = 0.f;
        EXPECT_EQ(wordsReached(5.f, 3, bad), 1u); // no time per word: one word, never a division by zero
        bad.mWordTime = kNan;
        EXPECT_EQ(wordsReached(5.f, 3, bad), 1u);
    }

    TEST(SkyrimShoutsHold, theTimeToAWordIsItsNumberLessOneTimesTheWordTime)
    {
        const Settings s;
        EXPECT_FLOAT_EQ(timeToWord(1, s), 0.f);
        EXPECT_FLOAT_EQ(timeToWord(2, s), 0.9f);
        EXPECT_FLOAT_EQ(timeToWord(3, s), 1.8f);
        EXPECT_FLOAT_EQ(timeToWord(0, s), 0.f);
    }

    // ---- the records -----------------------------------------------------------------------------------------------

    struct Fixture
    {
        LoadOrder mOrder = [] {
            LoadOrder order;
            std::string err;
            EXPECT_TRUE(order.add("Skyrim.esm", false, err));
            EXPECT_TRUE(order.add("test.esp", false, err));
            return order;
        }();
        std::unique_ptr<Content::Plugin> mPlugin = [] {
            Content::Error error;
            return Content::Plugin::open(
                std::make_unique<std::istringstream>(
                    tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44).str(), std::ios::binary),
                "test.esp", error);
        }();
        ExteriorLimits mLimits;
        DecodeNotes mNotes;
        FormIdResolver mResolver{ mPlugin->header(), "test.esp", mOrder };
        DecodeContext context() { return DecodeContext{ mResolver, mLimits, mNotes, false }; }
    };

    std::span<const std::byte> view(const Bytes& b)
    {
        return { reinterpret_cast<const std::byte*>(b.str().data()), b.str().size() };
    }

    Content::RawRecord raw(const char (&type)[5], std::uint32_t id)
    {
        Content::RawRecord r;
        r.mType = Content::fourCC(type);
        r.mFormId = id;
        r.mOffset = 0x1234;
        return r;
    }

    Bytes snam(std::uint32_t word, std::uint32_t spell, float recovery)
    {
        return sub4("SNAM", Bytes().u32(word).u32(spell).f32(recovery));
    }

    TEST(SkyrimShoutRecords, aShoutKeepsItsThreeWordsSpellsAndRecoveryTimes)
    {
        Fixture f;
        std::string why;
        Bytes shou;
        shou.append(sub4("EDID", Bytes().zstring("UnrelentingForceShout")));
        shou.append(sub4("FULL", Bytes().u32(0x1234)));
        shou.append(sub4("MDOB", Bytes().u32(0x00054321)));
        shou.append(sub4("DESC", Bytes().u32(0x1235)));
        shou.append(snam(0x00013E08, 0x00013E09, 15.f));
        shou.append(snam(0x00013E0A, 0x00013E0B, 20.f));
        shou.append(snam(0x00013E0C, 0x00013E0D, 45.f));
        ShoutRecord s;
        ASSERT_TRUE(decodeShout(view(shou), raw("SHOU", 0x00013E07), f.context(), s, why)) << why;
        EXPECT_EQ(s.mCommon.mEditorId, "UnrelentingForceShout");
        EXPECT_TRUE(s.mHasData);
        EXPECT_EQ(s.mWordCount, 3u);
        EXPECT_EQ(s.mCommon.mId, *f.mResolver.resolve(0x00013E07));
        EXPECT_EQ(s.mMenuObject, *f.mResolver.resolve(0x00054321));
        EXPECT_EQ(s.mWords[0].mWord, *f.mResolver.resolve(0x00013E08));
        EXPECT_EQ(s.mWords[0].mSpell, *f.mResolver.resolve(0x00013E09));
        EXPECT_FLOAT_EQ(s.mWords[0].mRecovery, 15.f);
        EXPECT_EQ(s.mWords[1].mSpell, *f.mResolver.resolve(0x00013E0B));
        EXPECT_FLOAT_EQ(s.mWords[1].mRecovery, 20.f);
        EXPECT_EQ(s.mWords[2].mWord, *f.mResolver.resolve(0x00013E0C));
        EXPECT_FLOAT_EQ(s.mWords[2].mRecovery, 45.f);
    }

    TEST(SkyrimShoutRecords, aShoutWithFewerWordsKeepsThemAndAWrongSizedSnamIsCountedOdd)
    {
        Fixture f;
        std::string why;
        Bytes shou;
        shou.append(sub4("EDID", Bytes().zstring("Half")));
        shou.append(snam(0x00013E08, 0x00013E09, 7.f));
        shou.append(sub4("SNAM", Bytes().u32(1).u32(2))); // 8 bytes: odd
        ShoutRecord s;
        ASSERT_TRUE(decodeShout(view(shou), raw("SHOU", 0x00013E30), f.context(), s, why)) << why;
        EXPECT_EQ(s.mWordCount, 1u);
        EXPECT_GE(f.mNotes.mOddSubrecords, 1u);
    }

    TEST(SkyrimShoutRecords, aFourthWordOrABadRecoveryRefusesTheRecord)
    {
        Fixture f;
        std::string why;
        Bytes four;
        four.append(sub4("EDID", Bytes().zstring("Four")));
        for (int i = 0; i < 4; ++i)
            four.append(snam(0x00013E08, 0x00013E09, 1.f));
        ShoutRecord s;
        EXPECT_FALSE(decodeShout(view(four), raw("SHOU", 0x00013E31), f.context(), s, why));
        EXPECT_NE(why.find("SNAM"), std::string::npos);

        for (const float bad : { kNan, -1.f })
        {
            Bytes shou;
            shou.append(sub4("EDID", Bytes().zstring("Bad")));
            shou.append(snam(0x00013E08, 0x00013E09, bad));
            why.clear();
            EXPECT_FALSE(decodeShout(view(shou), raw("SHOU", 0x00013E32), f.context(), s, why));
            EXPECT_NE(why.find("SNAM"), std::string::npos);
        }
    }

    TEST(SkyrimShoutRecords, aWordKeepsItsEditorIdAndNames)
    {
        Fixture f;
        std::string why;
        Bytes woop;
        woop.append(sub4("EDID", Bytes().zstring("WordFus")));
        woop.append(sub4("FULL", Bytes().u32(0x10)));
        woop.append(sub4("TNAM", Bytes().u32(0x11)));
        WordRecord w;
        ASSERT_TRUE(decodeWord(view(woop), raw("WOOP", 0x00013E08), f.context(), w, why)) << why;
        EXPECT_EQ(w.mCommon.mEditorId, "WordFus");
        EXPECT_EQ(w.mCommon.mId, *f.mResolver.resolve(0x00013E08));
    }

    // ---- the index -------------------------------------------------------------------------------------------------

    ShoutRecord makeShout(FormId id, const char* name, FormId w1, FormId w2, FormId w3)
    {
        ShoutRecord s;
        s.mCommon.mId = id;
        s.mCommon.mEditorId = name;
        s.mHasData = true;
        s.mWordCount = 3;
        s.mWords[0] = { w1, 0x100 + w1, 15.f };
        s.mWords[1] = { w2, 0x100 + w2, 20.f };
        s.mWords[2] = { w3, 0x100 + w3, 45.f };
        return s;
    }

    WordRecord makeWord(FormId id, const char* name)
    {
        WordRecord w;
        w.mCommon.mId = id;
        w.mCommon.mEditorId = name;
        return w;
    }

    TEST(SkyrimShoutIndex, itFindsByIdAndByEditorIdWithoutCase)
    {
        ShoutIndex index;
        index.add({ makeShout(kShout, "UnrelentingForceShout", kFus, kRo, kDah) },
            { makeWord(kFus, "WordFus"), makeWord(kRo, "WordRo") });
        ASSERT_NE(index.shout(kShout), nullptr);
        EXPECT_EQ(index.shoutByEditorId("unrelentingforceshout"), index.shout(kShout));
        EXPECT_EQ(index.shout(kOther), nullptr);
        ASSERT_NE(index.word(kRo), nullptr);
        EXPECT_EQ(index.wordByEditorId("WORDFUS"), index.word(kFus));
        EXPECT_EQ(index.wordByEditorId("WordNone"), nullptr);
        EXPECT_EQ(index.shoutCount(), 1u);
        EXPECT_EQ(index.wordCount(), 2u);
    }

    TEST(SkyrimShoutIndex, aLaterPluginReplacesTheWholeRecordAndADeletedOneRemovesIt)
    {
        ShoutIndex index;
        index.add({ makeShout(kShout, "Old", kFus, kRo, kDah) }, {});
        index.add({ makeShout(kShout, "New", kFus, kRo, kDah) }, {});
        EXPECT_EQ(index.shoutCount(), 1u);
        EXPECT_EQ(index.shout(kShout)->mCommon.mEditorId, "New");
        ShoutRecord gone = makeShout(kShout, "New", kFus, kRo, kDah);
        gone.mCommon.mFlags = RecordFlags::Deleted;
        index.add({ gone }, {});
        EXPECT_EQ(index.shout(kShout), nullptr);
    }

    TEST(SkyrimShoutIndex, theShoutOfAWordIsTheOneWithTheLowestIdThatHasIt)
    {
        ShoutIndex index;
        index.add({ makeShout(0x500, "B", kFus, kRo, kDah), makeShout(0x400, "A", kFus, 0x9001, 0x9002) }, {});
        ASSERT_NE(index.shoutOfWord(kFus), nullptr);
        EXPECT_EQ(index.shoutOfWord(kFus)->mCommon.mId, 0x400u);
        EXPECT_EQ(index.shoutOfWord(kDah)->mCommon.mId, 0x500u);
        EXPECT_EQ(index.shoutOfWord(kOther), nullptr);
    }

    // ---- the state -------------------------------------------------------------------------------------------------

    TEST(SkyrimShoutState, learningAWordTwiceIsNewOnlyTheFirstTime)
    {
        ShoutState s;
        EXPECT_TRUE(s.learn(kFus));
        EXPECT_FALSE(s.learn(kFus));
        EXPECT_TRUE(s.learned(kFus));
        EXPECT_FALSE(s.learned(kRo));
        EXPECT_EQ(s.learnedCount(), 1u);
        EXPECT_FALSE(s.learn(kNullFormId));
    }

    TEST(SkyrimShoutState, unlockingNeedsALearnedWordAndASoulAndSpendsOne)
    {
        ShoutState s;
        EXPECT_FALSE(s.unlock(kFus)); // not learned
        s.learn(kFus);
        EXPECT_FALSE(s.unlock(kFus)); // no soul
        s.addSouls(2);
        EXPECT_TRUE(s.unlock(kFus));
        EXPECT_EQ(s.souls(), 1u);
        EXPECT_TRUE(s.unlocked(kFus));
        EXPECT_FALSE(s.unlock(kFus)); // already, and no soul is spent
        EXPECT_EQ(s.souls(), 1u);
    }

    TEST(SkyrimShoutState, aScriptsUnlockLearnsAndSpendsNoSoul)
    {
        ShoutState s;
        EXPECT_TRUE(s.forceUnlock(kRo));
        EXPECT_TRUE(s.learned(kRo));
        EXPECT_TRUE(s.unlocked(kRo));
        EXPECT_EQ(s.souls(), 0u);
        EXPECT_FALSE(s.forceUnlock(kRo));
    }

    TEST(SkyrimShoutState, theSoulsAreCapped)
    {
        ShoutState s;
        s.addSouls(kMaxSouls + 50);
        EXPECT_EQ(s.souls(), kMaxSouls);
        s.addSouls(5);
        EXPECT_EQ(s.souls(), kMaxSouls);
    }

    TEST(SkyrimShoutState, aShoutsLevelIsTheLeadingRunOfUnlockedWords)
    {
        const ShoutRecord shout = makeShout(kShout, "UF", kFus, kRo, kDah);
        ShoutState s;
        EXPECT_EQ(s.availableWords(shout), 0u);
        s.forceUnlock(kRo); // the second word alone is not a level
        EXPECT_EQ(s.availableWords(shout), 0u);
        s.forceUnlock(kFus);
        EXPECT_EQ(s.availableWords(shout), 2u);
        s.forceUnlock(kDah);
        EXPECT_EQ(s.availableWords(shout), 3u);
    }

    TEST(SkyrimShoutState, theCooldownCountsDownAndEndsAtZero)
    {
        ShoutState s;
        EXPECT_TRUE(s.ready());
        s.startCooldown(15.f);
        EXPECT_FALSE(s.ready());
        EXPECT_FLOAT_EQ(s.cooldown(), 15.f);
        EXPECT_FLOAT_EQ(s.cooldownTotal(), 15.f);
        s.tick(5.f);
        EXPECT_FLOAT_EQ(s.cooldown(), 10.f);
        EXPECT_FLOAT_EQ(s.cooldownTotal(), 15.f);
        s.tick(20.f);
        EXPECT_FLOAT_EQ(s.cooldown(), 0.f);
        EXPECT_TRUE(s.ready());
    }

    TEST(SkyrimShoutState, anUnusableCooldownOrTickChangesNothing)
    {
        ShoutState s;
        s.startCooldown(kNan);
        s.startCooldown(0.f);
        s.startCooldown(-3.f);
        EXPECT_TRUE(s.ready());
        s.startCooldown(10.f);
        s.tick(kNan);
        s.tick(-5.f);
        EXPECT_FLOAT_EQ(s.cooldown(), 10.f);
    }

    TEST(SkyrimShoutState, theRevisionMovesOnlyWhenSomethingChanged)
    {
        ShoutState s;
        const std::uint64_t r0 = s.revision();
        s.learn(kFus);
        const std::uint64_t r1 = s.revision();
        EXPECT_GT(r1, r0);
        s.learn(kFus);
        EXPECT_EQ(s.revision(), r1);
        s.equip(kShout);
        EXPECT_GT(s.revision(), r1);
        EXPECT_EQ(s.equipped(), kShout);
    }

    // ---- the chunk -------------------------------------------------------------------------------------------------

    class MemoryWriter final : public openeden::sdk::ChunkWriter
    {
    public:
        void write(const void* data, std::size_t size) override
        {
            const auto* bytes = static_cast<const unsigned char*>(data);
            mBytes.insert(mBytes.end(), bytes, bytes + size);
        }
        std::vector<unsigned char> mBytes;
    };

    class MemoryReader final : public openeden::sdk::ChunkReader
    {
    public:
        explicit MemoryReader(const std::vector<unsigned char>& bytes)
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

    std::vector<unsigned char> handBuilt(std::uint32_t nLearned, const std::vector<std::uint32_t>& learned,
        const std::vector<std::uint32_t>& unlocked, std::uint32_t souls, std::uint32_t equipped, float cooldown,
        float total)
    {
        std::vector<unsigned char> out;
        const auto put = [&](const void* p, std::size_t n) {
            const auto* b = static_cast<const unsigned char*>(p);
            out.insert(out.end(), b, b + n);
        };
        put(&nLearned, 4);
        for (const std::uint32_t id : learned)
            put(&id, 4);
        const std::uint32_t nUnlocked = static_cast<std::uint32_t>(unlocked.size());
        put(&nUnlocked, 4);
        for (const std::uint32_t id : unlocked)
            put(&id, 4);
        put(&souls, 4);
        put(&equipped, 4);
        put(&cooldown, 4);
        put(&total, 4);
        return out;
    }

    TEST(SkyrimShoutChunk, theBytesAreTheDocumentedLayout)
    {
        ShoutState s;
        s.learn(9);
        s.learn(5); // out of order on purpose: the bytes are ascending
        s.forceUnlock(5);
        s.addSouls(3);
        s.equip(7);
        s.startCooldown(15.f);
        s.tick(12.5f);
        MemoryWriter w;
        ShoutChunk chunk(s);
        chunk.write(w);
        EXPECT_EQ(w.mBytes, handBuilt(2, { 5, 9 }, { 5 }, 3, 7, 2.5f, 15.f));
        EXPECT_EQ(w.mBytes.size(), 36u);
        EXPECT_EQ(chunk.chunkName(), "shouts");
        EXPECT_EQ(chunk.chunkVersion(), 1u);
    }

    TEST(SkyrimShoutChunk, aStateWrittenAndReadBackIsTheSameState)
    {
        ShoutState a;
        a.learn(kFus);
        a.learn(kRo);
        a.forceUnlock(kFus);
        a.addSouls(4);
        a.equip(kShout);
        a.startCooldown(20.f);
        a.tick(1.f);
        MemoryWriter w;
        ShoutChunk(a).write(w);
        ShoutState b;
        MemoryReader reader(w.mBytes);
        ShoutChunk chunk(b);
        ASSERT_TRUE(chunk.read(reader, 1));
        EXPECT_TRUE(b.learned(kRo));
        EXPECT_TRUE(b.unlocked(kFus));
        EXPECT_FALSE(b.unlocked(kRo));
        EXPECT_EQ(b.souls(), 4u);
        EXPECT_EQ(b.equipped(), kShout);
        EXPECT_FLOAT_EQ(b.cooldown(), 19.f);
        EXPECT_FLOAT_EQ(b.cooldownTotal(), 20.f);
        MemoryWriter again;
        ShoutChunk(b).write(again);
        EXPECT_EQ(again.mBytes, w.mBytes);
    }

    TEST(SkyrimShoutChunk, aReadReplacesWhatWasThere)
    {
        ShoutState a;
        a.learn(kRo);
        MemoryWriter w;
        ShoutChunk(a).write(w);
        ShoutState b;
        b.learn(kFus);
        b.addSouls(9);
        MemoryReader reader(w.mBytes);
        ShoutChunk chunk(b);
        ASSERT_TRUE(chunk.read(reader, 1));
        EXPECT_FALSE(b.learned(kFus));
        EXPECT_EQ(b.souls(), 0u);
    }

    TEST(SkyrimShoutChunk, aChunkThatDoesNotFitIsRefusedAndChangesNothing)
    {
        const std::vector<std::vector<unsigned char>> bad = {
            handBuilt(2, { 9, 5 }, {}, 0, 0, 0.f, 0.f), // ids not ascending
            handBuilt(2, { 5, 5 }, {}, 0, 0, 0.f, 0.f), // a repeated id
            handBuilt(1, { 5 }, { 6 }, 0, 0, 0.f, 0.f), // unlocked but not learned
            handBuilt(0, {}, {}, 0, 0, kNan, 0.f), // cooldown not finite
            handBuilt(0, {}, {}, 0, 0, -1.f, 1.f), // negative cooldown
            handBuilt(0, {}, {}, 0, 0, 5.f, 1.f), // more left than the total
            handBuilt(0, {}, {}, kMaxSouls + 1, 0, 0.f, 0.f), // souls over the cap
            handBuilt(0x7FFFFFFF, {}, {}, 0, 0, 0.f, 0.f), // a count the size cannot hold
        };
        for (const auto& bytes : bad)
        {
            ShoutState s;
            s.learn(kFus);
            std::string reason;
            ShoutChunk chunk(s);
            chunk.setRefusalSink([&](const std::string& text) { reason = text; });
            MemoryReader reader(bytes);
            EXPECT_FALSE(chunk.read(reader, 1));
            EXPECT_FALSE(reason.empty());
            EXPECT_TRUE(s.learned(kFus)); // untouched
            EXPECT_EQ(s.learnedCount(), 1u);
        }
    }

    TEST(SkyrimShoutChunk, aTrailingByteOrAnotherVersionIsRefused)
    {
        std::vector<unsigned char> bytes = handBuilt(0, {}, {}, 1, 0, 0.f, 0.f);
        ShoutState s;
        ShoutChunk chunk(s);
        {
            MemoryReader reader(bytes);
            EXPECT_TRUE(chunk.read(reader, 1));
            EXPECT_EQ(s.souls(), 1u);
        }
        bytes.push_back(0);
        {
            MemoryReader reader(bytes);
            EXPECT_FALSE(chunk.read(reader, 1));
        }
        bytes.pop_back();
        {
            MemoryReader reader(bytes);
            EXPECT_FALSE(chunk.read(reader, 2));
        }
    }

    // ---- names -----------------------------------------------------------------------------------------------------

    TEST(SkyrimShoutNames, theEditorIdBecomesAReadableName)
    {
        EXPECT_EQ(prettyShoutName("UnrelentingForceShout"), "Unrelenting Force");
        EXPECT_EQ(prettyShoutName("AuraWhisperShout"), "Aura Whisper");
        EXPECT_EQ(prettyShoutName("FireBreath2"), "Fire Breath");
        EXPECT_EQ(prettyShoutName("Shout"), "Shout");
        EXPECT_EQ(prettyShoutName(""), "");
    }

    // ---- the service -----------------------------------------------------------------------------------------------

    struct ServiceFixture
    {
        ShoutIndex mIndex;
        bool mReady = false;
        std::vector<std::string> mLog;
        std::unique_ptr<ShoutService> mService;
        ServiceFixture()
        {
            mIndex.add({ makeShout(kShout, "UnrelentingForceShout", kFus, kRo, kDah) },
                { makeWord(kFus, "WordFus"), makeWord(kRo, "WordRo"), makeWord(kDah, "WordDah") });
            Hooks hooks;
            hooks.mIndex = [this]() -> const ShoutIndex* { return mReady ? &mIndex : nullptr; };
            hooks.mLog = [this](const std::string& line) { mLog.push_back(line); };
            mService = std::make_unique<ShoutService>(std::move(hooks));
        }
    };

    TEST(SkyrimShoutService, theTestSettingsAreAppliedOnceWhenTheRecordsAreThere)
    {
        ServiceFixture f;
        TestSettings t;
        t.mLearn = "WordFus, wordro";
        t.mUnlock = "WordFus";
        t.mSouls = 2;
        t.mShout = "unrelentingforceshout";
        f.mService->setTestSettings(t);
        EXPECT_FALSE(f.mService->poll()); // no records yet
        EXPECT_EQ(f.mService->state().learnedCount(), 0u);
        f.mReady = true;
        EXPECT_TRUE(f.mService->poll());
        const ShoutState& s = f.mService->state();
        EXPECT_TRUE(s.learned(kFus));
        EXPECT_TRUE(s.learned(kRo));
        EXPECT_TRUE(s.unlocked(kFus));
        EXPECT_FALSE(s.unlocked(kRo));
        EXPECT_EQ(s.souls(), 1u); // two given, one spent
        EXPECT_EQ(s.equipped(), kShout);
        EXPECT_EQ(f.mService->equippedName(), "Unrelenting Force");
        EXPECT_EQ(f.mService->equippedWords(), 1u);
        f.mService->state().addSouls(5);
        EXPECT_TRUE(f.mService->poll()); // not applied a second time
        EXPECT_EQ(f.mService->state().souls(), 6u);
    }

    TEST(SkyrimShoutService, anUnlockWithNoSoulsGrantsTheSoulItNeeds)
    {
        ServiceFixture f;
        TestSettings t;
        t.mUnlock = "WordFus,WordRo";
        f.mService->setTestSettings(t);
        f.mReady = true;
        ASSERT_TRUE(f.mService->poll());
        EXPECT_TRUE(f.mService->state().unlocked(kFus));
        EXPECT_TRUE(f.mService->state().unlocked(kRo));
        EXPECT_EQ(f.mService->state().souls(), 0u);
    }

    TEST(SkyrimShoutService, anUnknownNameInTheSettingsIsLoggedAndSkipped)
    {
        ServiceFixture f;
        TestSettings t;
        t.mLearn = "WordNope,WordFus";
        t.mShout = "NoSuchShout";
        f.mService->setTestSettings(t);
        f.mReady = true;
        ASSERT_TRUE(f.mService->poll());
        EXPECT_TRUE(f.mService->state().learned(kFus));
        EXPECT_EQ(f.mService->state().learnedCount(), 1u);
        EXPECT_EQ(f.mService->state().equipped(), kNullFormId);
        bool sawWord = false, sawShout = false;
        for (const std::string& line : f.mLog)
        {
            sawWord = sawWord || line.find("WordNope") != std::string::npos;
            sawShout = sawShout || line.find("NoSuchShout") != std::string::npos;
        }
        EXPECT_TRUE(sawWord);
        EXPECT_TRUE(sawShout);
    }

    TEST(SkyrimShoutService, theNativesTeachUnlockAndAnswerForKnownWordsOnly)
    {
        ServiceFixture f;
        f.mReady = true;
        EXPECT_FALSE(f.mService->isWordUnlocked(kFus));
        EXPECT_TRUE(f.mService->teachWord(kFus));
        EXPECT_TRUE(f.mService->state().learned(kFus));
        EXPECT_FALSE(f.mService->isWordUnlocked(kFus));
        EXPECT_TRUE(f.mService->unlockWord(kFus));
        EXPECT_TRUE(f.mService->isWordUnlocked(kFus));
        EXPECT_EQ(f.mService->state().souls(), 0u);
        EXPECT_FALSE(f.mService->teachWord(kOther)); // not a word of the data
        EXPECT_FALSE(f.mService->unlockWord(kOther));
        EXPECT_FALSE(f.mService->state().learned(kOther));
    }

    TEST(SkyrimShoutService, theNativesDoNothingBeforeTheRecordsAreRead)
    {
        ServiceFixture f;
        EXPECT_FALSE(f.mService->teachWord(kFus));
        EXPECT_FALSE(f.mService->unlockWord(kFus));
        EXPECT_FALSE(f.mService->isWordUnlocked(kFus));
        EXPECT_EQ(f.mService->state().learnedCount(), 0u);
    }
}
