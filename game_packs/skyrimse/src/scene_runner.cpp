#include "game_skyrimse/scene_runner.hpp"
#include <cstdlib>

#include <algorithm>
#include <cmath>
#include <cstdio>

// Rules: see the header (task 0194). All of them are RECALLED-VERIFY and named options.

namespace game_skyrimse
{
    namespace
    {
        const bool sSceneWaitTrace = std::getenv("OPENEDEN_SCENE_WAIT") != nullptr; // a developer trace (task 0327)

        enum class ActionStage : std::uint8_t
        {
            Waiting,
            Running,
            Done,
        };

        struct ActionState
        {
            ActionStage mStage = ActionStage::Waiting;
            float mElapsed = 0.f;
            float mDuration = 0.f; ///< dialogue: the estimated line; timer: its seconds
            float mParked = 0.f;
            float mRetry = 0.f;
            bool mParkCounted = false;
            FormId mActor = kNullFormId;
            bool mPackageGiven = false;
        };

        std::string hex(FormId id)
        {
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%08X", id);
            return buffer;
        }
    }

    struct SceneRunner::Instance
    {
        FormId mId = kNullFormId;
        const SceneRecord* mRecord = nullptr;
        int mPhase = 0;
        bool mEntered = false;
        std::vector<ActionState> mActions;
        std::vector<FormId> mHeld;
        float mTime = 0.f;
        int mNote = 0;
    };

    SceneRunner::SceneRunner(const SceneIndex& scenes, SceneEnvironment& env, Options options)
        : mScenes(scenes)
        , mEnv(env)
        , mOptions(options)
    {
    }

    SceneRunner::~SceneRunner() = default;

    bool SceneRunner::start(FormId scene, bool force)
    {
        if (startImpl(scene, force, nullptr))
            return true;
        // task 0264: a script's explicit Scene.Start that does nothing says why (the quiet refusals of startImpl)
        const SceneRecord* record = mScenes.scene(scene);
        if (record == nullptr)
            mEnv.log("SCENE " + hex(scene) + " not started: no such scene");
        else if (mPlaying.count(scene) != 0)
            mEnv.log("SCENE " + hex(scene) + " not started: it is playing already");
        else if (!mEnv.questRunning(record->mQuest))
            mEnv.log("SCENE " + hex(scene) + " not started: its quest " + hex(record->mQuest) + " is not running");
        else if (mPlaying.size() >= mOptions.mMaxPlaying)
            mEnv.log("SCENE " + hex(scene) + " not started: too many scenes are playing");
        else if (!force && !record->mConditions.empty())
            mEnv.log("SCENE " + hex(scene) + " not started: no speaker is loaded or its conditions fail");
        else
        {
            // task 0327: the one remaining quiet refusal of startImpl: not one of its actors is loaded
            std::string refs;
            for (const SceneActor& a : record->mActors)
                refs += " " + hex(mEnv.aliasReference(record->mQuest, a.mAlias));
            mEnv.log("SCENE " + hex(scene) + " not started: none of its actors is loaded (alias references" + refs
                + ")");
        }
        return false;
    }

