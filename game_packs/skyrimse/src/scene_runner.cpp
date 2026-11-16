#include "game_skyrimse/scene_runner.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

// Rules: see the header (task 0194). All of them are RECALLED-VERIFY and named options.

namespace game_skyrimse
{
    namespace
    {
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
        if (!force && !record->mConditions.empty() && !mEnv.evaluate(record->mConditions, scene, record->mQuest))
        {
            ++mStats.mRefused;
            return false;
        }
        auto instance = std::make_unique<Instance>();
        instance->mId = scene;
        instance->mRecord = record;
        instance->mActions.resize(record->mActions.size());
        instance->mHeld = held;
        mDone[scene].clear();
        for (const FormId ref : held)
            mEnv.hold(ref, true);
        mPlaying[scene] = std::move(instance);
        ++mStats.mStarted;
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
            if (record != nullptr && (record->mFlags & SceneFlags::BeginOnQuestStart) != 0 && start(id))
                ++started;
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

    namespace
    {
        void runPhaseFragments(SceneEnvironment& env, FormId scene, const SceneRecord& record, int phase, bool completion,
            std::uint64_t& counter)
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
                        break;
                    in.mEntered = true;
                    ++mStats.mPhases;
                    mEnv.log("SCENE " + hex(id) + " phase " + std::to_string(in.mPhase));
                    runPhaseFragments(mEnv, id, record, in.mPhase, false, mStats.mFragments);
                    for (std::size_t i = 0; i < record.mActions.size(); ++i)
                        if (record.mActions[i].mStartPhase == static_cast<std::uint32_t>(in.mPhase)
                            && in.mActions[i].mStage == ActionStage::Waiting)
                            in.mActions[i].mStage = ActionStage::Running;
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
                                st.mRetry -= step;
                                if (st.mRetry > 0.f && st.mParked > 0.f)
                                {
                                    st.mParked += step;
                                }
                                else
                                {
                                    SceneLine line;
                                    bool resolved = st.mActor != kNullFormId
                                        && mEnv.resolveLine(id, a.mTopic, st.mActor, line);
                                    if (st.mActor != kNullFormId && !resolved)
                                    {
                                        st.mStage = ActionStage::Done;
                                        ++mStats.mLinesSilent;
                                        break;
                                    }
                                    if (resolved && mEnv.speak(st.mActor, line))
                                    {
                                        st.mDuration = std::max(mOptions.mMinLineSeconds,
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
                    conditionsDone = phase.mConditionsOk && mEnv.evaluate(phase.mCompletionConditions, id, record.mQuest);
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
