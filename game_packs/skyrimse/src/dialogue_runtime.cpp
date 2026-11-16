#include "game_skyrimse/dialogue_runtime.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "game_skyrimse/services.hpp"
#include "game_skyrimse/speech.hpp"
#include "game_skyrimse/voice.hpp"

namespace game_skyrimse
{
    namespace
    {
        std::string hex8(std::uint32_t id)
        {
            char text[16];
            std::snprintf(text, sizeof(text), "%08X", id);
            return text;
        }
    }

    DialogueRuntime::DialogueRuntime(const ActorIndex& actors, const DialogueIndex& dialogue, const QuestIndex& quests,
        QuestState state, DialogueTextResolver resolver, DialogueRuntimeLimits limits)
        : mActors(actors)
        , mDialogue(dialogue)
        , mQuestIndex(quests)
        , mState(std::move(state))
        , mWorld(actors, dialogue)
        , mResolver(std::move(resolver))
        , mLimits(limits)
    {
        mWorld.setQuests(&mQuestIndex, &mState);
        mWorld.setPlayer(&mPlayer);
    }

    SelectionOptions DialogueRuntime::options(std::uint32_t pick)
    {
        SelectionOptions o;
        o.mQuestRunning = [this](FormId quest) { return !mState.exists(quest) || mState.running(quest); };
        o.mSaid = [this](FormId info) { return mSaid.count(info) != 0; };
        o.mQuests = &mQuestIndex;
        o.mPick = pick;
        return o;
    }

    std::string DialogueRuntime::expandText(const std::string& text, FormId quest) const
    {
        if (text.find('<') == std::string::npos)
            return text;
        journal::TagHooks hooks;
        if (mTagSources.mAliasName)
            hooks.mAlias = [this, quest](std::string_view alias) { return mTagSources.mAliasName(quest, alias); };
        if (mTagSources.mAliasFemale)
            hooks.mAliasFemale = [this, quest](std::string_view alias) { return mTagSources.mAliasFemale(quest, alias); };
        hooks.mGlobal = [this](std::string_view name) -> std::optional<float> {
            const GlobalValue* global = mDialogue.globalByEditorId(name);
            if (global == nullptr)
                return std::nullopt;
            return global->mValue;
        };
        hooks.mBaseName = [this]() -> std::string {
            const ActorNpc* npc = mActors.npc(mSpeaker.mBase);
            if (npc == nullptr || !npc->mName.mPresent)
                return {};
            return npc->mName.mLocalized ? mResolver(npc->mName, npc->mPlugin, StringTableKind::Strings)
                                         : npc->mName.mText;
        };
        hooks.mBribeCost = [this]() -> std::optional<float> {
            const std::uint16_t level = mWorld.speakerLevel();
            if (level == 0)
                return std::nullopt;
            return kBribeGoldPerLevel * static_cast<float>(level);
        };
        hooks.mCrimeGold = [this]() -> std::optional<float> {
            if (mTagSources.mCrimeGold)
                return mTagSources.mCrimeGold();
            // Task 0258: the crime rules' bounty with the speaker's crime faction
            return mCrimeSource.mCrimeGold ? mCrimeSource.mCrimeGold(mSpeaker.mBase, kNullFormId) : 0.f;
        };
        return journal::expandTags(text, hooks, std::string_view());
    }