    bool SceneRunner::startImpl(FormId scene, bool force, const SceneSaveEntry* resume)
    {
        const SceneRecord* record = mScenes.scene(scene);
        if (record == nullptr || mPlaying.count(scene) != 0 || !mEnv.questRunning(record->mQuest)
            || mPlaying.size() >= mOptions.mMaxPlaying)
        {
            ++mStats.mRefused;
            return false;
        }
        std::vector<FormId> held;
        for (const SceneActor& a : record->mActors)
        {
            const FormId ref = mEnv.aliasReference(record->mQuest, a.mAlias);
            if (ref != kNullFormId && std::find(held.begin(), held.end(), ref) == held.end())
                held.push_back(ref);
        }
        if (held.empty())
        {
            ++mStats.mRefused;
            mEnv.log("SCENE " + hex(scene) + " not started: no actor of it has a reference");
            return false;
        }
        // a resumed scene waits for its speakers as a started one does: nobody is loaded yet while a save is loading
        // task 0333: a scene with no actions only runs its fragments, so it needs nobody to be loaded
        const bool needsLoaded = !(mOptions.mActionlessStartsUnloaded && record->mActions.empty());
        if (resume == nullptr && needsLoaded
            && std::none_of(held.begin(), held.end(), [this](FormId ref) { return mEnv.actorLoaded(ref); }))
        {
            ++mStats.mRefused;
            ++mStats.mNotLoaded;
            return false;
        }
        if (resume == nullptr && !force && !record->mConditions.empty()
            && !mEnv.evaluate(record->mConditions, scene, record->mQuest))
        {
            ++mStats.mRefused;
            return false;
        }
        auto instance = std::make_unique<Instance>();
        instance->mId = scene;
        instance->mRecord = record;
        instance->mActions.resize(record->mActions.size());
        // the actors that speak and walk nowhere are held still; one with a package action must be free to walk
        for (const SceneAction& a : record->mActions)
        {
            if (a.mType != SceneActionType::Dialogue || a.mTopic == kNullFormId)
                continue;
            const FormId ref = mEnv.aliasReference(record->mQuest, a.mAlias);
            const bool walks = std::any_of(record->mActions.begin(), record->mActions.end(),
                [&a](const SceneAction& o) { return o.mType == SceneActionType::Package && o.mAlias == a.mAlias; });
            if (ref != kNullFormId && !walks
                && std::find(instance->mHeld.begin(), instance->mHeld.end(), ref) == instance->mHeld.end())
                instance->mHeld.push_back(ref);
        }
        mDone[scene].clear();
        if (resume != nullptr)
        {
            // task 0211: the phase, whether it was entered, and the stage of each action as the save kept them
            instance->mPhase = resume->mPhase;
            instance->mEntered = resume->mEntered;
            for (std::size_t i = 0; i < instance->mActions.size(); ++i)
            {
                const std::uint8_t stage = resume->mActions[i];
                instance->mActions[i].mStage = stage == 2 ? ActionStage::Done
                    : stage == 1                          ? ActionStage::Running
                                                          : ActionStage::Waiting;
                if (stage == 2)
                    mDone[scene].insert(record->mActions[i].mIndex);
            }
        }
        for (const FormId ref : instance->mHeld)
            mEnv.hold(ref, true);
        mPlaying[scene] = std::move(instance);
        ++mStats.mStarted;
        if (resume != nullptr)
        {
            mEnv.log("SCENE " + hex(scene) + " resumes at phase " + std::to_string(resume->mPhase));
            return true;
        }
        mEnv.log("SCENE " + hex(scene) + " begins (" + std::to_string(record->mPhases.size()) + " phases, "
            + std::to_string(record->mActions.size()) + " actions)");
        if (record->mVmad && record->mVmad->mHasSceneFragments)
        {
            const vmad::SceneFragments& f = record->mVmad->mScene;
            if ((f.mFlags & 1) != 0 && !f.mBeginFunction.empty())
            {
                ++mStats.mFragments;
                mEnv.runFragment(scene, f.mBeginScript, f.mBeginFunction);
            }
        }
        return true;
    }

    void SceneRunner::finish(FormId scene, bool ended)
    {
        const auto it = mPlaying.find(scene);
        if (it == mPlaying.end())
            return;
        Instance& in = *it->second;
        for (std::size_t i = 0; i < in.mActions.size(); ++i)
        {
            ActionState& st = in.mActions[i];
            if (in.mRecord->mActions[i].mType == SceneActionType::Package && st.mPackageGiven)
            {
                mEnv.clearPackage(st.mActor);
                st.mPackageGiven = false;
            }
        }
        for (const FormId ref : in.mHeld)
            mEnv.hold(ref, false);
        const SceneRecord* record = in.mRecord;
        mPlaying.erase(it);
        if (ended)
        {
            ++mStats.mCompleted;
            mEnv.log("SCENE " + hex(scene) + " ends");
            if (record->mVmad && record->mVmad->mHasSceneFragments)
            {
                const vmad::SceneFragments& f = record->mVmad->mScene;
                if ((f.mFlags & 2) != 0 && !f.mEndFunction.empty())
                {
                    ++mStats.mFragments;
                    mEnv.runFragment(scene, f.mEndScript, f.mEndFunction);
                }
            }
            if ((record->mFlags & SceneFlags::StopQuestOnEnd) != 0)
                mEnv.stopQuest(record->mQuest);
        }
        else
        {
            ++mStats.mStopped;
            mEnv.log("SCENE " + hex(scene) + " stopped");
        }
    }

    bool SceneRunner::stop(FormId scene)
    {
        if (mPlaying.count(scene) == 0)
            return false;
        finish(scene, false);
        return true;
    }

