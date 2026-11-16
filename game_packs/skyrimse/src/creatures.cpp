#include "game_skyrimse/creatures.hpp"

#include <algorithm>
#include <string>

namespace game_skyrimse
{
    namespace
    {
        // Strides and durations from meshes/animationdata/boundanims/anims_wolfproject.txt (the blocks of the cache
        // indices of WalkForward00_Wolf = 67 and RunForward = 46).
        constexpr float kWolfWalkStride = 89.448f;
        constexpr float kWolfWalkDuration = 1.2f;
        constexpr float kWolfRunStride = 300.081f;
        constexpr float kWolfRunDuration = 0.533333f;

        const CreatureProfile sWolf{
            "wolf",
            "actors/canine/character assets wolf/",
            "wolfproject",
            "mt_idle_wolf",
            "walkforward_wolf",
            "runforward",
            kWolfWalkStride / kWolfWalkDuration,
            kWolfRunStride / kWolfRunDuration,
            kWolfWalkStride,
            kWolfRunStride,
            80.f,
            "Attack1",
            "AttackPowerStanding",
            "Recoil",
            "StaggerBackSmall",
            "Death",
        };

        std::string normalised(std::string_view path)
        {
            std::string out(path);
            for (char& c : out)
            {
                if (c == '\\')
                    c = '/';
                else if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + 32);
            }
            return out;
        }
    }

    const CreatureProfile& wolfProfile()
    {
        return sWolf;
    }

    const CreatureProfile* creatureProfileFor(std::string_view skeletonPath)
    {
        const std::string path = normalised(skeletonPath);
        // The model root is optional (a path may come with or without it).
        const std::string_view folder = sWolf.mSkeletonFolder;
        const std::size_t at = path.find(folder);
        if (at != std::string::npos && (at == 0 || path.compare(0, at, "meshes/") == 0))
            return &sWolf;
        return nullptr;
    }
}
