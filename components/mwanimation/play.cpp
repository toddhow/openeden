#include "play.hpp"

#include <iostream>
#include <limits>

#include <components/animation/animset.hpp>
#include <components/animation/controllermap.hpp>
#include <components/animation/reset.hpp>
#include <components/misc/gameconventions.hpp>
#include <components/misc/strings/algorithm.hpp>

namespace
{
    float calcVelocity(const Anim::Tags& keys, const Anim::Channel<vsg::vec3>& channel, const vsg::vec3& accum,
        std::string_view groupname_)
    {
        std::string groupname(groupname_);
        const std::string start = groupname + ": start";
        const std::string loopstart = groupname + ": loop start";
        const std::string loopstop = groupname + ": loop stop";
        const std::string stop = groupname + ": stop";
        float starttime = std::numeric_limits<float>::max();
        float stoptime = 0.0f;

        // Pick the last Loop Stop key and the last Loop Start key.
        // This is required because of broken text keys in AshVampire.nif.
        // It has *two* WalkForward: Loop Stop keys at different times, the first one is used for stopping playback
        // but the animation velocity calculation uses the second one.
        // As result the animation velocity calculation is not correct, and this incorrect velocity must be replicated,
        // because otherwise the Creature's Speed (dagoth uthol) would not be sufficient to move fast enough.
        auto keyiter = keys.rbegin();
        while (keyiter != keys.rend())
        {
            if (keyiter->second == start || keyiter->second == loopstart)
            {
                starttime = keyiter->first;
                break;
            }
            ++keyiter;
        }
        keyiter = keys.rbegin();
        while (keyiter != keys.rend())
        {
            if (keyiter->second == stop)
                stoptime = keyiter->first;
            else if (keyiter->second == loopstop)
            {
                stoptime = keyiter->first;
                break;
            }
            ++keyiter;
        }

        if (stoptime > starttime)
        {
            auto startpos = channel.value(starttime) * accum;
            auto endpos = channel.value(stoptime) * accum;
            return vsg::length(startpos - endpos) / (stoptime - starttime);
        }
        return 0.0f;
    }

    const Anim::TransformController*& findCtrl(std::vector<Anim::TransformControllers>& vec, vsg::Object* needle)
    {
        for (auto& group : vec)
            for (auto& [ctrl, o] : group)
                if (o == needle)
                    return ctrl;
        throw std::runtime_error("!findCtrl");
    }
}

namespace MWAnim
{
    struct Play::AnimSource
    {
        vsg::ref_ptr<const Anim::ControllerMap> ref;
        // openeden: the source's group table (task 0124, stage C1); the group and text key queries answer from it.
        vsg::ref_ptr<const Anim::AnimSet> set;
        std::vector<Anim::TransformControllers> tcontrollers;
        vsg::ref_ptr<const Anim::Tags> tags;
        vsg::ref_ptr<Anim::Reset> reset;
        vsg::ref_ptr<vsg::Object> resetCtrl;
    };

    Play::Play(const Anim::Bones& in_bones, size_t boneGroupCount)
        : bones(in_bones)
    {
        mControllerGroups.resize(boneGroupCount);
    }

    Play::~Play() {}

    bool Play::isRotationControlled(Anim::Bone& bone) const
    {
        auto& group = mControllerGroups[bone.group];
        if (auto list = group.list)
        {
            auto node = bone.node();
            for (auto& [ctrl, o] : *list)
                if (o == node)
                    return ctrl->rotate.valid();
        }
        return false;
    }

    void Play::setAccumulation(const vsg::vec3& accum)
    {
        // Task 0124, stage C2: one value for both uses. It used to reach only the playing source's reset channel,
        // so the movement (updatePosition) and the velocity kept the previous axes while the bone showed the new ones,
        // and the next arbitration put the old axes back. OpenMW 0.51's Animation::setAccumulation does the same as
        // this.
        mAccumulate = accum;
        if (mResetAccum)
            mResetAccum->setAccumulate(accum);
    }