    bool SceneRunner::isPlaying(FormId scene) const
    {
        return mPlaying.count(scene) != 0;
    }

    std::size_t SceneRunner::questStarted(FormId quest)
    {
        std::size_t started = 0;
        for (const FormId id : mScenes.scenesOfQuest(quest))
        {
            const SceneRecord* record = mScenes.scene(id);
            if (record == nullptr || (record->mFlags & SceneFlags::BeginOnQuestStart) == 0)
                continue;
            const std::uint64_t notLoaded = mStats.mNotLoaded;
            if (start(id))
                ++started;
            else if (mOptions.mRetryQuestStart && mStats.mNotLoaded != notLoaded
                && std::find(mWaitingStart.begin(), mWaitingStart.end(), id) == mWaitingStart.end())
                mWaitingStart.push_back(id);
        }
        return started;
    }

    void SceneRunner::questStopped(FormId quest)
    {
        for (const FormId id : mScenes.scenesOfQuest(quest))
            (void)stop(id);
    }

    bool SceneRunner::actionComplete(FormId scene, std::uint32_t actionIndex) const
    {
        const auto it = mDone.find(scene);
        return it != mDone.end() && it->second.count(actionIndex) != 0;
    }

    int SceneRunner::phaseOf(FormId scene) const
    {
        const auto it = mPlaying.find(scene);
        return it == mPlaying.end() ? -1 : it->second->mPhase;
    }

    std::size_t SceneRunner::stopAll()
    {
        std::vector<FormId> ids;
        ids.reserve(mPlaying.size());
        for (const auto& [id, in] : mPlaying)
            ids.push_back(id);
        for (const FormId id : ids)
            finish(id, false);
        return ids.size();
    }

    std::vector<SceneSaveEntry> SceneRunner::saveState() const
    {
        std::vector<SceneSaveEntry> out;
        out.reserve(mPlaying.size());
        for (const auto& [id, in] : mPlaying) // ordered by scene id
        {
            SceneSaveEntry entry;
            entry.mScene = id;
            entry.mPhase = in->mPhase;
            entry.mEntered = in->mEntered;
            entry.mActions.reserve(in->mActions.size());
            for (const ActionState& a : in->mActions)
                entry.mActions.push_back(a.mStage == ActionStage::Done ? 2 : a.mStage == ActionStage::Running ? 1 : 0);
            out.push_back(std::move(entry));
        }
        return out;
    }

    SceneRestoreResult SceneRunner::restoreState(const std::vector<SceneSaveEntry>& entries)
    {
        SceneRestoreResult result;
        for (const SceneSaveEntry& entry : entries)
        {
            const SceneRecord* record = mScenes.scene(entry.mScene);
            const bool fits = record != nullptr && entry.mPhase >= 0
                && entry.mPhase < static_cast<std::int32_t>(record->mPhases.size())
                && entry.mActions.size() == record->mActions.size();
            if (!fits || !startImpl(entry.mScene, true, &entry))
            {
                ++result.mDropped;
                mEnv.log("SCENE " + hex(entry.mScene) + " not resumed");
                continue;
            }
            ++result.mResumed;
        }
        return result;
    }

    namespace
    {
        constexpr std::uint32_t kMaxSavedScenes = 64;
        constexpr std::uint32_t kMaxSavedActions = 4096;
        constexpr std::int32_t kMaxSavedPhase = 4096;

        void put32(std::vector<std::uint8_t>& out, std::uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
        }
    }

    std::vector<std::uint8_t> encodeSceneState(const std::vector<SceneSaveEntry>& entries)
    {
        std::vector<std::uint8_t> out;
        put32(out, static_cast<std::uint32_t>(entries.size()));
        for (const SceneSaveEntry& e : entries)
        {
            put32(out, e.mScene);
            put32(out, static_cast<std::uint32_t>(e.mPhase));
            out.push_back(e.mEntered ? 1 : 0);
            put32(out, static_cast<std::uint32_t>(e.mActions.size()));
            out.insert(out.end(), e.mActions.begin(), e.mActions.end());
        }
        return out;
    }

