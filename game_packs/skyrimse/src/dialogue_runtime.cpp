#include "game_skyrimse/dialogue_runtime.hpp"

#include <algorithm>
#include <utility>

#include "game_skyrimse/voice.hpp"

namespace game_skyrimse
{
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
        if (mVoiceProbe)
            nameVoice(info, out);
        for (const DialogResponse& response : info.mResponses)
        {
            std::string line = response.mText.mPresent && !response.mText.mLocalized
                ? response.mText.mText
                : mResolver(response.mText, info.mPlugin, StringTableKind::IlStrings);
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
        ++mStats.mResponses;
        if (info.mHasScript)
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
                it = mServiceOfScript.emplace(info.mFragmentScript, classifyService(mScriptProbe(info.mFragmentScript)))
                         .first;
            if (!it->second.empty())
            {
                out.service = it->second;
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
        mSpeaker = speaker;
        mWorld.setSpeaker(speaker);
        // Task 0156: the speaker is in dialogue with the player from the greeting on, has not talked to him yet, and any
        // random number comes from a generator seeded by the placement (the same actor greets the same way).
        mWorld.setConversationOpen(true);
        mWorld.setTalkedToPlayer(false);
        mWorld.setRandomSeed(speaker.mReference != kNullFormId ? speaker.mReference : speaker.mBase);
        mVoiceType = mDialogue.voiceTypeName(mWorld.speakerVoiceType());
        // The random pick is a function of the placement, so the same actor greets the same way in a session.
        const Selection greeting = pickFromTopics(
            mDialogue, "HELO", mWorld, mSpeaker, options(speaker.mReference != kNullFormId ? speaker.mReference : speaker.mBase));
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
        if (out.endsConversation)
        {
            mOpen = false;
            mWorld.setConversationOpen(false);
            mOffered.clear();
            return true;
        }
        offerTopics(out);
        return true;
    }

    void DialogueRuntime::end()
    {
        mOpen = false;
        mWorld.setConversationOpen(false);
        mOffered.clear();
    }
}