    void Play::addSingleAnimSource(vsg::ref_ptr<Anim::ControllerMap> map, const std::string& debugName)
    {
        auto anim = std::make_unique<AnimSource>();
        anim->ref = map;
        anim->tcontrollers.resize(mControllerGroups.size());
        Anim::Transform* bip01{};
        Anim::Transform* root{};
        const Misc::GameConventions& game = Misc::gameConventions(); // the root bone names are the game's (task 0085)
        for (auto& [target, ctrl] : map->map)
        {
            if (auto bone = this->bones.search(target))
            {
                anim->tcontrollers[bone->group].emplace_back(ctrl, bone->node());
                // An empty name means the game has no such bone; it must not match a bone with no name.
                if (!game.mRootBone.empty() && Misc::StringUtils::ciEqual(target, game.mRootBone))
                    bip01 = bone->node();
                else if (!game.mRootBoneFallback.empty() && Misc::StringUtils::ciEqual(target, game.mRootBoneFallback))
                    root = bone->node();
            }
            else
                std::cerr << "addAnimSource(" << debugName << "): can't find bone '" << target << "'" << std::endl;
        }
        if (!mAccumRoot)
            mAccumRoot = bip01;
        if (!mAccumRoot)
            mAccumRoot = root;
        if (mAccumRoot)
        {
            auto& replaceAccumCtrl = findCtrl(anim->tcontrollers, mAccumRoot);
            if (replaceAccumCtrl->translate)
            {
                auto resetCtrl = Anim::TransformController::create(*replaceAccumCtrl);
                resetCtrl->translate = anim->reset = Anim::make_channel<Anim::Reset>(replaceAccumCtrl->translate);
                anim->resetCtrl = resetCtrl;
                replaceAccumCtrl = resetCtrl;
            }
        }
        anim->set = Anim::AnimSet::forSource(map, debugName);
        anim->tags = anim->set->tags; // never null: a source with no text keys has an empty table
        mAnimSources.emplace_back(std::move(anim));
    }

    void Play::addAnimSources(
        const std::vector<vsg::ref_ptr<Anim::ControllerMap>>& sources, const std::string& debugName)
    {
        for (auto& anim : sources)
            addSingleAnimSource(anim, debugName);
    }

    bool Play::hasAnimation(std::string_view anim) const
    {
        for (const auto& src : mAnimSources)
            if (src->set->hasGroup(anim))
                return true;
        return false;
    }

    float Play::getStartTime(std::string_view groupname) const
    {
        for (AnimSourceList::const_reverse_iterator iter(mAnimSources.rbegin()); iter != mAnimSources.rend(); ++iter)
        {
            if (const Anim::AnimSet::Group* found = (*iter)->set->find(groupname))
                return found->firstKey;
        }
        return -1.f;
    }

    float Play::getTextKeyTime(std::string_view textKey) const
    {
        for (AnimSourceList::const_reverse_iterator iter(mAnimSources.rbegin()); iter != mAnimSources.rend(); ++iter)
        {
            const float time = (*iter)->set->textKeyTime(textKey);
            if (time != Anim::AnimSet::sAbsent)
                return time;
        }

        return -1.f;
    }

    void Play::handleTextKey(
        AnimState& state, std::string_view groupname, Anim::Tags::ConstIterator key, const Anim::Tags& map)
    {
        const std::string& evt = key->second;

        size_t off = groupname.size() + 2;
        size_t len = evt.size() - off;

        if (evt.compare(0, groupname.size(), groupname) == 0 && evt.compare(groupname.size(), 2, ": ") == 0)
        {
            if (evt.compare(off, len, "loop start") == 0)
                state.loopStartTime = key->first;
            else if (evt.compare(off, len, "loop stop") == 0)
                state.loopStopTime = key->first;
        }
        encounteredTags.push_back({ std::string(groupname), key, map });
    }

    float Play::AnimState::getCompletion() const
    {
        if (stopTime > startTime)
            return (time - startTime) / (stopTime - startTime);
        return playing ? 0.0f : 1.0f;
    }

    void Play::animationEnded(const AnimState& state)
    {
        endedAnimations.push_back(
            { state.groupname, state.time, state.getCompletion(), state.startKey, state.stopKey });
    }