    bool decodeSceneState(std::span<const std::uint8_t> bytes, std::vector<SceneSaveEntry>& out, std::string& error)
    {
        out.clear();
        std::size_t at = 0;
        const auto fail = [&](const std::string& why) {
            out.clear();
            error = why + " (byte " + std::to_string(at) + ")";
            return false;
        };
        const auto read32 = [&](std::uint32_t& v) {
            if (bytes.size() - at < 4)
                return false;
            v = 0;
            for (int i = 0; i < 4; ++i)
                v |= static_cast<std::uint32_t>(bytes[at + static_cast<std::size_t>(i)]) << (8 * i);
            at += 4;
            return true;
        };
        std::uint32_t count = 0;
        if (!read32(count))
            return fail("the scene state is cut short");
        if (count > kMaxSavedScenes)
            return fail("more than " + std::to_string(kMaxSavedScenes) + " scenes");
        for (std::uint32_t i = 0; i < count; ++i)
        {
            SceneSaveEntry e;
            std::uint32_t phase = 0, actions = 0;
            if (!read32(e.mScene) || !read32(phase))
                return fail("a scene is cut short");
            e.mPhase = static_cast<std::int32_t>(phase);
            if (e.mPhase < 0 || e.mPhase > kMaxSavedPhase)
                return fail("a scene has a phase out of range");
            if (bytes.size() - at < 1)
                return fail("a scene is cut short");
            const std::uint8_t entered = bytes[at++];
            if (entered > 1)
                return fail("a scene has a bad entered flag");
            e.mEntered = entered == 1;
            if (!read32(actions))
                return fail("a scene is cut short");
            if (actions > kMaxSavedActions)
                return fail("a scene has more than " + std::to_string(kMaxSavedActions) + " actions");
            if (bytes.size() - at < actions)
                return fail("a scene claims more actions than the bytes hold");
            for (std::uint32_t a = 0; a < actions; ++a)
            {
                const std::uint8_t stage = bytes[at + a];
                if (stage > 2)
                    return fail("an action has a bad stage");
                e.mActions.push_back(stage);
            }
            at += actions;
            for (const SceneSaveEntry& other : out)
                if (other.mScene == e.mScene)
                    return fail("a scene is listed twice");
            out.push_back(std::move(e));
        }
        if (at != bytes.size())
            return fail("trailing bytes after the last scene");
        error.clear();
        return true;
    }

    namespace
    {
        void runPhaseFragments(SceneEnvironment& env, FormId scene, const SceneRecord& record, int phase,
            bool completion, std::uint64_t& counter)
        {
            if (!record.mVmad || !record.mVmad->mHasSceneFragments)
                return;
            for (const vmad::PhaseFragment& f : record.mVmad->mScene.mPhases)
                if (f.mPhase == phase && (completion ? f.onCompletion() : f.onStart()) && !f.mFunction.empty())
                {
                    ++counter;
                    env.runFragment(scene, f.mScript, f.mFunction);
                }
        }
    }

