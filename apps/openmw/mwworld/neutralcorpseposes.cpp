#include "neutralcorpseposes.hpp"

#include <algorithm>
#include <cmath>

namespace MWWorld
{
    namespace
    {
        bool finite(const MWPhysics::RagdollBoneState& state)
        {
            for (double v : state.mPosition)
                if (!std::isfinite(v))
                    return false;
            for (double v : state.mRotation)
                if (!std::isfinite(v))
                    return false;
            return true;
        }
    }

    bool NeutralCorpsePoses::put(const std::string& gameId, std::uint32_t placement, std::span<const std::string> names,
        std::span<const MWPhysics::RagdollBoneState> states)
    {
        if (gameId.empty() || names.empty() || names.size() > kNeutralSaveMaxPoseBones || states.size() < names.size())
            return false;
        NeutralCorpsePose pose;
        pose.mGameId = gameId;
        pose.mPlacement = placement;
        pose.mBones.reserve(names.size());
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            if (names[i].empty() || names[i].size() > kNeutralSaveMaxPoseName || !finite(states[i]))
                return false;
            NeutralCorpseBone bone;
            bone.mName = names[i];
            for (int k = 0; k < 3; ++k)
                bone.mPos[k] = static_cast<float>(states[i].mPosition[k]);
            for (int k = 0; k < 4; ++k)
                bone.mRot[k] = static_cast<float>(states[i].mRotation[k]);
            pose.mBones.push_back(std::move(bone));
        }
        const auto key = std::make_pair(gameId, placement);
        const auto found = mPoses.find(key);
        if (found == mPoses.end() && mPoses.size() >= kNeutralSaveMaxPoses)
            return false;
        if (found != mPoses.end())
            found->second = std::move(pose);
        else
            mPoses.emplace(key, std::move(pose));
        return true;
    }

    const NeutralCorpsePose* NeutralCorpsePoses::find(const std::string& gameId, std::uint32_t placement) const
    {
        const auto found = mPoses.find(std::make_pair(gameId, placement));
        return found == mPoses.end() ? nullptr : &found->second;
    }

    bool NeutralCorpsePoses::erase(const std::string& gameId, std::uint32_t placement)
    {
        return mPoses.erase(std::make_pair(gameId, placement)) != 0;
    }

    void NeutralCorpsePoses::load(std::span<const NeutralCorpsePose> poses)
    {
        mPoses.clear();
        for (const NeutralCorpsePose& pose : poses)
        {
            if (mPoses.size() >= kNeutralSaveMaxPoses)
                break;
            mPoses.emplace(std::make_pair(pose.mGameId, pose.mPlacement), pose);
        }
    }

    std::vector<NeutralCorpsePose> NeutralCorpsePoses::collect() const
    {
        std::vector<NeutralCorpsePose> result;
        result.reserve(mPoses.size());
        for (const auto& [key, pose] : mPoses)
            result.push_back(pose);
        return result;
    }

    bool NeutralCorpsePoses::toStates(const NeutralCorpsePose& pose, std::vector<std::string>& names,
        std::vector<MWPhysics::RagdollBoneState>& states)
    {
        if (pose.mBones.empty())
            return false;
        names.clear();
        states.clear();
        names.reserve(pose.mBones.size());
        states.reserve(pose.mBones.size());
        for (const NeutralCorpseBone& bone : pose.mBones)
        {
            names.push_back(bone.mName);
            MWPhysics::RagdollBoneState state;
            for (int k = 0; k < 3; ++k)
                state.mPosition[k] = static_cast<double>(bone.mPos[k]);
            for (int k = 0; k < 4; ++k)
                state.mRotation[k] = static_cast<double>(bone.mRot[k]);
            states.push_back(state);
        }
        return true;
    }
}