    void Play::play(std::string_view groupname, const Priority& prio, int blendMask, bool autodisable, float speedmult,
        std::string_view start, std::string_view stop, float startpoint, uint32_t loops, bool loopfallback)
    {
        if (groupname.empty())
        {
            resetActiveGroups();
            return;
        }

        Priority priority = prio;
        if (priority.mPriority.size() <= mControllerGroups.size())
            priority.mPriority.resize(mControllerGroups.size(), priority[0]);

        // As in OpenMW 0.51: an already playing group takes the new priority, and only the other states that hold
        // this priority are replaced.
        AnimStateMap::iterator foundstateiter = mStates.find(groupname);
        if (foundstateiter != mStates.end())
            foundstateiter->second.priority = priority;

        AnimStateMap::iterator stateiter = mStates.begin();
        while (stateiter != mStates.end())
        {
            if (stateiter->second.priority == priority && stateiter->first != groupname)
            {
                animationEnded(stateiter->second);
                stateiter = mStates.erase(stateiter);
            }
            else
                ++stateiter;
        }

        if (foundstateiter != mStates.end())
        {
            resetActiveGroups();
            return;
        }

        /* Look in reverse; last-inserted source has priority. */
        AnimState state(mControllerGroups.size());
        for (auto iter = mAnimSources.rbegin(); iter != mAnimSources.rend(); ++iter)
        {
            const auto& textkeys = *((*iter)->tags);
            if (reset(state, textkeys, groupname, start, stop, startpoint, loopfallback))
            {
                state.source = iter->get();
                state.speedMult = speedmult;
                state.loopCount = loops;
                state.playing = (state.time < state.stopTime);
                state.priority = priority;
                state.blendMask = blendMask;
                state.autoDisable = autodisable;
                state.groupname = groupname;
                state.startKey = start;
                state.stopKey = stop;
                mStates.insert_or_assign(std::string(groupname), state);

                if (state.playing)
                {
                    auto textkey = textkeys.lowerBound(state.time);
                    while (textkey != textkeys.end() && textkey->first <= state.time)
                    {
                        handleTextKey(state, groupname, textkey, textkeys);
                        ++textkey;
                    }
                }

                if (state.time >= state.loopStopTime && state.loopCount > 0)
                {
                    state.loopCount--;
                    state.time = state.loopStartTime;
                    state.playing = true;
                    if (state.time >= state.loopStopTime)
                        break;
                    auto textkey = textkeys.lowerBound(state.time);
                    while (textkey != textkeys.end() && textkey->first <= state.time)
                    {
                        handleTextKey(state, groupname, textkey, textkeys);
                        ++textkey;
                    }
                }
                break;
            }
        }
        resetActiveGroups();
    }

    bool Play::reset(AnimState& state, const Anim::Tags& keys, std::string_view groupname_, std::string_view start_,
        std::string_view stop_, float startpoint, bool loopfallback)
    {
        std::string groupname(groupname_);
        std::string start(start_);
        std::string stop(stop_);

        // Look for text keys in reverse. This normally wouldn't matter, but for some reason undeadwolf_2.nif has two
        // separate walkforward keys, and the last one is supposed to be used.
        auto groupend = keys.rbegin();
        for (; groupend != keys.rend(); ++groupend)
        {
            if (groupend->second.compare(0, groupname.size(), groupname) == 0
                && groupend->second.compare(groupname.size(), 2, ": ") == 0)
                break;
        }

        std::string starttag = groupname + ": " + start;
        auto startkey = groupend;
        while (startkey != keys.rend() && startkey->second != starttag)
            ++startkey;
        if (startkey == keys.rend() && start == "loop start")
        {
            starttag = groupname + ": start";
            startkey = groupend;
            while (startkey != keys.rend() && startkey->second != starttag)
                ++startkey;
        }
        if (startkey == keys.rend())
            return false;

        const std::string stoptag = groupname + ": " + stop;
        auto stopkey = groupend;
        // We have to ignore extra garbage at the end.
        // The Scrib's idle3 animation has "Idle3: Stop." instead of "Idle3: Stop".
        // Why, just why? :(
        while (stopkey != keys.rend()
            && (stopkey->second.size() < stoptag.size() || stopkey->second.compare(0, stoptag.size(), stoptag) != 0))
            ++stopkey;
        if (stopkey == keys.rend())
            return false;

        if (startkey->first > stopkey->first)
            return false;

        state.startTime = startkey->first;
        if (loopfallback)
        {
            state.loopStartTime = startkey->first;
            state.loopStopTime = stopkey->first;
        }
        else
        {
            state.loopStartTime = startkey->first;
            state.loopStopTime = std::numeric_limits<float>::max();
        }
        state.stopTime = stopkey->first;

        state.time = state.startTime + ((state.stopTime - state.startTime) * startpoint);

        // mLoopStartTime and mLoopStopTime normally get assigned when encountering these keys while playing the
        // animation (see handleTextKey). But if startpoint is already past these keys, or start time is == stop time,
        // we need to assign them now.
        const std::string loopstarttag = groupname + ": loop start";
        const std::string loopstoptag = groupname + ": loop stop";

        auto key = groupend;
        for (; key != startkey && key != keys.rend(); ++key)
        {
            if (key->first > state.time)
                continue;

            if (key->second == loopstarttag)
                state.loopStartTime = key->first;
            else if (key->second == loopstoptag)
                state.loopStopTime = key->first;
        }

        return true;
    }