    void DialogueRuntime::nameVoice(const DialogInfo& info, openeden::sdk::game::DialogueTurn& out)
    {
        const DialogTopic* topic = mDialogue.topic(info.mTopic);
        // The file name is made from the topic's quest (the INFO's own QNAM is not used: SETTLED BY DATA, task 0143).
        const FormId questId = topic != nullptr && topic->mQuest != kNullFormId ? topic->mQuest : info.mQuest;
        const QuestRecord* quest = mQuestIndex.quest(questId);
        const std::string stem = voiceStem(quest != nullptr ? quest->mCommon.mEditorId : std::string(),
            topic != nullptr ? topic->mCommon.mEditorId : std::string());
        // Two places to look, in this order (SETTLED BY DATA, task 0143: of 407 responses whose INFO a later plugin
        // overrides, all 407 have their file in the folder of the plugin that made the INFO, under the id that plugin
        // stores it with; 12 other overrides are found under the overriding plugin): the plugin that made the INFO,
        // then the plugin whose record is used.
        std::string candidatePlugin[2];
        std::uint32_t candidateId[2] = { 0, 0 };
        std::size_t candidates = 0;
        if (const std::optional<DialogueIndex::VoiceOrigin> origin = mDialogue.voiceOriginOf(info))
        {
            candidatePlugin[candidates] = origin->mPlugin;
            candidateId[candidates++] = origin->mStoredId;
        }
        const std::string& winner = mDialogue.pluginName(info.mPlugin);
        if (candidates == 0 || candidatePlugin[0] != winner || candidateId[0] != info.mCommon.mStoredId)
        {
            candidatePlugin[candidates] = winner;
            candidateId[candidates++] = info.mCommon.mStoredId;
        }
        for (const DialogResponse& response : info.mResponses)
        {
            bool named = false;
            bool found = false;
            for (std::size_t c = 0; c < candidates && !found; ++c)
            {
                const std::string path
                    = voicePath(candidatePlugin[c], mVoiceType, stem, candidateId[c], response.mNumber);
                if (path.empty())
                    continue;
                named = true;
                if (mVoiceProbe(path))
                {
                    found = true;
                    out.voice.push_back(path);
                }
            }
            if (!named)
                ++mStats.mVoiceUnnamed;
            else if (found)
                ++mStats.mVoiceFound;
            else
                ++mStats.mVoiceMissing;
        }
    }

    void DialogueRuntime::fillTurn(const DialogInfo& info, openeden::sdk::game::DialogueTurn& out)
    {
        out.text.clear();
        out.voice.clear();
        const DialogTopic* ownTopic = mDialogue.topic(info.mTopic);
        const FormId topicQuest = ownTopic != nullptr && ownTopic->mQuest != kNullFormId ? ownTopic->mQuest : info.mQuest;
        if (mVoiceProbe)
            nameVoice(info, out);
        for (const DialogResponse& response : info.mResponses)
        {
            std::string line = response.mText.mPresent && !response.mText.mLocalized
                ? response.mText.mText
                : mResolver(response.mText, info.mPlugin, StringTableKind::IlStrings);
            if (line.empty())
                continue;
            line = expandText(line, topicQuest);
            if (line.empty())
                continue;
            if (!out.text.empty())
                out.text += ' ';
            out.text += line;
            if (out.text.size() > mLimits.mMaxTextBytes)
            {
                out.text.resize(mLimits.mMaxTextBytes);
                break;
            }
        }
        out.endsConversation = (info.mFlags & InfoFlags::Goodbye) != 0;
        out.hadUnrunEffects = info.mHasScript;
        // Task 0187: the fragments of the INFO go to the script host (begin, then end).
        if (mFragmentSink && info.mVmad != nullptr && info.mVmad->mHasInfoFragments
            && (info.mVmad->mInfo.hasBegin() || info.mVmad->mInfo.hasEnd()))
        {
            InfoFragmentCall call;
            call.mInfo = info.mCommon.mId;
            call.mVmad = info.mVmad;
            call.mSpeakerReference = mSpeaker.mReference;
            call.mSpeakerBase = mSpeaker.mBase;
            ++mStats.mFragmentCalls;
            mFragmentSink(call);
            out.hadUnrunEffects = false;
        }
        ++mStats.mResponses;
        if (info.mHasScript && out.hadUnrunEffects)
            ++mStats.mScriptsNotRun;
        if (!info.mLinksTo.empty())
            ++mStats.mLinksIgnored;
        if ((info.mFlags & InfoFlags::SayOnce) != 0)
            mSaid.insert(info.mCommon.mId);
        // Task 0155: a response whose fragment script opens a menu we do not have is named, and the conversation ends
        // (the menu would have replaced the dialogue). Cached per script: reading a .pex is not free.
        out.service.clear();
        if (mScriptProbe && !info.mFragmentScript.empty())
        {
            auto it = mServiceOfScript.find(info.mFragmentScript);
            if (it == mServiceOfScript.end())
            {
                const std::vector<std::string> names = mScriptProbe(info.mFragmentScript);
                std::string service = classifyService(names);
                if (service.empty() && mOpenServices)
                    service = services::classifyCarry(names); // task 0219: a follower's inventory line
                it = mServiceOfScript.emplace(info.mFragmentScript, std::move(service)).first;
            }
            if (!it->second.empty())
            {
                const std::string opened = mOpenServices ? services::openedServiceTag(it->second) : std::string();
                out.service = opened.empty() ? it->second : opened;
                out.endsConversation = true;
                ++mStats.mServices;
            }
        }
    }