    void SceneRunner::update(float dt)
    {
        if (!std::isfinite(dt) || dt < 0.f)
            dt = 0.f;
        if (!mWaitingStart.empty())
        {
            mWaitingClock += dt;
            if (mWaitingClock >= mOptions.mRetrySeconds)
            {
                mWaitingClock = 0.f;
                const std::vector<FormId> waiting = mWaitingStart;
                mWaitingStart.clear();
                for (const FormId id : waiting)
                {
                    const SceneRecord* record = mScenes.scene(id);
                    if (record == nullptr || !mEnv.questRunning(record->mQuest))
                        continue; // its quest stopped: no longer wanted
                    const std::uint64_t notLoaded = mStats.mNotLoaded;
                    if (!start(id) && mStats.mNotLoaded != notLoaded)
                        mWaitingStart.push_back(id);
                }
            }
        }
        std::vector<FormId> ids;
        ids.reserve(mPlaying.size());
        for (const auto& [id, in] : mPlaying)
            ids.push_back(id);
        for (const FormId id : ids)
        {
            const auto found = mPlaying.find(id);
            if (found == mPlaying.end())
                continue;
            Instance& in = *found->second;
            const SceneRecord& record = *in.mRecord;
            float step = dt;
            bool ended = false;
            for (int transitions = 0; transitions < 16 && !ended; ++transitions)
            {
                if (!in.mEntered)
                {
                    if (in.mPhase >= static_cast<int>(record.mPhases.size()))
                    {
                        ended = true;
                        break;
                    }
                    const ScenePhase& phase = record.mPhases[static_cast<std::size_t>(in.mPhase)];
                    if (!phase.mConditionsOk
                        || (!phase.mStartConditions.empty()
                            && !mEnv.evaluate(phase.mStartConditions, id, record.mQuest)))
                    {
                        if (mOptions.mSkipFailedPhases && phase.mConditionsOk) // task 0308
                        {
                            mEnv.log("SCENE " + hex(id) + " phase " + std::to_string(in.mPhase)
                                + " skipped: its start conditions fail");
                            ++in.mPhase;
                            continue;
                        }
                        break;
                    }
                    in.mEntered = true;
                    ++mStats.mPhases;
                    mEnv.log("SCENE " + hex(id) + " phase " + std::to_string(in.mPhase));
                    runPhaseFragments(mEnv, id, record, in.mPhase, false, mStats.mFragments);
                    for (std::size_t i = 0; i < record.mActions.size(); ++i)
                    {
                        const SceneAction& a = record.mActions[i];
                        if (in.mActions[i].mStage != ActionStage::Waiting)
                            continue;
                        const auto phaseNow = static_cast<std::uint32_t>(in.mPhase);
                        if (a.mStartPhase == phaseNow)
                            in.mActions[i].mStage = ActionStage::Running;
                        else if (a.mStartPhase < phaseNow && a.mEndPhase >= phaseNow)
                        {
                            // task 0328: the phase the action starts in was skipped (its start conditions failed), so
                            // the action never started and would hold the phase it ends in for ever (MQ105's
                            // EinarthTeachScene, a hold package from phase 1 to 4 with phases 1 and 2 skipped). A package
                            // is a state for the span: it starts in the first phase of the span that runs. A line or
                            // timer is part of the skipped phase: it is left out.
                            if (a.mType == SceneActionType::Package)
                                in.mActions[i].mStage = ActionStage::Running;
                            else
                            {
                                in.mActions[i].mStage = ActionStage::Done;
                                mDone[id].insert(a.mIndex);
                            }
                        }
                    }
                }
                // advance the running actions
                for (std::size_t i = 0; i < record.mActions.size(); ++i)
                {
                    const SceneAction& a = record.mActions[i];
                    ActionState& st = in.mActions[i];
                    if (st.mStage != ActionStage::Running)
                        continue;
                    switch (a.mType)
                    {
                        case SceneActionType::Dialogue:
                        {
                            if (st.mDuration == 0.f)
                            {
                                if (a.mTopic == kNullFormId)
                                {
                                    st.mStage = ActionStage::Done;
                                    ++mStats.mLinesSilent;
                                    break;
                                }
                                st.mActor = mEnv.aliasReference(record.mQuest, a.mAlias);
                                if (st.mActor == kNullFormId)
                                {
                                    // nobody fills the alias: nothing to wait for
                                    st.mStage = ActionStage::Done;
                                    ++mStats.mLinesSilent;
                                    break;
                                }
                                st.mRetry -= step;
                                if (st.mRetry > 0.f && st.mParked > 0.f)
                                {
                                    st.mParked += step;
                                }
                                else
                                {
                                    SceneLine line;
                                    bool resolved
                                        = st.mActor != kNullFormId && mEnv.resolveLine(id, a.mTopic, st.mActor, line);
                                    if (st.mActor != kNullFormId && !resolved)
                                    {
                                        st.mStage = ActionStage::Done;
                                        ++mStats.mLinesSilent;
                                        break;
                                    }
                                    if (resolved && mEnv.speak(st.mActor, line))
                                    {
                                        st.mDuration
                                            = std::max(mOptions.mMinLineSeconds,
                                                  static_cast<float>(line.mText.size()) * mOptions.mSecondsPerCharacter)
                                            + mOptions.mLineGapSeconds;
                                        st.mElapsed = 0.f;
                                        ++mStats.mLines;
                                        mEnv.log("SCENE " + hex(id) + " line " + std::to_string(a.mIndex) + " by "
                                            + hex(st.mActor) + ": " + line.mText);
                                    }
                                    else
                                    {
                                        if (!st.mParkCounted)
                                        {
                                            st.mParkCounted = true;
                                            ++mStats.mParked;
                                            mEnv.log("SCENE " + hex(id) + " line " + std::to_string(a.mIndex)
                                                + " waits for " + hex(st.mActor));
                                        }
                                        st.mParked += step;
                                        st.mRetry = mOptions.mRetrySeconds;
                                        if (st.mParked >= mOptions.mParkSeconds)
                                        {
                                            st.mStage = ActionStage::Done;
                                            ++mStats.mParkExpired;
                                        }
                                    }
                                }
                                break;
                            }
                            st.mElapsed += step;
                            if (st.mElapsed >= st.mDuration)
                                st.mStage = ActionStage::Done;
                            break;
                        }
                        case SceneActionType::Package:
                        {
                            if (!st.mPackageGiven)
                            {
                                st.mActor = mEnv.aliasReference(record.mQuest, a.mAlias);
                                if (st.mActor == kNullFormId || a.mPackages.empty())
                                {
                                    st.mStage = ActionStage::Done;
                                    break;
                                }
                                st.mPackageGiven = mEnv.setPackage(st.mActor, a.mPackages.front());
                                if (!st.mPackageGiven)
                                {
                                    st.mStage = ActionStage::Done;
                                    break;
                                }
                                ++mStats.mPackages;
                                mEnv.log("SCENE " + hex(id) + " package " + hex(a.mPackages.front()) + " for "
                                    + hex(st.mActor));
                            }
                            st.mElapsed += step;
                            if (mEnv.packageFinished(st.mActor) || st.mElapsed >= mOptions.mPackageSeconds)
                            {
                                mEnv.clearPackage(st.mActor);
                                st.mPackageGiven = false;
                                st.mStage = ActionStage::Done;
                            }
                            break;
                        }
                        case SceneActionType::Timer:
                            if (st.mElapsed == 0.f && st.mDuration == 0.f)
                            {
                                st.mDuration = a.mHasSeconds ? std::max(0.f, a.mSeconds) : 0.f;
                                ++mStats.mTimers;
                                if (st.mDuration == 0.f)
                                    st.mDuration = 1e-6f;
                            }
                            st.mElapsed += step;
                            if (st.mElapsed >= st.mDuration)
                                st.mStage = ActionStage::Done;
                            break;
                        default:
                            st.mStage = ActionStage::Done;
                            break;
                    }
                    if (st.mStage == ActionStage::Done)
                        mDone[id].insert(a.mIndex);
                }
                step = 0.f;
                // does the phase end?
                const ScenePhase& phase = record.mPhases[static_cast<std::size_t>(in.mPhase)];
                bool actionsDone = true;
                for (std::size_t i = 0; i < record.mActions.size(); ++i)
                    if (record.mActions[i].mEndPhase == static_cast<std::uint32_t>(in.mPhase)
                        && in.mActions[i].mStage != ActionStage::Done)
                        actionsDone = false;
                bool conditionsDone = true;
                if (!phase.mCompletionConditions.empty())
                    conditionsDone
                        = phase.mConditionsOk && mEnv.evaluate(phase.mCompletionConditions, id, record.mQuest);
                bool complete = false;
                switch (mOptions.mPhaseEnd)
                {
                    case PhaseEndRule::ActionsAndConditions:
                        complete = actionsDone && conditionsDone;
                        break;
                    case PhaseEndRule::ActionsOnly:
                        complete = actionsDone;
                        break;
                    case PhaseEndRule::ConditionsOnly:
                        complete = conditionsDone;
                        break;
                    case PhaseEndRule::ConditionsWhenPresent: // task 0329
                        complete = phase.mCompletionConditions.empty() ? actionsDone : conditionsDone;
                        break;
                }
                if (!complete && sSceneWaitTrace && ++in.mNote % 300 == 0)
                {
                    mEnv.log("SCENEWAIT " + hex(id) + " phase " + std::to_string(in.mPhase) + " actions "
                        + (actionsDone ? "done" : "pending") + " conditions " + (conditionsDone ? "ok" : "no"));
                }
                if (!complete)
                    break;
                // stop what still runs in this phase
                for (std::size_t i = 0; i < record.mActions.size(); ++i)
                {
                    ActionState& st = in.mActions[i];
                    if (record.mActions[i].mEndPhase == static_cast<std::uint32_t>(in.mPhase)
                        && st.mStage != ActionStage::Done)
                    {
                        if (st.mPackageGiven)
                        {
                            mEnv.clearPackage(st.mActor);
                            st.mPackageGiven = false;
                        }
                        st.mStage = ActionStage::Done;
                        mDone[id].insert(record.mActions[i].mIndex);
                    }
                }
                runPhaseFragments(mEnv, id, record, in.mPhase, true, mStats.mFragments);
                ++in.mPhase;
                in.mEntered = false;
            }
            if (ended)
                finish(id, true);
        }
    }
}
