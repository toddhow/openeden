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
        // The capsule radius (task 0246, the last number of a row): half the smaller horizontal extent of the skin
        // model's mid-body (the vertices between 20 and 60 percent of its height, rest pose), at least
        // kCreatureMinRadius and at most 45 percent of the height: wolf 14.0 -> 16, dog 14.2 -> 16, bear 67.9, skeever
        // 13.1 -> 16, horse 31.6, sabre cat 32.3, deer 34.2, goat 13.8 -> 16, mammoth 100.3, giant 32.0
        // (`SkyrimCreaturesThirdSurvey.boundsOfEverySkinForTheRadius`, docs/tasks/0246-skyrim-creatures-third.md).
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
                "AttackPowerStanding", "Recoil", "StaggerBackSmall", "Death", 16.f },
            // The dog shares the canine folder and the stride numbers of the wolf (WalkForward00 and RunForward00 of
            // dogproject are the same blocks) and has its own project and a plain idle.
            { "dog", "actors/canine/character assets dog/", "dogproject", "mt_idle", "walkforward", "runforward",
                89.448f / 1.2f, 300.081f / 0.533333f, 89.448f, 300.081f, 76.f, "attack1", "attackpowerstanding",
                "recoil", "staggerbacksmall", "death", 16.f },
            { "bear", "actors/bear/character assets/", "bearproject", "mt_idle", "walkforward", "runforward",
                99.7136f / 1.666667f, 382.846f / 0.6f, 99.7136f, 382.846f, 192.f, "attack1", "attackpowerstanding",
                "recoil", "staggerbacksmall", "death", 68.f },
            { "skeever", "actors/skeever/character assets/", "skeeverproject", "mt_idle", "walkforward", "runforward",
                37.3473f / 1.f, 260.128f / 0.533333f, 37.3473f, 260.128f, 57.f, "attack1", "attackpowerforward",
                "recoil", "staggerbacksmall", "staggerbacklarge", 16.f },
            // The horse's idle is `idle`; its first attack is the rear-up, the second a quick kick.
            { "horse", "actors/horse/character assets/", "horseproject", "idle", "walkforward", "runforward",
                137.624f / 1.1f, 212.32f / 0.466667f, 137.624f, 212.32f, 169.f, "attack2", "attack1", "recoil2",
                "staggerbacksmall", "staggerbacklarge", 32.f },
            { "sabrecat", "actors/sabrecat/character assets/", "sabrecatproject", "mt_idle", "walkforward",
                "runforward", 162.094f / 1.433333f, 359.399f / 0.733333f, 162.094f, 359.399f, 117.f, "attack1",
                "attackpowerforward", "recoil", "staggerbacksmall", "staggerbacklarge", 32.f },
            // The deer has two attacks and no power attack: the second plays as the power attack.
            { "deer", "actors/deer/character assets/", "deerproject", "mt_idle", "walkforward", "runforward",
                169.802f / 1.f, 500.f / 0.6f, 169.802f, 500.f, 214.f, "attack1", "attack2", "recoil",
                "staggerbacksmall", "staggerbacklarge", 34.f },
            { "goat", "actors/goat/character assets/", "goatproject", "mt_idle", "walkforward", "runforward",
                62.1303f / 1.f, 360.f / 1.f, 62.1303f, 360.f, 82.f, "attack1", "attackpowerstanding", "recoil",
                "staggerbacksmall", "staggerbacklarge", 16.f },
            { "mammoth", "actors/mammoth/character assets/", "mammothproject", "mt_idle", "walkforward", "runforward",
                247.36f / 4.f, 400.f / 1.f, 247.36f, 400.f, 376.f, "attack1", "attackpowerstanding", "recoil",
                "staggerbacksmall", "deathpose", 100.f },
            // The giant's generators carry other names than its files (`attack1` is `attack1_start`/`attack1_end`), and
            // the pack reads events by the file's name, so the attacks are the two clips whose names agree: the sweep
            // of both fists `attack1x2` and the stomp. The plain run is the combat run (415 units in 1 s).
            { "giant", "actors/giant/character assets/", "giantproject", "idle", "walkforward", "runforward",
                164.134f / 2.666667f, 415.f / 1.f, 164.134f, 415.f, 267.f, "attack1x2", "attack_stomp",
                "stagger_small", "stagger_large", "deathpose", 32.f },
            // ---- task 0246: the third slice. Each number is from docs/tasks/0246-skyrim-creatures-third.md section 2.
            // The draugr row takes the three skeletons of the draugr folder (skeleton, skeletonf, skeletons: a prefix of
            // the file name, not a folder), so a skeleton of a DLC subfolder is not matched. The skeleton race has its own
            // project file with the same generators and blocks (410 and 214, sizes differ by the name only).
            { "draugr", "actors/draugr/character assets/skeleton", "draugrproject", "mtidle", "mtforward",
                "mtfastforward", 76.9733f / 1.f, 180.921f / 0.533333f, 76.9733f, 180.921f, 145.f, "1hmattacka",
                "1hmattackpowerslash", "1hmrecoil", "mtstaggermedium", "special_deathbackward", 18.f },
            // The falmer's attack files are played by generators named `H2H_Attack1_Clip` and `H2H_Attack2_Clip`
            // (SkyrimClipMeta::sidecarAliases). It has no death clip: the heavy stagger stands in.
            { "falmer", "actors/falmer/character assets/", "falmerproject", "mtidle", "mtwalkforward", "mtrunforward",
                167.438f / 1.666667f, 191.458f / 0.533333f, 167.438f, 191.458f, 145.f, "h2hattack1", "h2hattack2",
                "h2hrecoilright", "1hmstaggerbackmid", "1hmstaggerbackheavy", 21.f },
            // The troll's walk and run files have no generator of their own name: the blocks of the same duration
            // (MTForward 1.16667 s, MTFastForward 0.666667 s). No death clip: the third back stagger stands in.
            { "troll", "actors/troll/character assets/", "trollproject", "mtidle_standing", "mtwalkforward",
                "mtrunforward", 116.63f / 1.166667f, 179.669f / 0.666667f, 116.63f, 179.669f, 173.f, "h2hattackrighta",
                "h2hattackrightforwardpower", "h2hrecoilrighta", "staggerbacka", "staggerbackc", 20.f },
            // The capsule radius of the spider is cut to 45 percent of its height (the body is 136 wide and 98 high).
            { "frostbitespider", "actors/frostbitespider/character assets/", "frostbitespiderproject", "mainidle",
                "forward_walk", "forward_run", 85.09f / 1.f, 300.2f / 1.f, 85.09f, 300.2f, 98.f, "attack_rightchop",
                "attack_combochop", "recoil_bite", "stagger_small", "death", 44.f },
            { "chaurus", "actors/chaurus/character assets/", "chaurusproject", "mainidle", "forwardwalk", "forwardrun",
                142.513f / 1.666667f, 350.265f / 1.f, 142.513f, 350.265f, 124.f, "attackrcbite", "attackrheadbash",
                "recoilrbite", "staggersmall", "death", 46.f },
            // The hagraven has no death clip (the large stagger stands in); its run covers little ground per cycle.
            { "hagraven", "actors/hagraven/character assets/", "hagravenproject", "idle", "walkforward", "runforward",
                90.9832f / 1.333333f, 89.1586f / 0.766667f, 90.9832f, 89.1586f, 128.f, "attackr", "attackl", "recoilr",
                "staggersmall", "staggerlarge", 17.f },
            // The spriggan's project is `spriggan` (animationdata/spriggan.txt). Its measured radius is 10.5 (a thin
            // body): the minimum applies.
            { "spriggan", "actors/spriggan/character assets/", "spriggan", "mt_idle", "walkforward", "runforward",
                95.7992f / 1.466667f, 263.173f / 0.733333f, 95.7992f, 263.173f, 156.f, "attackright1",
                "powerattackforwardx2", "recoilr", "staggerback_small", "death", 16.f },
            // The dragon (task 0262, moved here from dragons.cpp; docs/tasks/0253-skyrim-dragons.md section 1). Clips:
            // `CLIP_Ground_Combat_Default_Standing_Idle` (ground_combatidle), `CLIP_Ground_Locomotion_Forward`
            // (mtforwardground, 1.33333 s, motion 512 units), the bite (3 s, HitFrame 1.5667 s), the tail whip (2.66667 s,
            // HitFrame 1.4 s), `CLIP_Ground_Stagger`; the death is mtdeathintro. Height 414 is the z extent of dragon.nif
            // at rest; the radius 150 is OURS (the skin's mid-body slab includes the spread wings: 1675 wide).
            { "dragon", "actors/dragon/character assets/skeleton.nif", "dragonproject", "ground_combatidle",
                "mtforwardground", "mtforwardground", 512.f / 1.33333f, 512.f / 1.33333f, 512.f, 512.f, 414.f,
                "ground_bite", "ground_tailwhip", "", "mtland_staggerheavy", "mtdeathintro", 150.f },
        };

        // The rows of tasks 0212 and 0216 come first; creatureProfiles() is exactly these.
        constexpr std::size_t kEarlierCount = 10;
        static_assert(std::size(kProfiles) > kEarlierCount);

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
        return std::span<const CreatureProfile>(kProfiles, kEarlierCount);
    }

    std::span<const CreatureProfile> creatureProfilesAll()
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