    void DialogueRuntime::offerTopics(openeden::sdk::game::DialogueTurn& out)
    {
        out.topics.clear();
        mOffered.clear();
        const SelectionOptions opts = options(0);
        for (FormId id : mDialogue.topicIds())
        {
            const DialogTopic* topic = mDialogue.topic(id);
            if (topic == nullptr || topic->mCategory != TopicCategory::Topic || !topic->mName.mPresent)
                continue;
            const Selection s = selectInfo(mDialogue, id, mWorld, mSpeaker, opts);
            if (s.mInfo == nullptr)
                continue;
            // The prompt of the response (RNAM) is what the player reads when it is set, else the topic's name.
            std::string text;
            if (s.mInfo->mPrompt.mPresent)
                text = mResolver(s.mInfo->mPrompt, s.mInfo->mPlugin, StringTableKind::Strings);
            if (text.empty())
                text = topic->mName.mLocalized ? mResolver(topic->mName, topic->mPlugin, StringTableKind::Strings)
                                               : topic->mName.mText;
            text = expandText(text, topic->mQuest != kNullFormId ? topic->mQuest : s.mInfo->mQuest);
            if (text.empty())
                continue;
            if (out.topics.size() >= mLimits.mMaxTopics)
            {
                ++mStats.mTopicsCut;
                continue;
            }
            if (s.mUncertainBefore != 0)
                ++mStats.mUncertainTopics;
            out.topics.push_back({ id, std::move(text) });
            mOffered.insert(id);
        }
    }

    bool DialogueRuntime::begin(
        const DialogueSpeaker& speaker, openeden::sdk::game::DialogueTurn& out, std::string& error)
    {
        out = {};
        mOpen = false;
        mLinkMode = false; // task 0258
        mSpeaker = speaker;
        mWorld.setSpeaker(speaker);
        // Task 0156: the speaker is in dialogue with the player from the greeting on, has not talked to him yet, and
        // any random number comes from a generator seeded by the placement (the same actor greets the same way).
        mWorld.setConversationOpen(true);
        mWorld.setTalkedToPlayer(false);
        mWorld.setRandomSeed(speaker.mReference != kNullFormId ? speaker.mReference : speaker.mBase);
        mVoiceType = mDialogue.voiceTypeName(mWorld.speakerVoiceType());
        // The random pick is a function of the placement, so the same actor greets the same way in a session.
        const Selection greeting = pickFromTopics(mDialogue, "HELO", mWorld, mSpeaker,
            options(speaker.mReference != kNullFormId ? speaker.mReference : speaker.mBase));
        if (greeting.mInfo == nullptr)
        {
            error = "no greeting passes for this speaker";
            return false;
        }
        fillTurn(*greeting.mInfo, out);
        // The speaker's own name (FULL of the NPC_); empty when the record has none, then the caller uses its own.
        if (const ActorNpc* npc = mActors.npc(speaker.mBase); npc != nullptr && npc->mName.mPresent)
            out.speakerName = npc->mName.mLocalized ? mResolver(npc->mName, npc->mPlugin, StringTableKind::Strings)
                                                    : npc->mName.mText;
        ++mStats.mConversations;
        mWorld.setTalkedToPlayer(true);
        mOpen = true;
        offerTopics(out);
        return true;
    }

