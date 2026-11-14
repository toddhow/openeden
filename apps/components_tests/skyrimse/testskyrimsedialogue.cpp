// Task 0137, milestone M-S4 slice 1: Skyrim SE dialogue records (DIAL, INFO, GLOB, FLST), the load-order merge of
// them, the conditions world that answers from data, and the choice of a response, on generated plugins. No game data.
#include <game_skyrimse/dialogue.hpp>

#include <gtest/gtest.h>

#include <cstring>
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

    constexpr std::uint32_t kGrupTop = 0;
    constexpr std::uint32_t kGrupTopicChildren = 7;

    // load order: Skyrim.esm = 0, test.esp = 1, other.esp = 2. Ids 0x01xxxxxx are test.esp's own.
    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kRaceNord = 0x00013746;
    constexpr FormId kFaction = kSelf | 0x701;
    constexpr FormId kKeywordNpc = kSelf | 0x601;
    constexpr FormId kKeywordRace = kSelf | 0x602;
    constexpr FormId kVoice = kSelf | 0x801;
    constexpr FormId kVoiceList = kSelf | 0x802;
    constexpr FormId kGlobal = kSelf | 0x901;
    constexpr FormId kList = kSelf | 0xA01;
    constexpr FormId kNpcA = kSelf | 0x200;
    constexpr FormId kNpcB = kSelf | 0x201;
    constexpr FormId kTemplateNpc = kSelf | 0x202;
    constexpr FormId kTopicHello = kSelf | 0x300;
    constexpr FormId kTopicHigh = kSelf | 0x301;
    constexpr FormId kQuest = kSelf | 0x400;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        EXPECT_TRUE(o.add("other.esp", false, err));
        return o;
    }

    Bytes header(const std::vector<Master>& masters)
    {
        return tes4Header(1.7f, 0, "A", "B", 1, masters, 24, 44);
    }

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    Bytes top(const char (&type)[5], const Bytes& records)
    {
        return group4(Content::fourCC(type), kGrupTop, records);
    }

    Bytes ctda(std::uint16_t function, std::uint8_t compare, float value, std::uint32_t p1 = 0, std::uint32_t runOn = 0,
        std::uint32_t reference = 0, std::uint8_t flags = 0, std::uint32_t p2 = 0)
    {
        Bytes b;
        b.u8(static_cast<std::uint8_t>((compare << 5) | flags)).u8(0).u8(0).u8(0).f32(value);
        b.u16(function).u16(0).u32(p1).u32(p2).u32(runOn).u32(reference).u32(0);
        return sub4("CTDA", b);
    }

    Bytes trdt(std::uint8_t number, std::uint32_t emotionType = 0, std::uint32_t emotionValue = 50)
    {
        Bytes b;
        b.u32(emotionType).u32(emotionValue).u32(0).u8(number).u8(0).u8(0).u8(0).u32(0).u8(1).u8(0).u8(0).u8(0);
        return sub4("TRDT", b);
    }

    struct InfoSpec
    {
        std::uint32_t mId = 0;
        std::uint16_t mFlags = 0;
        Bytes mConditions;
        std::vector<std::string> mTexts{ "Hello." };
        std::uint32_t mQuest = 0;
        std::uint32_t mSpeaker = 0;
        bool mDeleted = false;
    };

    Bytes infoRecord(const InfoSpec& s)
    {
        Bytes b;
        b.append(sub4("ENAM", Bytes().u16(s.mFlags).u16(24)));
        b.append(sub4("CNAM", Bytes().u8(0)));
        if (s.mQuest != 0)
            b.append(sub4("QNAM", Bytes().u32(s.mQuest)));
        if (s.mSpeaker != 0)
            b.append(sub4("ANAM", Bytes().u32(s.mSpeaker)));
        std::uint8_t n = 1;
        for (const std::string& t : s.mTexts)
        {
            b.append(trdt(n++));
            b.append(sub4("NAM1", Bytes().zstring(t)));
            b.append(sub4("NAM2", Bytes().u8(0)));
        }
        b.append(s.mConditions);
        return record4("INFO", s.mDeleted ? 0x20u : 0u, s.mId, b);
    }

    Bytes topicRecord(std::uint32_t id, const std::string& name, const std::string& subtype, float priority,
        std::uint32_t quest, std::uint8_t category = 7)
    {
        Bytes b;
        b.append(edid(name));
        b.append(sub4("PNAM", Bytes().f32(priority)));
        b.append(sub4("QNAM", Bytes().u32(quest)));
        b.append(sub4("BNAM", Bytes().u32(kSelf | 0x500)));
        b.append(sub4("DATA", Bytes().u8(0).u8(category).u16(73)));
        b.append(sub4("SNAM", Bytes().text(subtype)));
        b.append(sub4("TIFC", Bytes().u32(1)));
        return record4("DIAL", 0, id, b);
    }

    /// DIAL + its type 7 group holding the infos.
    Bytes topicWithInfos(std::uint32_t id, const std::string& name, const std::string& subtype, float priority,
        std::uint32_t quest, const std::vector<InfoSpec>& infos)
    {
        Bytes children;
        for (const InfoSpec& i : infos)
            children.append(infoRecord(i));
        Bytes b = topicRecord(id, name, subtype, priority, quest);
        b.append(group4(id, kGrupTopicChildren, children));
        return b;
    }

    Bytes acbs(std::uint32_t flags, std::uint16_t templateFlags)
    {
        Bytes b;
        b.u32(flags).u16(0).u16(0).u16(5).u16(1).u16(50).u16(100).u16(0).u16(templateFlags).u16(0).u16(0);
        return b;
    }

    Bytes npc(std::uint32_t id, const std::string& name, std::uint32_t flags, std::uint16_t templateFlags,
        std::uint32_t templ, const std::vector<std::uint32_t>& keywords, std::uint32_t voice, const Bytes& factions)
    {
        Bytes b;
        b.append(edid(name));
        b.append(sub4("ACBS", acbs(flags, templateFlags)));
        b.append(sub4("RNAM", Bytes().u32(kRaceNord)));
        b.append(sub4("CNAM", Bytes().u32(kSelf | 0x300)));
        if (voice != 0)
            b.append(sub4("VTCK", Bytes().u32(voice)));
        if (templ != 0)
            b.append(sub4("TPLT", Bytes().u32(templ)));
        b.append(factions);
        if (!keywords.empty())
        {
            Bytes k;
            for (std::uint32_t w : keywords)
                k.u32(w);
            b.append(sub4("KWDA", k));
        }
        return record4("NPC_", 0, id, b);
    }

    Bytes faction(std::uint32_t id, std::int8_t rank)
    {
        return sub4("SNAM", Bytes().u32(id).u8(static_cast<std::uint8_t>(rank)).u8(0).u8(0).u8(0));
    }

    Bytes raceRecord()
    {
        Bytes b;
        b.append(edid("NordRace"));
        b.append(sub4("VTCK", Bytes().u32(kSelf | 0x803).u32(kSelf | 0x804)));
        b.append(sub4("KWDA", Bytes().u32(kKeywordRace)));
        return record4("RACE", 0, kRaceNord, b);
    }

    Bytes globalRecord(std::uint32_t id, float value)
    {
        Bytes b;
        b.append(edid("Glob"));
        b.append(sub4("FNAM", Bytes().u8('f')));
        b.append(sub4("FLTV", Bytes().f32(value)));
        return record4("GLOB", 0, id, b);
    }

    Bytes listRecord(std::uint32_t id, const std::vector<std::uint32_t>& entries)
    {
        Bytes b;
        b.append(edid("List"));
        for (std::uint32_t e : entries)
            b.append(sub4("LNAM", Bytes().u32(e)));
        return record4("FLST", 0, id, b);
    }

    Bytes pluginBytes(const std::vector<Master>& masters, const Bytes& dials, const Bytes& npcs = Bytes(),
        const Bytes& extras = Bytes())
    {
        Bytes bytes = header(masters);
        if (dials.size() != 0)
            bytes.append(top("DIAL", dials));
        if (npcs.size() != 0)
        {
            bytes.append(top("NPC_", npcs));
            bytes.append(top("RACE", raceRecord()));
        }
        bytes.append(extras);
        return bytes;
    }

    std::vector<Master> masters()
    {
        return { { "Skyrim.esm", 0 } };
    }

    struct Loaded
    {
        std::unique_ptr<Content::Plugin> mPlugin;
        DialogueRecords mDialogue;
        ActorRecords mActors;
        bool mOk = false;
    };

    Loaded load(const Bytes& bytes, const std::string& name = "test.esp", DialogueLimits limits = DialogueLimits())
    {
        Loaded l;
        Content::Error error;
        l.mOk = parseDialogueRecordsBytes(bytes.str(), name, order(), limits, l.mDialogue, error);
        l.mPlugin
            = Content::Plugin::open(std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), name, error);
        Content::Error e2;
        EXPECT_TRUE(parseActorRecordsBytes(bytes.str(), name, order(), ActorLimits(), l.mActors, e2)) << e2.mMessage;
        return l;
    }

    FormIdResolver resolverOf(const Loaded& l, const std::string& name = "test.esp")
    {
        return FormIdResolver(l.mPlugin->header(), name, order());
    }

    // ---- parsing -------------------------------------------------------------------------------------------------

    TEST(SkyrimDialogueParse, aTopicIsReadInFull)
    {
        const Loaded l
            = load(pluginBytes(masters(), topicWithInfos(kTopicHello, "GenericHello", "HELO", 50.0f, kQuest, {})));
        ASSERT_TRUE(l.mOk);
        ASSERT_EQ(l.mDialogue.mTopics.size(), 1u);
        const DialogTopic& t = l.mDialogue.mTopics[0];
        EXPECT_EQ(t.mCommon.mId, kTopicHello);
        EXPECT_EQ(t.mCommon.mEditorId, "GenericHello");
        EXPECT_FLOAT_EQ(t.mPriority, 50.0f);
        EXPECT_EQ(t.mQuest, kQuest);
        EXPECT_EQ(t.mBranch, kSelf | 0x500);
        EXPECT_TRUE(t.mHasData);
        EXPECT_EQ(t.mCategory, TopicCategory::Miscellaneous);
        EXPECT_EQ(t.mSubtype, 73);
        EXPECT_EQ(t.mSubtypeName, "HELO");
        EXPECT_EQ(t.mInfoCount, 1u);
    }

    TEST(SkyrimDialogueParse, anInfoIsReadInFullAndKnowsItsTopicFromTheGroupLabel)
    {
        InfoSpec s;
        s.mId = kSelf | 0x1000;
        s.mFlags = InfoFlags::Random | InfoFlags::SayOnce;
        s.mTexts = { "One.", "Two." };
        s.mQuest = kQuest;
        s.mSpeaker = kNpcA;
        s.mConditions.append(ctda(72, 0, 1.0f, kNpcA));
        const Loaded l = load(pluginBytes(masters(), topicWithInfos(kTopicHello, "H", "HELO", 1.0f, kQuest, { s })));
        ASSERT_TRUE(l.mOk);
        ASSERT_EQ(l.mDialogue.mInfos.size(), 1u);
        const DialogInfo& i = l.mDialogue.mInfos[0];
        EXPECT_EQ(i.mCommon.mId, kSelf | 0x1000);
        EXPECT_EQ(i.mTopic, kTopicHello);
        EXPECT_EQ(i.mFlags, InfoFlags::Random | InfoFlags::SayOnce);
        EXPECT_EQ(i.mResetHours, 24);
        EXPECT_EQ(i.mQuest, kQuest);
        EXPECT_EQ(i.mSpeaker, kNpcA);
        ASSERT_EQ(i.mResponses.size(), 2u);
        EXPECT_EQ(i.mResponses[0].mText.mText, "One.");
        EXPECT_EQ(i.mResponses[1].mText.mText, "Two.");
        EXPECT_EQ(i.mResponses[1].mNumber, 2);
        EXPECT_EQ(i.mResponses[0].mEmotionValue, 50u);
        EXPECT_TRUE(i.mResponses[0].mUseEmotionAnimation);
        ASSERT_EQ(i.mConditions.size(), 1u);
        EXPECT_EQ(i.mConditions[0].mFunction, 72);
        EXPECT_TRUE(i.mConditionsOk);
    }

    TEST(SkyrimDialogueParse, linksAndPromptAreRead)
    {
        Bytes b;
        b.append(sub4("ENAM", Bytes().u16(0).u16(0)));
        b.append(sub4("RNAM", Bytes().zstring("Ask about it")));
        b.append(sub4("TCLT", Bytes().u32(kTopicHello)));
        b.append(sub4("TCLT", Bytes().u32(kTopicHigh)));
        Bytes children = record4("INFO", 0, kSelf | 0x1001, b);
        Bytes dials = topicRecord(kTopicHello, "T", "CUST", 1.0f, kQuest);
        dials.append(group4(kTopicHello, kGrupTopicChildren, children));
        const Loaded l = load(pluginBytes(masters(), dials));
        ASSERT_EQ(l.mDialogue.mInfos.size(), 1u);
        EXPECT_EQ(l.mDialogue.mInfos[0].mPrompt.mText, "Ask about it");
        EXPECT_EQ(l.mDialogue.mInfos[0].mLinksTo, (std::vector<FormId>{ kTopicHello, kTopicHigh }));
    }

    TEST(SkyrimDialogueParse, aLocalisedTextIsAnId)
    {
        Bytes bytes = tes4Header(1.7f, 0x80, "A", "B", 1, masters(), 24, 44); // localised
        InfoSpec s;
        s.mId = kSelf | 0x1000;
        Bytes info;
        info.append(sub4("ENAM", Bytes().u16(0).u16(0)));
        info.append(trdt(1));
        info.append(sub4("NAM1", Bytes().u32(0x1234)));
        Bytes dials = topicRecord(kTopicHello, "T", "HELO", 1.0f, kQuest);
        dials.append(group4(kTopicHello, kGrupTopicChildren, record4("INFO", 0, s.mId, info)));
        bytes.append(top("DIAL", dials));
        const Loaded l = load(bytes);
        ASSERT_EQ(l.mDialogue.mInfos.size(), 1u);
        const LString& t = l.mDialogue.mInfos[0].mResponses.at(0).mText;
        EXPECT_TRUE(t.mLocalized);
        EXPECT_EQ(t.mId, 0x1234u);
        EXPECT_TRUE(t.mText.empty());
    }

    TEST(SkyrimDialogueParse, globalsAndFormListsAreRead)
    {
        Bytes extras = top("GLOB", globalRecord(kGlobal, 3.5f));
        extras.append(top("FLST", listRecord(kList, { kVoice, kVoiceList })));
        const Loaded l = load(pluginBytes(masters(), Bytes(), Bytes(), extras));
        ASSERT_TRUE(l.mOk);
        ASSERT_EQ(l.mDialogue.mGlobals.size(), 1u);
        EXPECT_FLOAT_EQ(l.mDialogue.mGlobals[0].mValue, 3.5f);
        EXPECT_EQ(l.mDialogue.mGlobals[0].mType, 'f');
        ASSERT_EQ(l.mDialogue.mFormLists.size(), 1u);
        EXPECT_EQ(l.mDialogue.mFormLists[0].mEntries, (std::vector<FormId>{ kVoice, kVoiceList }));
    }

    TEST(SkyrimDialogueParse, anInfoOutsideATopicGroupIsCounted)
    {
        InfoSpec s;
        s.mId = kSelf | 0x1000;
        const Loaded l = load(pluginBytes(masters(), infoRecord(s)));
        EXPECT_EQ(l.mDialogue.mInfosOutsideTopicGroup, 1u);
        ASSERT_EQ(l.mDialogue.mInfos.size(), 1u);
        EXPECT_EQ(l.mDialogue.mInfos[0].mTopic, kNullFormId);
    }

    TEST(SkyrimDialogueParse, aWrongSizedSubrecordIsOddNotGuessed)
    {
        Bytes b;
        b.append(sub4("ENAM", Bytes().u16(1))); // 2 bytes, not 4
        b.append(sub4("TRDT", Bytes().u32(1))); // 4 bytes, not 24
        b.append(sub4("CNAM", Bytes().u16(0)));
        Bytes dials = topicRecord(kTopicHello, "T", "HELO", 1.0f, kQuest);
        dials.append(group4(kTopicHello, kGrupTopicChildren, record4("INFO", 0, kSelf | 0x1000, b)));
        const Loaded l = load(pluginBytes(masters(), dials));
        ASSERT_EQ(l.mDialogue.mInfos.size(), 1u);
        EXPECT_EQ(l.mDialogue.mInfos[0].mFlags, 0);
        EXPECT_TRUE(l.mDialogue.mInfos[0].mResponses.empty());
        EXPECT_EQ(l.mDialogue.mOddSubrecords, 3u);
        EXPECT_EQ(l.mDialogue.mOddBy.at("INFO.ENAM"), 1u);
    }

    TEST(SkyrimDialogueParse, aTornSubrecordFailsOnlyThatRecord)
    {
        Bytes bad;
        bad.fourCC("EDID").u16(200).text("short");
        InfoSpec ok;
        ok.mId = kSelf | 0x1001;
        Bytes children = record4("INFO", 0, kSelf | 0x1000, bad);
        children.append(infoRecord(ok));
        Bytes dials = topicRecord(kTopicHello, "T", "HELO", 1.0f, kQuest);
        dials.append(group4(kTopicHello, kGrupTopicChildren, children));
        const Loaded l = load(pluginBytes(masters(), dials));
        EXPECT_EQ(l.mDialogue.mCounts.at(Content::fourCC("INFO")).mFailed, 1u);
        ASSERT_EQ(l.mDialogue.mInfos.size(), 1u);
        EXPECT_EQ(l.mDialogue.mInfos[0].mCommon.mId, kSelf | 0x1001);
        EXPECT_EQ(l.mDialogue.mErrors.size(), 1u);
    }

    TEST(SkyrimDialogueParse, tooManyResponsesFailTheRecord)
    {
        InfoSpec s;
        s.mId = kSelf | 0x1000;
        s.mTexts.assign(5, "x");
        DialogueLimits limits;
        limits.mMaxResponses = 4;
        const Loaded l = load(
            pluginBytes(masters(), topicWithInfos(kTopicHello, "H", "HELO", 1.0f, kQuest, { s })), "test.esp", limits);
        EXPECT_TRUE(l.mDialogue.mInfos.empty());
        EXPECT_EQ(l.mDialogue.mCounts.at(Content::fourCC("INFO")).mFailed, 1u);
    }

    TEST(SkyrimDialogueParse, aDamagedConditionListIsMarked)
    {
        InfoSpec s;
        s.mId = kSelf | 0x1000;
        s.mConditions.append(sub4("CTDA", Bytes().u32(1))); // not 32 bytes
        const Loaded l = load(pluginBytes(masters(), topicWithInfos(kTopicHello, "H", "HELO", 1.0f, kQuest, { s })));
        ASSERT_EQ(l.mDialogue.mInfos.size(), 1u);
        EXPECT_FALSE(l.mDialogue.mInfos[0].mConditionsOk);
    }

    TEST(SkyrimDialogueParse, aPluginForAnotherGameIsRefused)
    {
        Bytes bytes = tes4Header(0.94f, 0, "A", "B", 0, masters(), 24, 44);
        Content::Error error;
        DialogueRecords out;
        EXPECT_FALSE(parseDialogueRecordsBytes(bytes.str(), "old.esp", order(), DialogueLimits(), out, error));
    }

    // ---- the load-order merge ------------------------------------------------------------------------------------

    DialogueIndex indexOf(const Loaded& l)
    {
        DialogueIndex idx;
        idx.add(l.mDialogue, resolverOf(l));
        return idx;
    }

    InfoSpec info(std::uint32_t local, const std::string& text, std::uint16_t flags = 0)
    {
        InfoSpec s;
        s.mId = kSelf | local;
        s.mTexts = { text };
        s.mFlags = flags;
        return s;
    }

    TEST(SkyrimDialogueIndex, infosKeepTheirFileOrder)
    {
        const Loaded l = load(pluginBytes(masters(),
            topicWithInfos(
                kTopicHello, "H", "HELO", 1.0f, kQuest, { info(0x10, "a"), info(0x11, "b"), info(0x12, "c") })));
        const DialogueIndex idx = indexOf(l);
        const auto infos = idx.infosOf(kTopicHello);
        ASSERT_EQ(infos.size(), 3u);
        EXPECT_EQ(infos[0]->mCommon.mId, kSelf | 0x10);
        EXPECT_EQ(infos[1]->mCommon.mId, kSelf | 0x11);
        EXPECT_EQ(infos[2]->mCommon.mId, kSelf | 0x12);
        EXPECT_EQ(idx.infoCount(), 3u);
        EXPECT_TRUE(idx.infosOf(0x01009999).empty());
    }

    TEST(SkyrimDialogueIndex, aLaterPluginOverridesInPlaceDeletesAndAppends)
    {
        const Loaded base = load(pluginBytes(masters(),
            topicWithInfos(
                kTopicHello, "H", "HELO", 1.0f, kQuest, { info(0x10, "a"), info(0x11, "b"), info(0x12, "c") })));
        // other.esp (index 2) lists test.esp as a master: ids 0x01xxxxxx are test.esp's, 0x02xxxxxx its own.
        InfoSpec over = info(0x11, "B2");
        over.mId = 0x01000011; // master index 1 = test.esp
        InfoSpec del = info(0x12, "x");
        del.mDeleted = true;
        InfoSpec fresh;
        fresh.mId = 0x02000020;
        fresh.mTexts = { "new" };
        Bytes dials = topicRecord(kTopicHello, "H", "HELO", 9.0f, kQuest);
        Bytes children = infoRecord(over);
        children.append(infoRecord(del));
        children.append(infoRecord(fresh));
        dials.append(group4(kTopicHello, kGrupTopicChildren, children));
        const Loaded later = load(pluginBytes({ { "Skyrim.esm", 0 }, { "test.esp", 0 } }, dials), "other.esp");
        ASSERT_TRUE(later.mOk);

        DialogueIndex idx;
        idx.add(base.mDialogue, resolverOf(base));
        idx.add(later.mDialogue, resolverOf(later, "other.esp"));
        const auto infos = idx.infosOf(kTopicHello);
        ASSERT_EQ(infos.size(), 3u);
        EXPECT_EQ(infos[0]->mResponses[0].mText.mText, "a");
        EXPECT_EQ(infos[1]->mResponses[0].mText.mText, "B2");
        EXPECT_EQ(infos[2]->mCommon.mId, 0x02000020u);
        EXPECT_FLOAT_EQ(idx.topic(kTopicHello)->mPriority, 9.0f); // the later topic record wins
        EXPECT_EQ(idx.infoCount(), 3u);
        EXPECT_EQ(idx.resolverOf(*infos[1]).resolve(0x01000011), std::optional<FormId>(0x01000011));
    }

    TEST(SkyrimDialogueIndex, topicsListedBySubtypeAreSorted)
    {
        Bytes dials = topicRecord(kTopicHigh, "A", "HELO", 1.0f, kQuest);
        dials.append(topicRecord(kTopicHello, "B", "HELO", 2.0f, kQuest));
        dials.append(topicRecord(kSelf | 0x302, "C", "GBYE", 2.0f, kQuest));
        const DialogueIndex idx = indexOf(load(pluginBytes(masters(), dials)));
        EXPECT_EQ(idx.topicIds("HELO"), (std::vector<FormId>{ kTopicHello, kTopicHigh }));
        EXPECT_EQ(idx.topicIds().size(), 3u);
        EXPECT_EQ(idx.topicCount(), 3u);
    }

    // ---- the conditions world ------------------------------------------------------------------------------------

    struct World
    {
        Loaded mLoaded;
        ActorIndex mActors;
        DialogueIndex mDialogue;
        FormIdResolver mResolver;
        std::unique_ptr<SkyrimConditionWorld> mWorld;

        explicit World(const Bytes& bytes)
            : mLoaded(load(bytes))
            , mResolver(resolverOf(mLoaded))
        {
            mActors.add(mLoaded.mActors);
            mDialogue.add(mLoaded.mDialogue, resolverOf(mLoaded));
            mWorld = std::make_unique<SkyrimConditionWorld>(mActors, mDialogue);
            mWorld->setPlugin(&mResolver);
        }

        bool test(const Bytes& condition, const DialogueSpeaker& speaker)
        {
            mWorld->setSpeaker(speaker);
            std::vector<survey::Ctda> list;
            const std::string raw = condition.str();
            EXPECT_TRUE(
                conditions::decodeConditions({ reinterpret_cast<const std::byte*>(raw.data()), raw.size() }, list));
            EXPECT_EQ(list.size(), 1u);
            conditions::Evaluator eval;
            return eval.evaluate(list, *mWorld);
        }
    };

    Bytes worldPlugin()
    {
        Bytes npcs;
        Bytes fac = faction(kFaction, 4);
        npcs.append(npc(kNpcA, "Alice", NpcFlags::Female, 0, 0, { kKeywordNpc }, kVoice, fac));
        npcs.append(npc(kNpcB, "Bob", 0, 0, 0, {}, 0, Bytes()));
        // a templated NPC: takes traits (race, voice, sex) and factions from kNpcA
        npcs.append(
            npc(kTemplateNpc, "Templated", 0, TemplateFlags::Traits | TemplateFlags::Factions, kNpcA, {}, 0, Bytes()));
        Bytes extras = top("GLOB", globalRecord(kGlobal, 3.5f));
        extras.append(top("FLST", listRecord(kList, { kNpcA, kNpcB })));
        extras.append(top("FLST", listRecord(kVoiceList, { kVoice })));
        return pluginBytes(masters(), Bytes(), npcs, extras);
    }

    DialogueSpeaker speaker(FormId base)
    {
        DialogueSpeaker s;
        s.mBase = base;
        return s;
    }

    TEST(SkyrimConditionWorld, getIsIdMatchesTheBaseAndTheReference)
    {
        World w(worldPlugin());
        EXPECT_TRUE(w.test(ctda(72, 0, 1.0f, kNpcA), speaker(kNpcA)));
        EXPECT_FALSE(w.test(ctda(72, 0, 1.0f, kNpcB), speaker(kNpcA)));
        DialogueSpeaker s = speaker(kNpcA);
        s.mReference = kSelf | 0x5000;
        EXPECT_TRUE(w.test(ctda(72, 0, 1.0f, kSelf | 0x5000), s));
        EXPECT_TRUE(w.test(ctda(136, 0, 1.0f, kSelf | 0x5000), s)); // GetIsReference
        EXPECT_FALSE(w.test(ctda(136, 0, 1.0f, kSelf | 0x5001), s));
        EXPECT_FALSE(w.test(ctda(136, 0, 1.0f, kSelf | 0x5000), speaker(kNpcA))); // no reference given
    }

    TEST(SkyrimConditionWorld, theTargetIsThePlayerAndOnlyHisIdIsKnown)
    {
        World w(worldPlugin());
        EXPECT_TRUE(w.test(ctda(72, 0, 1.0f, 0x00000007, 1), speaker(kNpcA)));
        EXPECT_FALSE(w.test(ctda(72, 0, 1.0f, kNpcA, 1), speaker(kNpcA)));
        EXPECT_FALSE(w.test(ctda(69, 0, 1.0f, kRaceNord, 1), speaker(kNpcA))); // his race is not known
    }

    TEST(SkyrimConditionWorld, raceSexAndFactionComeFromTheRecordsThroughTemplates)
    {
        World w(worldPlugin());
        EXPECT_TRUE(w.test(ctda(69, 0, 1.0f, kRaceNord), speaker(kNpcA)));
        EXPECT_FALSE(w.test(ctda(69, 0, 1.0f, kSelf | 0x77), speaker(kNpcA)));
        EXPECT_TRUE(w.test(ctda(70, 0, 1.0f, 1), speaker(kNpcA))); // female
        EXPECT_FALSE(w.test(ctda(70, 0, 1.0f, 1), speaker(kNpcB)));
        EXPECT_TRUE(w.test(ctda(71, 0, 1.0f, kFaction), speaker(kNpcA)));
        EXPECT_FALSE(w.test(ctda(71, 0, 1.0f, kFaction), speaker(kNpcB)));
        EXPECT_TRUE(w.test(ctda(73, 0, 4.0f, kFaction), speaker(kNpcA))); // rank 4
        EXPECT_TRUE(w.test(ctda(73, 0, -1.0f, kFaction), speaker(kNpcB))); // not a member: -1
        // the templated NPC takes sex and factions from kNpcA through its template flags
        EXPECT_TRUE(w.test(ctda(70, 0, 1.0f, 1), speaker(kTemplateNpc)));
        EXPECT_TRUE(w.test(ctda(71, 0, 1.0f, kFaction), speaker(kTemplateNpc)));
    }

    TEST(SkyrimConditionWorld, voiceTypeMatchesDirectlyByListAndFallsBackToTheRace)
    {
        World w(worldPlugin());
        EXPECT_TRUE(w.test(ctda(426, 0, 1.0f, kVoice), speaker(kNpcA)));
        EXPECT_TRUE(w.test(ctda(426, 0, 1.0f, kVoiceList), speaker(kNpcA)));
        EXPECT_FALSE(w.test(ctda(426, 0, 1.0f, kSelf | 0x899), speaker(kNpcA)));
        // kNpcB has no voice of its own: its race's male voice (0x803) counts
        EXPECT_TRUE(w.test(ctda(426, 0, 1.0f, kSelf | 0x803), speaker(kNpcB)));
        EXPECT_FALSE(w.test(ctda(426, 0, 1.0f, kSelf | 0x804), speaker(kNpcB)));
    }

    TEST(SkyrimConditionWorld, keywordsComeFromTheNpcAndItsRace)
    {
        World w(worldPlugin());
        EXPECT_TRUE(w.test(ctda(560, 0, 1.0f, kKeywordNpc), speaker(kNpcA)));
        EXPECT_TRUE(w.test(ctda(560, 0, 1.0f, kKeywordRace), speaker(kNpcA)));
        EXPECT_FALSE(w.test(ctda(560, 0, 1.0f, kKeywordNpc), speaker(kNpcB)));
        EXPECT_TRUE(w.test(ctda(560, 0, 1.0f, kKeywordRace), speaker(kNpcB)));
    }

    TEST(SkyrimConditionWorld, globalsAndTheUseGlobalFlagComeFromGlob)
    {
        World w(worldPlugin());
        EXPECT_TRUE(w.test(ctda(74, 0, 3.5f, kGlobal), speaker(kNpcA)));
        EXPECT_TRUE(w.test(ctda(74, 2, 3.0f, kGlobal), speaker(kNpcA)));
        EXPECT_TRUE(w.test(ctda(74, 0, 0.0f, kSelf | 0x998), speaker(kNpcA))); // unknown global: 0
        // compare against another global's value: the comparison field holds the global's id
        Bytes c;
        c.u8(0x04).u8(0).u8(0).u8(0).u32(kGlobal).u16(74).u16(0).u32(kGlobal).u32(0).u32(0).u32(0).u32(0);
        EXPECT_TRUE(w.test(sub4("CTDA", c), speaker(kNpcA))); // value == value
    }

    TEST(SkyrimConditionWorld, isInListFollowsAFormListIncludingNestedOnes)
    {
        World w(worldPlugin());
        EXPECT_TRUE(w.test(ctda(372, 0, 1.0f, kList), speaker(kNpcA)));
        EXPECT_TRUE(w.test(ctda(372, 0, 1.0f, kList), speaker(kNpcB)));
        EXPECT_FALSE(w.test(ctda(372, 0, 1.0f, kList), speaker(kTemplateNpc)));
        EXPECT_FALSE(w.test(ctda(372, 0, 1.0f, kSelf | 0x998), speaker(kNpcA)));
    }

    TEST(SkyrimConditionWorld, cellWorldspaceAndWeatherAnswerOnlyWhenGiven)
    {
        World w(worldPlugin());
        DialogueSpeaker s = speaker(kNpcA);
        EXPECT_FALSE(w.test(ctda(67, 0, 1.0f, kSelf | 0x6000), s));
        s.mCell = kSelf | 0x6000;
        s.mWorldspace = kSelf | 0x6100;
        EXPECT_TRUE(w.test(ctda(67, 0, 1.0f, kSelf | 0x6000), s));
        EXPECT_FALSE(w.test(ctda(67, 0, 1.0f, kSelf | 0x6001), s));
        EXPECT_TRUE(w.test(ctda(310, 0, 1.0f, kSelf | 0x6100), s));
        EXPECT_FALSE(w.test(ctda(149, 0, 1.0f, kSelf | 0x6200), s));
        w.mWorld->setCurrentWeather(kSelf | 0x6200);
        EXPECT_TRUE(w.test(ctda(149, 0, 1.0f, kSelf | 0x6200), s));
    }

    TEST(SkyrimConditionWorld, idsAreResolvedThroughThePluginTheConditionCameFrom)
    {
        World w(worldPlugin());
        // 0x00013746 is Skyrim.esm's (master index 0): load-order id 0x00013746
        EXPECT_TRUE(w.test(ctda(69, 0, 1.0f, 0x00013746), speaker(kNpcA)));
        EXPECT_FALSE(w.test(ctda(69, 0, 1.0f, 0x01013746), speaker(kNpcA))); // that is test.esp's own 0x013746
        // another plugin's resolver: stored 0x01000200 there means a different load-order id
        const FormIdResolver other(w.mLoaded.mPlugin->header(), "other.esp", order());
        w.mWorld->setPlugin(&other);
        EXPECT_FALSE(w.test(ctda(72, 0, 1.0f, 0x01000200),
            speaker(kNpcA))); // there index 1 is other.esp itself, load-order id 0x02000200
    }

    TEST(SkyrimConditionWorld, answersTellsWhatTheDataCanSettle)
    {
        World w(worldPlugin());
        w.mWorld->setSpeaker(speaker(kNpcA));
        survey::Ctda c;
        c.mFunction = 72;
        EXPECT_TRUE(w.mWorld->answers(c));
        c.mFunction = 58; // GetStage
        EXPECT_FALSE(w.mWorld->answers(c));
        c.mFunction = 74;
        c.mRunOn = 1;
        EXPECT_TRUE(w.mWorld->answers(c)); // a global has no subject
        c.mFunction = 69;
        EXPECT_FALSE(w.mWorld->answers(c)); // the player's race
        c.mFunction = 72;
        EXPECT_TRUE(w.mWorld->answers(c)); // the player's id
        c.mRunOn = 3;
        EXPECT_FALSE(w.mWorld->answers(c));
        c = survey::Ctda();
        c.mFunction = 67;
        EXPECT_FALSE(w.mWorld->answers(c)); // no cell given
        c.mFunction = 149;
        EXPECT_FALSE(w.mWorld->answers(c));
    }

    // ---- choosing a response -------------------------------------------------------------------------------------

    struct Pick
    {
        Loaded mLoaded;
        ActorIndex mActors;
        DialogueIndex mDialogue;
        std::unique_ptr<SkyrimConditionWorld> mWorld;

        explicit Pick(const Bytes& dials)
        {
            Bytes npcs = npc(kNpcA, "Alice", NpcFlags::Female, 0, 0, {}, kVoice, faction(kFaction, 1));
            npcs.append(npc(kNpcB, "Bob", 0, 0, 0, {}, 0, Bytes()));
            mLoaded = load(pluginBytes(masters(), dials, npcs));
            mActors.add(mLoaded.mActors);
            mDialogue.add(mLoaded.mDialogue, resolverOf(mLoaded));
            mWorld = std::make_unique<SkyrimConditionWorld>(mActors, mDialogue);
        }

        Selection select(FormId topic, FormId speakerBase, const SelectionOptions& o = SelectionOptions())
        {
            return selectInfo(mDialogue, topic, *mWorld, speaker(speakerBase), o);
        }
    };

    InfoSpec withCond(std::uint32_t local, const std::string& text, const Bytes& cond, std::uint16_t flags = 0)
    {
        InfoSpec s = info(local, text, flags);
        s.mConditions = cond;
        return s;
    }

    std::string textOf(const Selection& s)
    {
        return s.mInfo == nullptr ? std::string("<none>") : s.mInfo->mResponses.at(0).mText.mText;
    }

    TEST(SkyrimDialogueSelect, theFirstInfoWhoseConditionsPassIsPicked)
    {
        Pick p(topicWithInfos(kTopicHello, "H", "HELO", 1.0f, kQuest,
            { withCond(0x10, "for Bob", ctda(72, 0, 1.0f, kNpcB)),
                withCond(0x11, "for Alice", ctda(72, 0, 1.0f, kNpcA)), info(0x12, "for anyone") }));
        EXPECT_EQ(textOf(p.select(kTopicHello, kNpcA)), "for Alice");
        EXPECT_EQ(textOf(p.select(kTopicHello, kNpcB)), "for Bob");
        const Selection s = p.select(kTopicHello, kSelf | 0x299);
        EXPECT_EQ(textOf(s), "for anyone");
        EXPECT_EQ(s.mConsidered, 3u);
        EXPECT_EQ(s.mPassing, 1u); // the walk stops at the first one that passes
    }

    TEST(SkyrimDialogueSelect, nothingPassingGivesNothing)
    {
        Pick p(topicWithInfos(
            kTopicHello, "H", "HELO", 1.0f, kQuest, { withCond(0x10, "for Bob", ctda(72, 0, 1.0f, kNpcB)) }));
        const Selection s = p.select(kTopicHello, kNpcA);
        EXPECT_EQ(s.mInfo, nullptr);
        EXPECT_EQ(s.mPassing, 0u);
        EXPECT_EQ(p.select(0x01009999, kNpcA).mInfo, nullptr); // an unknown topic
    }

    TEST(SkyrimDialogueSelect, anUnknownFunctionFailsTheWholeInfoClosed)
    {
        Bytes cond = ctda(72, 0, 1.0f, kNpcA);
        cond.append(ctda(9999, 0, 1.0f)); // not a function this engine knows
        Pick p(topicWithInfos(
            kTopicHello, "H", "HELO", 1.0f, kQuest, { withCond(0x10, "closed", cond), info(0x11, "open") }));
        conditions::Evaluator eval;
        const Selection s = selectInfo(p.mDialogue, kTopicHello, *p.mWorld, speaker(kNpcA), SelectionOptions(), &eval);
        EXPECT_EQ(textOf(s), "open");
        EXPECT_EQ(eval.mListsFailedClosed, 1u);
        EXPECT_EQ(s.mUncertainBefore, 1u); // the closed one holds a condition the data cannot answer
    }

    TEST(SkyrimDialogueSelect, aDamagedConditionListNeverPasses)
    {
        InfoSpec bad = info(0x10, "bad");
        bad.mConditions.append(sub4("CTDA", Bytes().u32(1)));
        Pick p(topicWithInfos(kTopicHello, "H", "HELO", 1.0f, kQuest, { bad, info(0x11, "good") }));
        EXPECT_EQ(textOf(p.select(kTopicHello, kNpcA)), "good");
    }

    TEST(SkyrimDialogueSelect, aQuestThatIsNotRunningSkipsItsInfos)
    {
        InfoSpec a = info(0x10, "quest A");
        a.mQuest = kSelf | 0x401;
        Pick p(topicWithInfos(kTopicHello, "H", "HELO", 1.0f, kQuest, { a, info(0x11, "topic quest") }));
        SelectionOptions o;
        o.mQuestRunning = [](FormId q) { return q != (kSelf | 0x401); };
        EXPECT_EQ(textOf(p.select(kTopicHello, kNpcA, o)), "topic quest");
        EXPECT_EQ(textOf(p.select(kTopicHello, kNpcA)), "quest A"); // default: every quest runs
        o.mQuestRunning = [](FormId q) { return q != kQuest; };
        EXPECT_EQ(textOf(p.select(kTopicHello, kNpcA, o)), "quest A"); // the topic's quest does not run
    }

    TEST(SkyrimDialogueSelect, sayOnceInfosAreSkippedWhenSaid)
    {
        Pick p(topicWithInfos(kTopicHello, "H", "HELO", 1.0f, kQuest,
            { info(0x10, "first time", InfoFlags::SayOnce), info(0x11, "again") }));
        SelectionOptions o;
        o.mSaid = [](FormId id) { return id == (kSelf | 0x10); };
        EXPECT_EQ(textOf(p.select(kTopicHello, kNpcA, o)), "again");
        EXPECT_EQ(textOf(p.select(kTopicHello, kNpcA)), "first time");
    }

    TEST(SkyrimDialogueSelect, anInfoNamingAnotherSpeakerIsSkipped)
    {
        InfoSpec s = info(0x10, "only Bob");
        s.mSpeaker = kNpcB;
        Pick p(topicWithInfos(kTopicHello, "H", "HELO", 1.0f, kQuest, { s, info(0x11, "anyone") }));
        EXPECT_EQ(textOf(p.select(kTopicHello, kNpcA)), "anyone");
        EXPECT_EQ(textOf(p.select(kTopicHello, kNpcB)), "only Bob");
    }

    TEST(SkyrimDialogueSelect, randomInfosShareThePick)
    {
        Pick p(topicWithInfos(kTopicHello, "H", "HELO", 1.0f, kQuest,
            { info(0x10, "r0", InfoFlags::Random), info(0x11, "r1", InfoFlags::Random),
                info(0x12, "r2", InfoFlags::Random), info(0x13, "fixed") }));
        SelectionOptions o;
        std::set<std::string> seen;
        for (std::uint32_t k = 0; k < 6; ++k)
        {
            o.mPick = k;
            seen.insert(textOf(p.select(kTopicHello, kNpcA, o)));
        }
        EXPECT_EQ(seen, (std::set<std::string>{ "r0", "r1", "r2" }));
        o.mPick = 4;
        EXPECT_EQ(textOf(p.select(kTopicHello, kNpcA, o)), "r1");
    }

    TEST(SkyrimDialogueSelect, aNonRandomInfoAheadOfTheRandomOnesWins)
    {
        Pick p(topicWithInfos(
            kTopicHello, "H", "HELO", 1.0f, kQuest, { info(0x10, "fixed"), info(0x11, "r1", InfoFlags::Random) }));
        SelectionOptions o;
        o.mPick = 1;
        EXPECT_EQ(textOf(p.select(kTopicHello, kNpcA, o)), "fixed");
    }

    TEST(SkyrimDialogueSelect, theTopicWithTheHighestPriorityGivingAnInfoWins)
    {
        Bytes dials = topicWithInfos(kTopicHello, "Low", "HELO", 10.0f, kQuest, { info(0x10, "low") });
        dials.append(topicWithInfos(
            kTopicHigh, "High", "HELO", 90.0f, kQuest, { withCond(0x20, "high for Bob", ctda(72, 0, 1.0f, kNpcB)) }));
        dials.append(topicWithInfos(kSelf | 0x302, "Bye", "GBYE", 99.0f, kQuest, { info(0x30, "bye") }));
        Pick p(dials);
        const Selection forBob = pickFromTopics(p.mDialogue, "HELO", *p.mWorld, speaker(kNpcB), SelectionOptions());
        EXPECT_EQ(textOf(forBob), "high for Bob");
        EXPECT_EQ(forBob.mTopic->mCommon.mId, kTopicHigh);
        const Selection forAlice = pickFromTopics(p.mDialogue, "HELO", *p.mWorld, speaker(kNpcA), SelectionOptions());
        EXPECT_EQ(textOf(forAlice), "low");
        EXPECT_EQ(forAlice.mTopic->mCommon.mId, kTopicHello);
        EXPECT_EQ(pickFromTopics(p.mDialogue, "NOPE", *p.mWorld, speaker(kNpcA), SelectionOptions()).mInfo, nullptr);
    }

    TEST(SkyrimDialogueSelect, unansweredConditionsAreCounted)
    {
        Bytes cond = ctda(72, 0, 1.0f, kNpcA); // answered
        cond.append(ctda(58, 0, 10.0f, kQuest)); // GetStage: not answered
        cond.append(ctda(59, 0, 1.0f, kQuest, 0, 0, 0x01)); // GetStageDone: not answered
        Pick p(topicWithInfos(kTopicHello, "H", "HELO", 1.0f, kQuest, { withCond(0x10, "x", cond) }));
        p.mWorld->setSpeaker(speaker(kNpcA));
        EXPECT_EQ(unansweredConditions(*p.mDialogue.infosOf(kTopicHello)[0], *p.mWorld), 2u);
    }
}
