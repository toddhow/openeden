#ifndef OPENEDEN_EXTENSION_GAME_CONVENTIONS_H
#define OPENEDEN_EXTENSION_GAME_CONVENTIONS_H

// Copies the values of a pack's `openeden.game` contract (version 1.1 and later) into the engine's convention
// snapshot (components/misc/gameconventions.hpp), which generic code reads (task 0085, move 1). Values are copied, so
// nothing refers to the contract object after this returns.

#include <openeden/contracts/game_contract.hpp>

#include <components/misc/gameconventions.hpp>

#include <cmath>
#include <string>
#include <string_view>

namespace openeden::extension
{
    namespace detail
    {
        // The engine looks bones and model files up by lower-cased name, so a value with capitals could never match.
        inline std::string lowerAscii(std::string_view value)
        {
            std::string result(value);
            for (char& c : result)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            return result;
        }
    }

    /// A pack whose contract returns specifiesAllConventions() == true answers every convention itself: an empty
    /// bone name, prefix list, suffix or file means "this game has none" and is copied as it is (Skyrim has no
    /// first-person body part suffix and no fallback root bone). Any other pack (written for contract 1.0) leaves
    /// the values it does not answer empty, and the engine keeps its own default for those. In both cases the unit
    /// must be finite and positive, else the default stays, and bone names and prefixes are lower-cased.
    inline Misc::GameConventions gameConventionsFrom(const openeden::contracts::GameContract& contract)
    {
        const bool explicitValues = contract.specifiesAllConventions();
        Misc::GameConventions result;
        const float unit = contract.getUnitsPerMeter();
        if (std::isfinite(unit) && unit > 0.0f)
            result.mUnitsPerMeter = unit;
        const auto bones = contract.getHumanoidBoneNames();
        const auto take = [explicitValues](std::string& target, std::string_view value, bool lowerCase) {
            if (explicitValues || !value.empty())
                target = lowerCase ? detail::lowerAscii(value) : std::string(value);
        };
        take(result.mRootBone, bones.root, true);
        take(result.mRootBoneFallback, bones.rootFallback, true);
        take(result.mHeadBone, bones.head, true);
        take(result.mSpine1Bone, bones.spine1, true);
        take(result.mSpine2Bone, bones.spine2, true);
        const auto prefixes = contract.getSkeletonModelNamePrefixes();
        if (explicitValues || !prefixes.empty())
        {
            result.mSkeletonModelPrefixes.clear();
            for (const std::string_view prefix : prefixes)
                if (!prefix.empty())
                    result.mSkeletonModelPrefixes.push_back(detail::lowerAscii(prefix));
        }
        take(result.mFirstPersonBodyPartSuffix, contract.getFirstPersonBodyPartSuffix(), false);
        // A file name or path as the game's animation context names it; not lower-cased (paths are looked up by the
        // VFS).
        take(result.mFirstPersonBaseAnimationFile, contract.getFirstPersonBaseAnimationFile(), false);
        return result;
    }
}

#endif
