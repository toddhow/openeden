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
            hooks.mAliasFemale
                = [this, quest](std::string_view alias) { return mTagSources.mAliasFemale(quest, alias); };
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

    const DialogInfo& DialogueRuntime::responderOf(const DialogInfo& info) const
    {
        if (!info.mResponses.empty() || info.mSharedInfo == kNullFormId || info.mSharedInfo == kUnresolvedFormId)
            return info;
        const DialogInfo* shared = mDialogue.findInfo(info.mSharedInfo);
        return shared != nullptr && shared != &info && !shared->mResponses.empty() ? *shared : info;
    }

    bool DialogueRuntime::evaluateQuestConditions(
        const std::vector<survey::Ctda>& list, FormId quest, const FormIdResolver& resolver, FormId playerLocation)
    {
        if (list.empty())
            return true;
        conditions::Evaluator eval;
        mWorld.setQuestContext(quest);
        mWorld.setPlugin(&resolver);
        mWorld.setPlayerLocation(playerLocation);
        // the entry's conditions run on the player; the last speaker stands for him, so he stands where he does
        const FormId speakerAt = mWorld.speakerLocation();
        const bool speakerKnown = mWorld.speakerLocationKnown();
        if (playerLocation != kNullFormId)
            mWorld.setSpeakerLocation(playerLocation, true);
        const bool pass = eval.evaluate(list, mWorld);
        if (playerLocation != kNullFormId)
            mWorld.setSpeakerLocation(speakerAt, speakerKnown);
        mWorld.setPlayerLocation(kNullFormId);
        mWorld.setPlugin(nullptr);
        mWorld.setQuestContext(kNullFormId);
        return pass;
    }

    void DialogueRuntime::nameVoice(const DialogInfo& asked, openeden::sdk::game::DialogueTurn& out)
    {
        const DialogInfo& info = responderOf(asked); // task 0261: a shared info's files carry its own id
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
        const DialogInfo& responder = responderOf(info); // task 0261: responses may be shared
        const DialogTopic* ownTopic = mDialogue.topic(info.mTopic);
        const FormId topicQuest
            = ownTopic != nullptr && ownTopic->mQuest != kNullFormId ? ownTopic->mQuest : info.mQuest;
        if (mVoiceProbe)
            nameVoice(info, out);
        for (const DialogResponse& response : responder.mResponses)
        {
            std::string line = response.mText.mPresent && !response.mText.mLocalized
                ? response.mText.mText
                : mResolver(response.mText, responder.mPlugin, StringTableKind::IlStrings);
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
        mLastContinues = (info.mFlags & InfoFlags::InvisibleContinue) != 0; // task 0340
        mWalkAway = info.mWalkAwayTopic; // task 0315: the last response decides which walk-away topic waits
        mLastTopic = info.mTopic; // task 0326
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

    bool DialogueRuntime::hidesWalkAway(FormId topic) const
    {
        // Task 0325: the walk-away topic a response names (TWAT) is said as the player leaves; it is never a button even
        // when the response also links to it (Astrid's every line links to DB02AstridWalkAwayTopic, a topic whose name is
        // its editor id). Only with walk-away topics switched on.
        return mWalkAwayTopics && mWalkAway != kNullFormId && topic == mWalkAway;
    }

    void DialogueRuntime::offerTopics(openeden::sdk::game::DialogueTurn& out)
    {
        out.topics.clear();
        mOffered.clear();
        // Task 0340: the plain topics test what the line just shown set (Skjor's `SkjorGaveLowdown`), so its queued
        // fragments run first, as offerLinks has done since task 0310
        if (mFragmentFlush)
            mFragmentFlush();
        const SelectionOptions opts = options(0);
        // Task 0261: after a response that links to topics (TCLT) the menu is those of them that pass; when none does
        // it is the normal list. A topic that sits in a branch as a member, not its start, is only ever a choice.
        std::vector<FormId> candidates = mDialogue.topicIds();
        bool choices = false;
        if (!mChoices.empty())
        {
            std::vector<FormId> linked;
            for (const FormId id : mChoices)
            {
                const DialogTopic* topic = mDialogue.topic(id);
                if (topic == nullptr || topic->mCategory != TopicCategory::Topic)
                    continue;
                if (hidesWalkAway(id)) // task 0325
                    continue;
                // task 0270: a choice may have no name of its own (the marriage branch's yes and no): the prompt of its
                // INFO (RNAM) is what the player reads, so a nameless topic counts when the chosen INFO has one
                const Selection pick = selectInfo(mDialogue, id, mWorld, mSpeaker, opts);
                if (pick.mInfo != nullptr && (topic->mName.mPresent || pick.mInfo->mPrompt.mPresent))
                    linked.push_back(id);
            }
            if (!linked.empty())
            {
                candidates = std::move(linked);
                choices = true;
            }
            else
                mChoices.clear();
        }
        for (FormId id : candidates)
        {
            const DialogTopic* topic = mDialogue.topic(id);
            if (topic == nullptr || topic->mCategory != TopicCategory::Topic)
                continue;
            if (!topic->mName.mPresent && !choices) // a nameless topic is only ever a choice (task 0270)
                continue;
            if (!choices && !mDialogue.isBranchStart(id))
                continue;
            const Selection s = selectInfo(mDialogue, id, mWorld, mSpeaker, opts);
            if (s.mInfo == nullptr)
                continue;
            if (!topic->mName.mPresent && !s.mInfo->mPrompt.mPresent)
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
        const std::uint32_t pickNumber = speaker.mReference != kNullFormId ? speaker.mReference : speaker.mBase;
        const SelectionOptions greetingOptions = options(pickNumber);
        Selection greeting;
        // The greeting step, in this order: tasks 0298 and 0304 each say a line of their own instead of Hello, behind
        // their own settings, then the Hello pick as before.
        // 1. Task 0298: a Blocking branch (the Civil War's Rikke: "Are you lost?", "You survived Helgen?") says its first line
        // that passes; the lowest topic id comes first so the choice never depends on a hash order
        if (mBlockingBranches)
            for (const FormId start : mDialogue.blockingStarts())
            {
                greeting = selectInfo(mDialogue, start, mWorld, mSpeaker, greetingOptions);
                conditions::conditionDebugReport("GREETING blocking start " + hex8(start) + ": "
                    + std::to_string(greeting.mConsidered) + " considered, " + std::to_string(greeting.mPassing)
                    + " passing"); // task 0324 (debug aid sinks only)
                if (greeting.mInfo != nullptr)
                    break;
            }
        // 1b. Task 0340: the game picks a Blocking branch's entry line BEFORE the actor is in dialogue, and such lines test
        // IsInDialogueWithPlayer == 0 (Skjor's C01SkjorEntry). The pass above treats the conversation as open (the Hello
        // lines and the other branches count on it), so a start no line of which passed is tried once more with the
        // conversation not yet open. Additive: nothing that passed before is chosen differently.
        if (mBlockingBranches && greeting.mInfo == nullptr)
        {
            mWorld.setConversationOpen(false);
            for (const FormId start : mDialogue.blockingStarts())
            {
                greeting = selectInfo(mDialogue, start, mWorld, mSpeaker, greetingOptions);
                if (greeting.mInfo != nullptr)
                    break;
            }
            mWorld.setConversationOpen(true);
        }
        // 2. Task 0304: a quest's own greeting (the nameless start of one of its branches)
        if (greeting.mInfo == nullptr && mQuestGreetings)
        {
            if (!mGreetingTopicsKnown)
            {
                mGreetingTopics = mDialogue.questGreetingTopics();
                mGreetingTopicsKnown = true;
            }
            for (const FormId id : mGreetingTopics)
            {
                const Selection s = selectInfo(mDialogue, id, mWorld, mSpeaker, greetingOptions);
                conditions::conditionDebugReport("GREETING candidate " + hex8(id) + ": " + std::to_string(s.mConsidered)
                    + " considered, " + std::to_string(s.mPassing) + " passing"); // task 0324 (debug aid sinks only)
                if (mTrace && s.mInfo != nullptr)
                    mTrace("quest greeting topic " + hex8(id) + ": " + std::to_string(s.mConsidered)
                        + " considered, " + std::to_string(s.mPassing) + " passing, chosen "
                        + hex8(s.mInfo->mCommon.mId));
                if (s.mInfo != nullptr)
                {
                    greeting = s;
                    break;
                }
            }
        }
        // 3. Hello
        if (greeting.mInfo == nullptr)
            greeting = pickFromTopics(mDialogue, "HELO", mWorld, mSpeaker, greetingOptions);
        if (greeting.mInfo == nullptr)
        {
            error = "no greeting passes for this speaker";
            return false;
        }
        fillTurn(*greeting.mInfo, out);
        mChoices = greeting.mInfo->mLinksTo; // task 0261
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

    bool DialogueRuntime::hasResponseText(const DialogInfo& asked) const
    {
        const DialogInfo& info = responderOf(asked); // task 0261
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

    void DialogueRuntime::followNamelessLinks(openeden::sdk::game::DialogueTurn& out)
    {
        // Task 0304: a response that links to a topic with no name whose INFO has no prompt is not a choice for the
        // player: the speaker goes on with it (Faralda's "Excellent." continues with the Nirya spell branch's greeting).
        if (!mQuestGreetings)
            return;
        // task 0340: a line flagged Goodbye AND InvisibleContinue (Skjor's "Farkas will be your Shield-Sibling") goes on with its
        // link before the conversation ends
        for (std::size_t depth = 0; depth < kMaxFollowDepth && (!out.endsConversation || mLastContinues); ++depth)
        {
            const DialogInfo* next = nullptr;
            if (mFragmentFlush)
                mFragmentFlush();
            for (const FormId id : mChoices)
            {
                const DialogTopic* topic = mDialogue.topic(id);
                if (topic == nullptr || topic->mCategory != TopicCategory::Topic || topic->mName.mPresent)
                    continue;
                // task 0331: the walk-away topic of the response just said (Viarmo's "You're leaving? Okay, we'll just
                // work on the verse in a bit.", linked beside the poem choices) is said as the player LEAVES, not a
                // continuation: it is skipped here as it is in the menus (hidesWalkAway)
                if (hidesWalkAway(id))
                    continue;
                const Selection s = selectInfo(mDialogue, id, mWorld, mSpeaker, options(id));
                if (s.mInfo != nullptr && !s.mInfo->mPrompt.mPresent)
                {
                    next = s.mInfo;
                    break;
                }
            }
            if (next == nullptr)
                return;
            openeden::sdk::game::DialogueTurn more;
            fillTurn(*next, more);
            if (!more.text.empty())
            {
                if (!out.text.empty())
                    out.text += ' ';
                out.text += more.text;
            }
            out.voice.insert(out.voice.end(), more.voice.begin(), more.voice.end());
            out.endsConversation = more.endsConversation;
            if (!more.service.empty())
                out.service = more.service;
            if (mTrace)
                mTrace("followed " + hex8(next->mCommon.mId));
            mChoices = next->mLinksTo;
        }
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
        mChoices = s.mInfo->mLinksTo; // task 0261: the next menu is what this response links to (offerTopics)
        if (!mLinkMode) // task 0310: a forced conversation follows a link with no name in offerLinks (once)
            followNamelessLinks(out); // task 0304
        // Task 0258: in the forced (arrest) conversation the choices are link-to topics; a response with none of its
        // own ends the talk, as the game closes a forced dialogue when nothing is left to choose
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
            mChoices.clear();
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

    // ---- task 0258: the guard's forced greeting and its link-to choices
    // ----------------------------------------------

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

    bool DialogueRuntime::beginForcedTopic(
        const DialogueSpeaker& speaker, FormId topic, openeden::sdk::game::DialogueTurn& out, std::string& error)
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
        const Selection selected = selectInfo(mDialogue, topic, mWorld, mSpeaker, options(0));
        const DialogInfo* const greeting = selected.mInfo;
        if (greeting == nullptr)
        {
            mWorld.setConversationOpen(false);
            error = "no line of the forced greeting's topic passes for this speaker";
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
            out.endsConversation = true;
        return true;
    }

    void DialogueRuntime::offerLinks(const DialogInfo& from, openeden::sdk::game::DialogueTurn& out, int depth)
    {
        out.topics.clear();
        mOffered.clear();
        if (mFragmentFlush) // task 0310: what the response just said set in the world (a quest stage) before a link is chosen
            mFragmentFlush();
        const SelectionOptions opts = options(0);
        const DialogInfo* silent = nullptr; // task 0271: the first link that is no choice (the speaker goes on)
        for (const FormId link : from.mLinksTo)
        {
            const DialogTopic* topic = mDialogue.topic(link);
            if (hidesWalkAway(link)) // task 0325
                continue;
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
            {
                if (silent == nullptr && hasResponseText(*s.mInfo))
                    silent = s.mInfo;
                continue;
            }
            if (out.topics.size() >= mLimits.mMaxTopics)
            {
                ++mStats.mTopicsCut;
                continue;
            }
            out.topics.push_back({ link, std::move(text) });
            mOffered.insert(link);
        }
        constexpr int kMaxChain = 4;
        if (out.topics.empty() && silent != nullptr && depth < kMaxChain)
        {
            std::string said = std::move(out.text);
            std::vector<std::string> heard = std::move(out.voice);
            fillTurn(*silent, out);
            if (!said.empty())
                out.text = said + ' ' + out.text;
            heard.insert(heard.end(), out.voice.begin(), out.voice.end());
            out.voice = std::move(heard);
            offerLinks(*silent, out, depth + 1);
        }
    }

    void DialogueRuntime::end()
    {
        // Task 0315: the walk-away topic of the last response is "said" as the conversation ends: its INFO (the first
        // that passes) runs its fragments; nothing is shown. Once: a second end() finds it spent.
        if (mWalkAwayTopics && mWalkAway != kNullFormId && mOpen)
        {
            const FormId topic = mWalkAway;
            mWalkAway = kNullFormId;
            const Selection s = selectInfo(mDialogue, topic, mWorld, mSpeaker, options(0));
            if (s.mInfo != nullptr)
            {
                openeden::sdk::game::DialogueTurn quiet;
                fillTurn(*s.mInfo, quiet);
            }
        }
        else if (mWalkAwayInfos && mLastTopic != kNullFormId && mOpen)
        {
            // Task 0326: no TWAT; the topic of the last response may hold a walk-away INFO of its own (flag 0x80): the
            // first INFO that passes runs its fragments if it is that kind (Rikke's INFO 0D5EE9: CW01A stage 160)
            const Selection s = selectInfo(mDialogue, mLastTopic, mWorld, mSpeaker, options(0));
            if (s.mInfo != nullptr && (s.mInfo->mFlags & InfoFlags::WalkAway) != 0)
            {
                openeden::sdk::game::DialogueTurn quiet;
                fillTurn(*s.mInfo, quiet);
            }
        }
        mLastTopic = kNullFormId;
        mWalkAway = kNullFormId;
        mChoices.clear();
        mOpen = false;
        mWorld.setConversationOpen(false);
        mOffered.clear();
    }
}