    void Play::resetActiveGroups()
    {
        mResetAccum = nullptr;
        for (size_t i = 0; i < mControllerGroups.size(); ++i)
        {
            mControllerGroups[i] = {};

            auto active = mStates.cend();
            for (auto state = mStates.cbegin(); state != mStates.cend(); ++state)
            {
                if (!(state->second.blendMask & (1 << i)))
                    continue;

                if (active == mStates.end() || active->second.priority[i] < state->second.priority[i])
                    active = state;
            }
            if (active != mStates.end())
            {
                auto animsrc = active->second.source;
                mControllerGroups[i] = { &animsrc->tcontrollers[i], &active->second.time };
                if (i == 0)
                {
                    mResetAccum = animsrc->reset;
                    if (mResetAccum)
                        mResetAccum->setAccumulate(mAccumulate);
                }
            }
        }
    }

    void Play::adjustSpeedMult(std::string_view groupname, float speedmult)
    {
        auto state(mStates.find(groupname));
        if (state != mStates.end())
            state->second.speedMult = speedmult;
    }

    bool Play::isPlaying(std::string_view groupname) const
    {
        auto state(mStates.find(groupname));
        if (state != mStates.end())
            return state->second.playing;
        return false;
    }

    bool Play::getInfo(std::string_view groupname, float* complete, float* speedmult, uint32_t* loopcount) const
    {
        auto iter = mStates.find(groupname);
        if (iter == mStates.end())
        {
            if (complete)
                *complete = 0.0f;
            if (speedmult)
                *speedmult = 0.0f;
            if (loopcount)
                *loopcount = 0;
            return false;
        }

        if (complete)
            *complete = iter->second.getCompletion();
        if (speedmult)
            *speedmult = iter->second.speedMult;
        if (loopcount)
            *loopcount = iter->second.loopCount;
        return true;
    }

    std::string_view Play::getActiveGroup(size_t boneGroup) const
    {
        if (boneGroup >= mControllerGroups.size())
            return {};
        if (const float* time = mControllerGroups[boneGroup].time)
            for (const auto& [name, state] : mStates)
                if (&state.time == time)
                    return name;
        return {};
    }

    Play::ActiveState Play::getActiveState(size_t boneGroup) const
    {
        if (boneGroup >= mControllerGroups.size())
            return {};
        if (const float* time = mControllerGroups[boneGroup].time)
            for (const auto& [name, state] : mStates)
                if (&state.time == time)
                    return { state.source != nullptr ? state.source->ref.get() : nullptr,
                        state.source != nullptr ? state.source->set.get() : nullptr, name, state.time };
        return {};
    }

    float Play::getCurrentTime(std::string_view groupname) const
    {
        auto iter = mStates.find(groupname);
        if (iter == mStates.end())
            return -1.f;
        return iter->second.time;
    }

    uint32_t Play::getCurrentLoopCount(std::string_view groupname) const
    {
        auto iter = mStates.find(groupname);
        if (iter == mStates.end())
            return 0;
        return iter->second.loopCount;
    }

    void Play::disable(std::string_view groupname)
    {
        auto iter = mStates.find(groupname);
        if (iter != mStates.end())
        {
            animationEnded(iter->second);
            mStates.erase(iter);
        }
        resetActiveGroups();
    }

