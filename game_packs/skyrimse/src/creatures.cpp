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
                164.134f / 2.666667f, 415.f / 1.f, 164.134f, 415.f, 267.f, "attack1x2", "attack_stomp", "stagger_small",
                "stagger_large", "deathpose", 32.f },
            // ---- task 0246: the third slice. Each number is from docs/tasks/0246-skyrim-creatures-third.md section 2.
            // The draugr row takes the three skeletons of the draugr folder (skeleton, skeletonf, skeletons: a prefix
            // of
            // the file name, not a folder), so a skeleton of a DLC subfolder is not matched. The skeleton race has its
            // own
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
            // (mtforwardground, 1.33333 s, motion 512 units), the bite (3 s, HitFrame 1.5667 s), the tail whip (2.66667
            // s,
            // HitFrame 1.4 s), `CLIP_Ground_Stagger`; the death is mtdeathintro. Height 414 is the z extent of
            // dragon.nif
            // at rest; the radius 150 is OURS (the skin's mid-body slab includes the spread wings: 1675 wide).
            { "dragon", "actors/dragon/character assets/skeleton.nif", "dragonproject", "ground_combatidle",
                "mtforwardground", "mtforwardground", 512.f / 1.33333f, 512.f / 1.33333f, 512.f, 512.f, 414.f,
                "ground_bite", "ground_tailwhip", "", "mtland_staggerheavy", "mtdeathintro", 150.f },
        };

        // Task 0290: the fourth slice, searched after the others (older tests pin the size of kProfiles). Numbers:
        // docs/tasks/0290-skyrim-beast-and-machine-animations.md section 2. The centurions' clips lie flat in
        // <actor folder>/animations (no sex folder), under names of their own.
        const CreatureProfile kFourthProfiles[] = {
            // Steam centurion: its sidecar names the cycles `DefaultForward` (the clip mtforward, 128 units in 1.33333
            // s) and `DefaultFastForward` (mtfastforward, 256 in 1.33333 s); the clip `death` stands for the death.
            // Height 305 and radius 76.7 from steamcenturion.nif (mid-body 153 by 164).
            { "dwarvensteamcenturion", "actors/dwarvensteamcenturion/character assets/", "steamproject", "mtidle",
                "mtforward", "mtfastforward", 128.f / 1.33333f, 256.f / 1.33333f, 128.f, 256.f, 305.f,
                "h2hattackleftslash", "h2hattackforwardpower", "h2hrecoilleft", "stagger_light", "death", 76.7f },
            // Sphere centurion: MT_Forward 192 units in 1 s, MT_FastForward 384 in 1 s (VERIFIED by the linked
            // blocks); no death clip (the heavy combat stagger stands in, as for the troll). Height 143 and radius
            // 46.5 from spherecenturion.nif (mid-body 93 by 106).
            { "dwarvenspherecenturion", "actors/dwarvenspherecenturion/character assets/", "spherecenturion", "mt_idle",
                "mt_forward", "mt_fastforward", 192.f / 1.f, 384.f / 1.f, 192.f, 384.f, 143.f, "h2h_attackslash",
                "h2h_attackforwardpowerchop", "h2h_recoil", "combat_staggerlight", "combat_staggerheavy", 46.5f },
            // The Solstheim boar riekling (DLC2; skeletonboar.nif). The installed data has NO sidecar for it, so
            // `boarproject` names a file that does not exist (the clip meta reader logs that once and the clips have no
            // events) and the strides are ESTIMATES: the front hoof travels 45.3 units along the body over walkforward
            // (1.06667 s) and 96.2 over runforward (0.533333 s) in the survey; a stride is that travel over the share
            // of the cycle the foot is on the ground (0.6 for the walk, 0.4 for the run: named guesses). Height 138
            // and radius 40.5 from boarriekling.nif.
            { "boarriekling", "actors/dlc02/boarriekling/character assets/", "boarproject", "mt_idle", "walkforward",
                "runforward", 75.55f / 1.06667f, 240.4f / 0.533333f, 75.55f, 240.4f, 138.f, "attack1",
                "attackpowerstanding", "recoil", "staggerbacksmall", "death", 40.5f },
        };

        // Task 0301: the fifth slice, searched last (older tests pin the sizes of the first two tables). Every row is
        // read as docs/tasks/0301-skyrim-creatures-rest-pose.md section 2 says: the clip files from the listing of
        // Skyrim - Animations.bsa under <actor folder>/animations (no sex folder); strides and durations from the
        // sidecar's blocks (meshes/animationdata/boundanims/anims_<project>.txt) where the project has one, else the
        // ESTIMATE marked on the row: a foot bone's peak to peak travel along the body over the cycle, divided by a
        // stance share of 0.6 (walk) or 0.4 (run), the method of the boar riekling (task 0290); heights are the skin
        // model's z extent (the top of the skin for a model that hovers above its root: icewraith, netch), radii half
        // the smaller mid-body width (at least kCreatureMinRadius; the core caps a radius at 45 percent of the
        // height). A row of a creature that never fights (hare, chicken) has no combat clips.
        const CreatureProfile kFifthProfiles[] = {
            // Sidecar: Forward_Walk 72.7391 and Forward_Run 145.865 units in 1 s. No death file: the back stagger.
            { "mudcrab", "actors/mudcrab/character assets/", "mudcrabproject", "mainidle", "forward_walk",
                "forward_run", 72.7391f / 1.f, 145.865f / 1.f, 72.7391f, 145.865f, 75.f, "attack_leftchop",
                "attack_combochop", "recoillchop", "stagger_small", "stagger_back", 34.8f },
            // Sidecar: Forward_Walk 52.2566 in 0.833333 s, Forward_Run 100.194 in 0.5 s. No combat clips.
            { "hare", "actors/ambient/hare/character assets/", "hareproject", "mt_idle", "walkforward", "runforward",
                52.2566f / 0.833333f, 100.194f / 0.5f, 52.2566f, 100.194f, 34.4f, "", "", "", "", "", 16.f },
            // Sidecar: Forward_Walk 34.7115 and Forward_Run 251.938 units in 1 s. No combat clips.
            { "chicken", "actors/ambient/chicken/character assets/", "chickenproject", "mt_idle", "walkforward",
                "runforward", 34.7115f / 1.f, 251.938f / 1.f, 34.7115f, 251.938f, 37.2f, "", "", "", "", "", 16.f },
            // Sidecar (highlandcowproject): WalkForward 129.727 in 2 s, RunForward 350.164 in 0.666667 s.
            { "cow", "actors/cow/character assets/", "highlandcowproject", "mt_idle", "walkforward", "runforward",
                129.727f / 2.f, 350.164f / 0.666667f, 129.727f, 350.164f, 112.8f, "attack1", "attackpowerstanding",
                "recoil", "staggerbacksmall", "deathpose", 29.1f },
            // Sidecar: WalkForward 49.1507 and RunForward 125.112 units in 1.5 s.
            { "horker", "actors/horker/character assets/", "horkerproject", "mt_idle", "walkforward", "runforward",
                49.1507f / 1.5f, 125.112f / 1.5f, 49.1507f, 125.112f, 74.5f, "attackleft1", "attackpowerstanding",
                "recoil", "staggerbacksmall", "death", 44.5f },
            // A swimmer: the walk is swim_forward (600 units in 3.33333 s) and the run swim_fastforward (600.1 in
            // 1.66667 s). The skin is 23.5 high (a capsule thinner than kCreatureMinRadius is capped by the core).
            { "slaughterfish", "actors/slaughterfish/character assets/", "slaughterfishproject", "mainidle",
                "swim_forward", "swim_fastforward", 600.f / 3.33333f, 600.1f / 1.66667f, 600.f, 600.1f, 23.5f,
                "attacklslash", "attackruppercut", "recoilright", "stagger", "stagger", 16.f },
            // It has one gait clip, runf (341 units in 1.06667 s, sidecar), used for the walk too: an ESTIMATE. The
            // skin hovers 70.4 above its root: 136 is the top of the skin.
            { "icewraith", "actors/icewraith/character assets/", "icewraithproject", "idle", "runf", "runf",
                341.f / 1.06667f, 341.f / 1.06667f, 341.f, 341.f, 136.f, "attack_power", "attack_grounddive", "recoil",
                "stagger", "staggerlarge", 18.2f },
            // The Solstheim riekling (the ordinary one, not the boar): NO sidecar. Foot excursion 59.5387 (mt_walkf)
            // and 71.1671 (mt_runf), over 0.6 and 0.4: ESTIMATED strides 99.23 and 177.92. No death clip: the large
            // back stagger. Its spear clip is used for the power attack only (the unarmed set has none).
            { "riekling", "actors/dlc02/riekling/character assets/", "rieklingproject", "mt_idle", "mt_walkf",
                "mt_runf", 99.23f / 1.f, 177.92f / 0.666667f, 99.23f, 177.92f, 95.6f, "mt_attack1",
                "spear_standingpowerattack", "mt_attack1_recoil", "mt_staggerb_small", "mt_staggerb_large", 16.f },
            // NO sidecar. Foot excursion 122.65 (walkforward) and 147.196 (runforward): ESTIMATED strides 204.42 and
            // 367.99, each over 1 s.
            { "scrib", "actors/dlc02/scrib/character assets/", "scribproject", "mt_idle", "walkforward", "runforward",
                204.42f / 1.f, 367.99f / 1.f, 204.42f, 367.99f, 76.9f, "attack1", "attackpowerstanding", "recoil",
                "staggerbacksmall", "death", 50.3f },
            // Its folder is `characterassets` (no space). NO sidecar and no foot bone: the gait clips are
            // netch_runforward (2 s) and netch_sprintforward (1 s); the strides 200 and 265 are ESTIMATES (about
            // three quarters and one of its 265 unit body length).
            { "netch", "actors/dlc02/netch/characterassets/", "netchproject", "netch_idle", "netch_runforward",
                "netch_sprintforward", 200.f / 2.f, 265.f / 1.f, 200.f, 265.f, 430.4f, "netch_attackright",
                "netch_attackpowerstanding", "netch_recoilright", "netch_staggersmall", "netch_staggerlarge", 50.6f },
            // Sidecar `dragon_priest`: MT_Walk_F 160 and MT_Run_F 600 units in 2 s. It fights with spells (the clips
            // are creatureCastClip's), so it has no melee attack; its second skeleton (skeletonnecropriest) lies in
            // the same folder.
            { "dragonpriest", "actors/dragonpriest/character assets/", "dragon_priest", "mt_idle", "mt_walk_f",
                "mt_run_f", 160.f / 2.f, 600.f / 2.f, 160.f, 600.f, 162.8f, "", "", "mt_stagger_b_small",
                "mt_stagger_b_medium", "deathpose", 20.4f },
            // NO sidecar. Foot excursion 59.0163 (mt_walkforward) and 92.2725 (mt_runforward): ESTIMATED strides 98.36
            // and 230.68.
            { "vampirelord", "actors/vampirelord/character assets/", "vampirelord", "mt_idle", "mt_walkforward",
                "mt_runforward", 98.36f / 1.13333f, 230.68f / 0.633333f, 98.36f, 230.68f, 153.9f, "mt_attackright",
                "mt_powerattackright", "mt_staggerbacksmall", "mt_staggerbacklarge", "mt_staggerbacklarge", 16.f },
            // NO sidecar. Foot excursion 97.0748 (walkforward) and 107.533 (runforward): ESTIMATED strides 161.79 and
            // 268.83.
            { "vampirebrute", "actors/dlc01/vampirebrute/character assets/", "vampirebruteproject", "idle",
                "walkforward", "runforward", 161.79f / 1.43333f, 268.83f / 0.6f, 161.79f, 268.83f, 182.4f, "attackr",
                "attack_standingpower", "recoilr", "stagger_small", "deathpose", 33.5f },
            // The Dwarven spider centurion; its folder is `dwarvenspider` (task 0290 looked under the other name).
            // Sidecar: Forward_Walk 100.037 units in 1 s; it has NO Forward_Run block, so the run stride 200.074
            // (twice the walk, as the mudcrab's) is an ESTIMATE.
            { "dwarvenspider", "actors/dwarvenspider/character assets/", "dwarvenspidercenturionproject", "mainidle",
                "forward_walk", "forward_run", 100.037f / 1.f, 200.074f / 1.f, 100.037f, 200.074f, 56.7f,
                "attack_combolow", "attackcombohigh", "recoil_jump", "stagger_back", "stagger_back", 54.6f },
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

    std::span<const CreatureProfile> creatureProfilesFourth()
    {
        return std::span<const CreatureProfile>(kFourthProfiles, std::size(kFourthProfiles));
    }

    std::span<const CreatureProfile> creatureProfilesFifth()
    {
        return std::span<const CreatureProfile>(kFifthProfiles, std::size(kFifthProfiles));
    }

    std::string_view creatureCastClip(const CreatureProfile& profile, bool self)
    {
        // Task 0301: the clips a creature that casts plays instead of the person's MRh_Release (not a file of its
        // folder). The dragon priest's Mag_RH_ff_R_release (MRh_SpellFire at 0.1 s) and Mag_RH_ff_S_release (0.466 s)
        // are the right hand fire and forget releases; the Dwarven spider's Attack_CastingA carries MLh_SpellFire at
        // 0.266 s, and the chaurus's file attack_releasespit is that generator too (a sidecar alias: same duration).
        if (profile.mName == "dragonpriest")
            return self ? "mag_rh_ff_s_release" : "mag_rh_ff_r_release";
        if (profile.mName == "dwarvenspider")
            return "attack_castinga";
        if (profile.mName == "chaurus")
            return "attack_releasespit";
        return {};
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
        const auto matches = [&path](const CreatureProfile& profile) {
            // The model root is optional (a path may come with or without it).
            const std::size_t at = path.find(profile.mSkeletonFolder);
            return at != std::string::npos && (at == 0 || path.compare(0, at, "meshes/") == 0);
        };
        for (const CreatureProfile& profile : kProfiles)
            if (matches(profile))
                return &profile;
        for (const CreatureProfile& profile : kFourthProfiles) // task 0290
            if (matches(profile))
                return &profile;
        for (const CreatureProfile& profile : kFifthProfiles) // task 0301
            if (matches(profile))
                return &profile;
        return nullptr;
    }
}