    bool DialogueRuntime::sceneLine(
        FormId topic, const DialogueSpeaker& speaker, std::uint32_t pick, openeden::sdk::game::DialogueTurn& out)
    {
        out = {};
        const DialogueSpeaker savedSpeaker = mSpeaker;
        const std::string savedVoice = mVoiceType;
        mSpeaker = speaker;
        mWorld.setSpeaker(speaker);
        mVoiceType = mDialogue.voiceTypeName(mWorld.speakerVoiceType());
        const Selection s = selectInfo(mDialogue, topic, mWorld, mSpeaker, options(pick));
        const bool found = s.mInfo != nullptr;
        if (found)
            fillTurn(*s.mInfo, out);
        mSpeaker = savedSpeaker;
        mVoiceType = savedVoice;
        mWorld.setSpeaker(savedSpeaker);
        return found;
    }

    bool DialogueRuntime::hasResponseText(const DialogInfo& info) const
    {
        for (const DialogResponse& response : info.mResponses)
        {
            const std::string line = response.mText.mPresent && !response.mText.mLocalized
                ? response.mText.mText
                : mResolver(response.mText, info.mPlugin, StringTableKind::IlStrings);
            if (!line.empty())
                return true;
        }
        return false;
    }

    bool DialogueRuntime::hasVoiceFile(const DialogInfo& info)
    {
        if (!mVoiceProbe)
            return true;
        const Stats saved = mStats;
        openeden::sdk::game::DialogueTurn scratch;
        nameVoice(info, scratch);
        mStats = saved;
        return !scratch.voice.empty();
    }

    const DialogInfo* DialogueRuntime::selectBarkInfo(std::string_view subtype, std::uint32_t pick, std::string* why)
    {
        // The rules of selectInfo (dialogue.cpp) with the skip of task 0243 after the conditions; kept here so that the
        // shared selection, which conversations use, is not touched.
        std::vector<FormId> ids = mDialogue.topicIds(std::string(subtype));
        std::stable_sort(ids.begin(), ids.end(),
            [&](FormId a, FormId b) { return mDialogue.topic(a)->mPriority > mDialogue.topic(b)->mPriority; });
        const SelectionOptions opts = options(pick);
        conditions::Evaluator eval;
        for (const FormId topicId : ids)
        {
            const DialogTopic* topic = mDialogue.topic(topicId);
            std::vector<const DialogInfo*> randoms;
            const DialogInfo* chosen = nullptr;
            for (const DialogInfo* info : mDialogue.infosOf(topicId))
            {
                const FormId quest
                    = info->mQuest != kNullFormId ? info->mQuest : (topic != nullptr ? topic->mQuest : kNullFormId);
                const auto skip = [&](const char* reason) {
                    if (why != nullptr && why->size() < 1500)
                        *why += " [" + hex8(info->mCommon.mId) + ": " + reason + "]";
                };
                if (opts.mQuestRunning && quest != kNullFormId && !opts.mQuestRunning(quest))
                {
                    skip("quest not running");
                    continue;
                }
                if ((info->mFlags & InfoFlags::SayOnce) != 0 && opts.mSaid && opts.mSaid(info->mCommon.mId))
                {
                    skip("said once");
                    continue;
                }
                if (info->mSpeaker != kNullFormId && info->mSpeaker != mSpeaker.mBase)
                {
                    skip("another speaker");
                    continue;
                }
                if (!info->mConditionsOk)
                {
                    skip("damaged conditions");
                    continue;
                }
                mWorld.setQuestContext(quest);
                if (opts.mQuests != nullptr)
                {
                    if (const QuestRecord* record = opts.mQuests->quest(quest))
                    {
                        if (!record->mConditionsOk)
                        {
                            skip("damaged quest conditions");
                            continue;
                        }
                        if (!record->mConditions.empty())
                        {
                            mWorld.setPlugin(&opts.mQuests->resolverOf(*record));
                            const bool questPass = eval.evaluate(record->mConditions, mWorld);
                            mWorld.setPlugin(nullptr);
                            if (!questPass)
                            {
                                skip("the quest dialogue conditions fail");
                                continue;
                            }
                        }
                    }
                }
                mWorld.setPlugin(&mDialogue.resolverOf(*info));
                const bool pass = eval.evaluate(info->mConditions, mWorld);
                mWorld.setPlugin(nullptr);
                if (!pass)
                {
                    skip("conditions fail");
                    continue;
                }
                if (!hasResponseText(*info))
                {
                    skip("no response text");
                    continue;
                }
                if (!hasVoiceFile(*info))
                {
                    skip("no voice file");
                    continue;
                }
                if (chosen == nullptr)
                {
                    chosen = info;
                    if ((info->mFlags & InfoFlags::Random) == 0)
                        break;
                }
                if ((info->mFlags & InfoFlags::Random) != 0)
                    randoms.push_back(info);
            }
            mWorld.setQuestContext(kNullFormId);
            if (!randoms.empty())
                chosen = randoms[opts.mPick % randoms.size()];
            if (chosen != nullptr)
                return chosen;
        }
        return nullptr;
    }