    float Play::getVelocity(std::string_view groupname) const
    {
        if (!mAccumRoot)
            return 0.0f;

        auto found = mAnimVelocities.find(groupname);
        if (found != mAnimVelocities.end())
            return found->second;

        // Look in reverse; last-inserted source has priority.
        auto animsrc(mAnimSources.rbegin());
        for (; animsrc != mAnimSources.rend(); ++animsrc)
        {
            const auto& keys = *((*animsrc)->tags);
            if (keys.hasGroup(groupname))
                break;
        }
        if (animsrc == mAnimSources.rend())
            return 0.0f;

        float velocity = 0.0f;
        const auto& keys = *((*animsrc)->tags);
        if (auto reset = (*animsrc)->reset)
            velocity = calcVelocity(keys, *reset->input, mAccumulate, groupname);

        // If there's no velocity, keep looking
        if (!(velocity > 1.0f))
        {
            AnimSourceList::const_reverse_iterator animiter = mAnimSources.rbegin();
            while (*animiter != *animsrc)
                ++animiter;

            while (!(velocity > 1.0f) && ++animiter != mAnimSources.rend())
            {
                const auto& keys2 = *((*animiter)->tags);
                if (auto reset = (*animsrc)->reset)
                    velocity = calcVelocity(keys2, *reset->input, mAccumulate, groupname);
            }
        }
        mAnimVelocities.emplace(groupname, velocity);
        return velocity;
    }

    void Play::updatePosition(float oldtime, float newtime, vsg::vec3& position)
    {
        // Get the difference from the last update, and move the position
        auto& accumChannel = *mResetAccum->input;
        vsg::vec3 off = accumChannel.value(newtime) * mAccumulate;
        position += off - accumChannel.value(oldtime) * mAccumulate;
    }

    vsg::vec3 Play::runAnimation(float duration)
    {
        vsg::vec3 movement(0.f, 0.f, 0.f);
        AnimStateMap::iterator stateiter = mStates.begin();
        while (stateiter != mStates.end())
        {
            AnimState& state = stateiter->second;
            if (persistentPriority != -1 && !state.priority.contains(persistentPriority))
            {
                ++stateiter;
                continue;
            }

            const auto& textkeys = *state.source->tags;
            auto textkey = textkeys.upperBound(state.time);
            float timepassed = duration * state.speedMult;
            while (state.playing)
            {
                if (!state.shouldLoop())
                {
                    float targetTime = state.time + timepassed;
                    if (textkey == textkeys.end() || textkey->first > targetTime)
                    {
                        if (mResetAccum && &state.time == mControllerGroups[0].time)
                            updatePosition(state.time, targetTime, movement);
                        state.time = std::min(targetTime, state.stopTime);
                    }
                    else
                    {
                        if (mResetAccum && &state.time == mControllerGroups[0].time)
                            updatePosition(state.time, textkey->first, movement);
                        state.time = textkey->first;
                    }

                    state.playing = (state.time < state.stopTime);
                    timepassed = targetTime - state.time;

                    while (textkey != textkeys.end() && textkey->first <= state.time)
                    {
                        handleTextKey(state, stateiter->first, textkey, textkeys);
                        ++textkey;
                    }
                }
                if (state.shouldLoop())
                {
                    state.loopCount--;
                    state.time = state.loopStartTime;
                    state.playing = true;

                    textkey = textkeys.lowerBound(state.time);
                    while (textkey != textkeys.end() && textkey->first <= state.time)
                    {
                        handleTextKey(state, stateiter->first, textkey, textkeys);
                        ++textkey;
                    }
                    if (state.time >= state.loopStopTime)
                        break;
                }

                if (timepassed <= 0.0f)
                    break;
            }

            if (!state.playing && state.autoDisable)
            {
                animationEnded(state);
                stateiter = mStates.erase(stateiter);
                resetActiveGroups();
            }
            else
                ++stateiter;
        }
        if (updateBones)
            for (auto& group : mControllerGroups)
                group.update();
        return movement;
    }

    void Play::setLoopingEnabled(std::string_view groupname, bool enabled)
    {
        auto state(mStates.find(groupname));
        if (state != mStates.end())
            state->second.loopingEnabled = enabled;
    }

    bool Play::isPriorityActive(int priority) const
    {
        for (const auto& [key, val] : mStates)
        {
            if (val.priority.contains(priority))
                return true;
        }
        return false;
    }

    void Play::listAnimationGroups(std::function<void(const std::string&)> handler) const
    {
        for (const auto& src : mAnimSources)
        {
            for (const auto& [name, group] : src->set->groups())
                handler(name);
        }
    }
}
