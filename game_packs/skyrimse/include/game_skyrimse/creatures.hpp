#ifndef OPENEDEN_GAME_SKYRIMSE_CREATURES_H
#define OPENEDEN_GAME_SKYRIMSE_CREATURES_H

// Tasks 0212 and 0216 (creatures: the wolf, then nine more): what the pack knows about a creature RACE that a person
// does not need.
// Skyrim's animals and monsters are NPC_ records with a creature RACE: their own skeleton and skin model (no outfit, no
// head parts), and their own animation project (a folder of clips next to the skeleton, with no male / female split).
// The numbers below are read from the installed data (docs/tasks/0212-skyrim-creatures-wolf.md section 2) or are named
// estimates:
//   VERIFIED  the file names (the listing of Skyrim - Animations.bsa, actors/canine/animations), the project name
//             (meshes/animationdata/wolfproject.txt and boundanims/anims_wolfproject.txt), the sidecar events of
//             Attack1 (weaponSwing 0.2, preHitFrame 0.267, HitFrame 0.433) and the strides below: the root motion of
//             WalkForward00_Wolf is 89.448 units in 1.2 s and of RunForward 300.081 units in 0.533333 s.
//   ESTIMATE  the capsule height, and which clip is the "walk" and the "run" (the behaviour graph that chooses them is
//             not decoded: the pack takes walkforward_wolf and runforward by their names).
// Nothing here is read unless the pack setting `creatures` is on.

#include <openeden/game/combat_rules.hpp>

#include <cstddef>
#include <span>
#include <string_view>

namespace game_skyrimse
{
    /// The pack setting (bool, default false; needs the engine's `[Game] neutral actors`): creature races get their own
    /// animation, collision size and combat clips. Off, a creature is described like a person (as before this task).
    inline constexpr std::string_view kCreaturesKey = "creatures";

    /// What the core's capsule is measured against (NeutralActor::kStandingHeight): a creature's capsule height is
    /// `mCapsuleHeight / kCreatureReferenceHeight` of it.
    inline constexpr float kCreatureReferenceHeight = 128.f;

    struct CreatureProfile
    {
        std::string_view mName; ///< for logs
        /// Lower case, forward slashes, the folder of the skeleton file the race names (a match is a prefix of the
        /// skeleton path with its separators normalised).
        std::string_view mSkeletonFolder;
        std::string_view mProject; ///< the sidecar project: animationdata/<project>.txt
        std::string_view mIdle; ///< clip file names under <actor folder>/animations, no extension
        std::string_view mWalk;
        std::string_view mRun;
        float mWalkSpeed; ///< units a second: the stride over the clip's duration
        float mRunSpeed;
        float mWalkStride; ///< units one cycle covers by the clip's root motion
        float mRunStride;
        float mCapsuleHeight; ///< game units; an ESTIMATE of the animal's height at the shoulder
        // The clip of each combat slot (the sidecar's generator name equals the file name, VERIFIED for these five).
        std::string_view mAttack;
        std::string_view mPower;
        std::string_view mRecoil;
        std::string_view mStagger;
        std::string_view mDeath;
    };

    /// Task 0216: every profile the pack knows, in table order (the wolf first). The table is the one place to add a
    /// creature: one row in creatures.cpp, with the numbers read as docs/tasks/0216-skyrim-creatures-more.md section 2
    /// says.
    [[nodiscard]] std::span<const CreatureProfile> creatureProfiles();

    /// The clip of a combat slot for this creature (Attack, PowerAttack, Recoil, Stagger, Death), or empty: a creature
    /// has no block, bash, cast or bow clips.
    [[nodiscard]] std::string_view creatureClipFor(const CreatureProfile& profile, openeden::sdk::game::ClipSlot slot);

    /// The profile of a creature whose race names this skeleton file (any case, either slash), or null: a person, and
    /// every creature this slice does not know.
    [[nodiscard]] const CreatureProfile* creatureProfileFor(std::string_view skeletonPath);

    /// The wolf's profile (WolfRace, FoxRace's skeleton, C06WolfSpiritRace and the death hounds share it).
    [[nodiscard]] const CreatureProfile& wolfProfile();
}

#endif
