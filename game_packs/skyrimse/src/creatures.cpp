#include "game_skyrimse/creatures.hpp"

#include <algorithm>
#include <iterator>
#include <string>

namespace game_skyrimse
{
    namespace
    {
        // The table of creatures (tasks 0212 and 0216). One row is one skeleton folder. How each number was found:
        //   folder   the skeleton path the RACE names, lower case, with the trailing slash;
        //   project  the suffix of the skeleton's folder + "project": meshes/animationdata/<project>.txt;
        //   clips    file names in the listing of Skyrim - Animations.bsa under actors/<folder>/animations; the
        //            walk and run are the files the behaviour graph's WalkForward and RunForward generators play (a
        //            generator named like the file stem links by name, the others by the duration of their
        //            root-motion block);
        //   strides  and speeds: the last translation row of the generator's block in
        //            meshes/animationdata/boundanims/anims_<project>.txt and its duration (speed = stride / duration,
        //            as for the wolf);
        //   height   the z extent of the skin model in its rest pose (the `SkyrimCreaturesMeasure` test of
        //            docs/tasks/0216-skyrim-creatures-more.md: wolf 79.6, dog 75.9, bear 191.8, skeever 57.4, horse
        //            169.0, sabre cat 117.2, deer 213.6, goat 81.8, mammoth 376.2, giant 267.3), rounded.
        // The capsule radius is the core's (22), not part of a row: a quadruped is longer than it is wide, and a
        // mammoth or a giant is wider than the core's capsule (decision for toddhow).
        //
        // ESTIMATES (the behaviour graphs that choose the clips are not decoded): which walk and run file is "the"
        // walk and run; the power attack of a creature with no standing power attack (a forward lunge clip plays in
        // place: its root motion is not applied); the death clip of a creature with no death file (the large stagger,
        // the clip its own sidecar's `Death` generator plays before the ragdoll the engine does not have for a
        // creature).
        const CreatureProfile kProfiles[] = {
            // The wolf (task 0212): WalkForward00_Wolf 89.448 units in 1.2 s, RunForward 300.081 in 0.533333 s.
            { "wolf", "actors/canine/character assets wolf/", "wolfproject", "mt_idle_wolf", "walkforward_wolf",
                "runforward", 89.448f / 1.2f, 300.081f / 0.533333f, 89.448f, 300.081f, 80.f, "Attack1",
                "AttackPowerStanding", "Recoil", "StaggerBackSmall", "Death" },
            // The dog shares the canine folder and the stride numbers of the wolf (WalkForward00 and RunForward00 of
            // dogproject are the same blocks) and has its own project and a plain idle.
            { "dog", "actors/canine/character assets dog/", "dogproject", "mt_idle", "walkforward", "runforward",
                89.448f / 1.2f, 300.081f / 0.533333f, 89.448f, 300.081f, 76.f, "attack1", "attackpowerstanding",
                "recoil", "staggerbacksmall", "death" },
            { "bear", "actors/bear/character assets/", "bearproject", "mt_idle", "walkforward", "runforward",
                99.7136f / 1.666667f, 382.846f / 0.6f, 99.7136f, 382.846f, 192.f, "attack1", "attackpowerstanding",
                "recoil", "staggerbacksmall", "death" },
            { "skeever", "actors/skeever/character assets/", "skeeverproject", "mt_idle", "walkforward", "runforward",
                37.3473f / 1.f, 260.128f / 0.533333f, 37.3473f, 260.128f, 57.f, "attack1", "attackpowerforward",
                "recoil", "staggerbacksmall", "staggerbacklarge" },
            // The horse's idle is `idle`; its first attack is the rear-up, the second a quick kick.
            { "horse", "actors/horse/character assets/", "horseproject", "idle", "walkforward", "runforward",
                137.624f / 1.1f, 212.32f / 0.466667f, 137.624f, 212.32f, 169.f, "attack2", "attack1", "recoil2",
                "staggerbacksmall", "staggerbacklarge" },
            { "sabrecat", "actors/sabrecat/character assets/", "sabrecatproject", "mt_idle", "walkforward",
                "runforward", 162.094f / 1.433333f, 359.399f / 0.733333f, 162.094f, 359.399f, 117.f, "attack1",
                "attackpowerforward", "recoil", "staggerbacksmall", "staggerbacklarge" },
            // The deer has two attacks and no power attack: the second plays as the power attack.
            { "deer", "actors/deer/character assets/", "deerproject", "mt_idle", "walkforward", "runforward",
                169.802f / 1.f, 500.f / 0.6f, 169.802f, 500.f, 214.f, "attack1", "attack2", "recoil",
                "staggerbacksmall", "staggerbacklarge" },
            { "goat", "actors/goat/character assets/", "goatproject", "mt_idle", "walkforward", "runforward",
                62.1303f / 1.f, 360.f / 1.f, 62.1303f, 360.f, 82.f, "attack1", "attackpowerstanding", "recoil",
                "staggerbacksmall", "staggerbacklarge" },
            { "mammoth", "actors/mammoth/character assets/", "mammothproject", "mt_idle", "walkforward", "runforward",
                247.36f / 4.f, 400.f / 1.f, 247.36f, 400.f, 376.f, "attack1", "attackpowerstanding", "recoil",
                "staggerbacksmall", "deathpose" },
            // The giant's generators carry other names than its files (`attack1` is `attack1_start`/`attack1_end`), and
            // the pack reads events by the file's name, so the attacks are the two clips whose names agree: the sweep
            // of both fists `attack1x2` and the stomp. The plain run is the combat run (415 units in 1 s).
            { "giant", "actors/giant/character assets/", "giantproject", "idle", "walkforward", "runforward",
                164.134f / 2.666667f, 415.f / 1.f, 164.134f, 415.f, 267.f, "attack1x2", "attack_stomp",
                "stagger_small", "stagger_large", "deathpose" },
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

    std::span<const CreatureProfile> creatureProfiles()
    {
        return std::span<const CreatureProfile>(kProfiles, std::size(kProfiles));
    }

    std::string_view creatureClipFor(const CreatureProfile& profile, openeden::sdk::game::ClipSlot slot)
    {
        using openeden::sdk::game::ClipSlot;
        switch (slot)
        {
            case ClipSlot::Attack:
                return profile.mAttack;
            case ClipSlot::PowerAttack:
                return profile.mPower;
            case ClipSlot::Recoil:
                return profile.mRecoil;
            case ClipSlot::Stagger:
                return profile.mStagger;
            case ClipSlot::Death:
                return profile.mDeath;
            default:
                return {};
        }
    }

    const CreatureProfile& wolfProfile()
    {
        return kProfiles[0];
    }

    const CreatureProfile* creatureProfileFor(std::string_view skeletonPath)
    {
        const std::string path = normalised(skeletonPath);
        for (const CreatureProfile& profile : kProfiles)
        {
            // The model root is optional (a path may come with or without it).
            const std::size_t at = path.find(profile.mSkeletonFolder);
            if (at != std::string::npos && (at == 0 || path.compare(0, at, "meshes/") == 0))
                return &profile;
        }
        return nullptr;
    }
}
