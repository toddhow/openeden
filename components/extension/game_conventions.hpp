#ifndef OPENEDEN_EXTENSION_GAME_CONVENTIONS_H
#define OPENEDEN_EXTENSION_GAME_CONVENTIONS_H

// Copies the values of a pack's `openeden.game` contract (version 1.1 and later) into the engine's convention
// snapshot (components/misc/gameconventions.hpp), which generic code reads (task 0085, move 1). Values are copied, so
// nothing refers to the contract object after this returns.

#include <openeden/contracts/game_contract.hpp>

#include <components/misc/gameconventions.hpp>

namespace openeden::extension
{
    inline Misc::GameConventions gameConventionsFrom(const openeden::contracts::GameContract& contract)
    {
        // A value the pack leaves empty (the contract's "not specified") keeps the engine's default.
        Misc::GameConventions result;
        if (contract.getUnitsPerMeter() > 0.0f)
            result.mUnitsPerMeter = contract.getUnitsPerMeter();
        const auto bones = contract.getHumanoidBoneNames();
        const auto take = [](std::string& target, std::string_view value) {
            if (!value.empty())
                target = std::string(value);
        };
        take(result.mRootBone, bones.root);
        take(result.mRootBoneFallback, bones.rootFallback);
        take(result.mHeadBone, bones.head);
        take(result.mSpine1Bone, bones.spine1);
        take(result.mSpine2Bone, bones.spine2);
        const auto prefixes = contract.getSkeletonModelNamePrefixes();
        if (!prefixes.empty())
        {
            result.mSkeletonModelPrefixes.clear();
            for (const std::string_view prefix : prefixes)
                result.mSkeletonModelPrefixes.emplace_back(prefix);
        }
        take(result.mFirstPersonBodyPartSuffix, contract.getFirstPersonBodyPartSuffix());
        take(result.mFirstPersonBaseAnimationFile, contract.getFirstPersonBaseAnimationFile());
        return result;
    }
}

#endif