    bool DialogueRuntime::barkLine(std::string_view subtype, const DialogueSpeaker& speaker, std::uint32_t pick,
        openeden::sdk::game::DialogueTurn& out, bool speakerIsVictim)
    {
        out = {};
        const DialogueSpeaker savedSpeaker = mSpeaker;
        const std::string savedVoice = mVoiceType;
        mSpeaker = speaker;
        mWorld.setSpeaker(speaker);
        mVoiceType = mDialogue.voiceTypeName(mWorld.speakerVoiceType());
        // GetRandomPercent and the like draw from a generator seeded by the placement and the pick.
        mWorld.setRandomSeed(
            (speaker.mReference != kNullFormId ? speaker.mReference : speaker.mBase) ^ (std::uint64_t(pick) << 32));
        // Task 0243: an INFO with no response text or no voice file is not a bark line and is not marked said
        mWorld.setCrimeVictim(speakerIsVictim ? speaker.mBase : kNullFormId);
        const DialogInfo* const info = selectBarkInfo(subtype, pick);
        mWorld.setCrimeVictim(kNullFormId);
        const bool found = info != nullptr;
        if (found)
            fillTurn(*info, out);
        mSpeaker = savedSpeaker;
        mVoiceType = savedVoice;
        mWorld.setSpeaker(savedSpeaker);
        return found;
    }

    bool DialogueRuntime::choose(std::uint32_t topicId, openeden::sdk::game::DialogueTurn& out, std::string& error)
    {
        out = {};
        if (!mOpen)
        {
            error = "no conversation is open";
            return false;
        }
        if (mOffered.count(topicId) == 0)
        {
            error = "the topic was not offered";
            return false;
        }
        const Selection s = selectInfo(mDialogue, topicId, mWorld, mSpeaker, options(topicId));
        if (s.mInfo == nullptr)
        {
            error = "no response passes any more";
            return false;
        }
        fillTurn(*s.mInfo, out);
        // Task 0258: in the forced (arrest) conversation the choices are link-to topics; a response with none of its own
        // ends the talk, as the game closes a forced dialogue when nothing is left to choose
        if (mLinkMode && !out.endsConversation && !s.mInfo->mLinksTo.empty())
        {
            offerLinks(*s.mInfo, out);
            if (!out.topics.empty())
                return true;
        }
        if (mLinkMode)
            out.endsConversation = true;
        if (out.endsConversation)
        {
            mOpen = false;
            mLinkMode = false;
            out.topics.clear();
            mWorld.setConversationOpen(false);
            mOffered.clear();
            return true;
        }
        offerTopics(out);
        return true;
    }

    // ---- task 0258: the guard's forced greeting and its link-to choices ----------------------------------------------

    bool DialogueRuntime::forcedGreetingAvailable(
        const DialogueSpeaker& speaker, std::string_view subtype, std::string* why)
    {
        const DialogueSpeaker savedSpeaker = mSpeaker;
        const std::string savedVoice = mVoiceType;
        mSpeaker = speaker;
        mWorld.setSpeaker(speaker);
        mVoiceType = mDialogue.voiceTypeName(mWorld.speakerVoiceType());
        mWorld.setRandomSeed(speaker.mReference != kNullFormId ? speaker.mReference : speaker.mBase);
        const bool found = selectBarkInfo(subtype, 0, why) != nullptr;
        mSpeaker = savedSpeaker;
        mVoiceType = savedVoice;
        mWorld.setSpeaker(savedSpeaker);
        return found;
    }

    bool DialogueRuntime::beginForced(const DialogueSpeaker& speaker, std::string_view subtype,
        openeden::sdk::game::DialogueTurn& out, std::string& error)
    {
        out = {};
        mOpen = false;
        mLinkMode = false;
        mSpeaker = speaker;
        mWorld.setSpeaker(speaker);
        mWorld.setConversationOpen(true);
        mWorld.setTalkedToPlayer(false);
        mWorld.setRandomSeed(speaker.mReference != kNullFormId ? speaker.mReference : speaker.mBase);
        mVoiceType = mDialogue.voiceTypeName(mWorld.speakerVoiceType());
        const DialogInfo* const greeting = selectBarkInfo(subtype, 0, nullptr);
        if (greeting == nullptr)
        {
            mWorld.setConversationOpen(false);
            error = "no forced greeting passes for this speaker";
            return false;
        }
        fillTurn(*greeting, out);
        if (const ActorNpc* npc = mActors.npc(speaker.mBase); npc != nullptr && npc->mName.mPresent)
            out.speakerName = npc->mName.mLocalized ? mResolver(npc->mName, npc->mPlugin, StringTableKind::Strings)
                                                    : npc->mName.mText;
        ++mStats.mConversations;
        mWorld.setTalkedToPlayer(true);
        mOpen = true;
        mLinkMode = true;
        offerLinks(*greeting, out);
        if (out.topics.empty())
            out.endsConversation = true; // nothing to choose: the game would close it
        return true;
    }

    void DialogueRuntime::offerLinks(const DialogInfo& from, openeden::sdk::game::DialogueTurn& out)
    {
        out.topics.clear();
        mOffered.clear();
        const SelectionOptions opts = options(0);
        for (const FormId link : from.mLinksTo)
        {
            const DialogTopic* topic = mDialogue.topic(link);
            if (topic == nullptr || mOffered.count(link) != 0)
                continue;
            const Selection s = selectInfo(mDialogue, link, mWorld, mSpeaker, opts);
            if (s.mInfo == nullptr)
                continue;
            // The prompt of the response (RNAM) is what the player reads when it is set, else the topic's name.
            std::string text;
            if (s.mInfo->mPrompt.mPresent)
                text = mResolver(s.mInfo->mPrompt, s.mInfo->mPlugin, StringTableKind::Strings);
            if (text.empty())
                text = topic->mName.mLocalized ? mResolver(topic->mName, topic->mPlugin, StringTableKind::Strings)
                                               : topic->mName.mText;
            text = expandText(text, topic->mQuest != kNullFormId ? topic->mQuest : s.mInfo->mQuest);
            if (text.empty())
                continue;
            if (out.topics.size() >= mLimits.mMaxTopics)
            {
                ++mStats.mTopicsCut;
                continue;
            }
            out.topics.push_back({ link, std::move(text) });
            mOffered.insert(link);
        }
    }

    void DialogueRuntime::end()
    {
        mOpen = false;
        mWorld.setConversationOpen(false);
        mOffered.clear();
    }
}
