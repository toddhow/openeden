#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <openeden/game/world_model.hpp>
#include <openeden/version.hpp>

// What the core needs to DRAW a game's actor (decision 0017, option B; task 0121, stages N5-1 and N5-2): the skeleton
// model, the sex, and the list of skinned models the actor wears (body skin, outfit, head parts). Nothing about stats,
// inventory, AI or perks crosses: those stay in the pack (decision 0017 section 4).
//
// ABI choice (N5-1): this is a NEW interface, found by dynamic_cast on the game's registered WorldCellSource, not a
// new member appended to WorldObject by value (a by-value struct that a shared library fills is not ABI-additive, the
// RestoreResult row of CLAUDE.md) and not a new virtual on the frozen WorldCellSource (world_source.hpp is pinned at
// 1.0 by tests). A pack that does not implement it simply draws no actors. Plain values only: no engine, renderer,
// Jolt or Qt type.
//
// Version policy: DRAFT 0.5 (0.5, task 0220: the held weapon and shield at the end of ActorAppearance, empty unless the
// pack makes them; 0.2, task 0125: ActorPartKind::Face, appended; 0.3, task 0135:
// ActorAppearance::heightScale, a trailing field the core only reads; 0.4, task 0212: the creature fields at the end of
// ActorAppearance, which a consumer only reads and which are all empty or 0 for a person). Additive changes (a new trailing virtual with a
// default meaning "not supported", a new trailing field of ActorPart or ActorAppearance that a consumer only reads)
// raise the minor number; anything else raises the major number. Threading: main thread only, like WorldCellSource.

namespace openeden::sdk::game
{
    inline constexpr Version kActorAppearanceVersion{ 0, 6 };

    enum class ActorPartKind : std::uint8_t
    {
        Skin, ///< A body part of the race's naked skin (body, hands, feet)
        Armor, ///< A worn model of the default outfit
        HeadPart, ///< Head, eyes, hair, brows ...
        /// The actor's own pre-built face (Skyrim SE: the NPC's facegen head, head parts already morphed and coloured).
        /// When a Face part is present the head parts it contains are NOT listed as HeadPart parts.
        Face,
    };

    struct ActorPart
    {
        /// The skinned model as the data stores it, relative to the game's mesh root, with the game's own separators.
        std::string modelPath;
        ActorPartKind kind = ActorPartKind::Armor;
        /// The game's biped slot bit mask of the part (Skyrim SE: ARMA BOD2); 0 for a head part.
        std::uint32_t slotMask = 0;
        /// A head part's type as the game numbers it (Skyrim SE: HDPT PNAM: 0 misc, 1 face, 2 eyes, 3 hair, ...).
        std::uint32_t headPartType = 0;

        friend bool operator==(const ActorPart&, const ActorPart&) = default;
    };

    struct ActorAppearance
    {
        /// The skeleton model, relative to the game's mesh root.
        std::string skeletonPath;
        bool female = false;
        /// In draw order. Skin parts the outfit covers are already left out.
        std::vector<ActorPart> parts;
        /// How tall the actor stands compared with the game's standard adult (1 = standard), race and individual
        /// combined (Skyrim SE: RACE DATA height for the sex times the NPC's own NAM6). The core sizes the actor's
        /// collision capsule from it (task 0135, decision 0017 N5-3). Finite and positive; a pack clamps what the data
        /// holds. Added in 0.3.
        float heightScale = 1.0f;

        // ---- Added in 0.4 (task 0212): the animation of a creature. All empty or 0 for a person, and the core then
        // plays what it always did (the `male` / `female` folder clips `mt_idle`, `mt_walkforward`, `mt_runforward` of
        // the humanoid project). A creature's clips are in the actor folder's own animations folder, with no sex folder.
        /// The animation project that holds the clips' sidecar (events, root motion), the file stem under
        /// `animationdata/` (`wolfproject`). Empty: the humanoid `defaultmale` / `defaultfemale`.
        std::string animationProject;
        /// Clip file names without extension: the standing loop, the walk and the run cycles. Empty: the humanoid ones.
        std::string idleClip;
        std::string walkClip;
        std::string runClip;
        /// Units a second the creature walks and runs at, and the distance one cycle of each clip covers by its own
        /// root motion (what makes the feet stand still on the ground). 0: the core's own numbers.
        float walkSpeed = 0.f;
        float runSpeed = 0.f;
        float walkStride = 0.f;
        float runStride = 0.f;

        // ---- Added in 0.5 (task 0220): what the actor holds. Empty for an actor with no weapon or shield, and for a
        // pack that does not make them (the core then draws only `parts`).
        /// The model of the melee weapon the actor carries, relative to the game's mesh root. The core shows it at the
        /// actor's sheath node while at peace and in the hand while the actor fights.
        std::string heldWeaponModel;
        /// The weapon's animation type as the game numbers it (Skyrim SE: WEAP DNAM +0: 1 sword, 2 dagger, 3 axe, 4 mace,
        /// 5 two-handed sword, 6 two-handed axe or hammer); chooses the sheath node. 0 when there is no weapon.
        std::uint8_t heldWeaponType = 0;
        /// The model of the shield on the actor's arm; empty when it has none. Its part is not in `parts`.
        std::string heldShieldModel;

        // ---- Added in 0.6 (task 0246): the collision capsule's radius.
        /// The radius of the actor's collision capsule in game units. 0: the core's own (a person's). A creature's is
        /// measured from its skin model (docs/tasks/0246-skyrim-creatures-third.md); the core caps it so that the capsule
        /// stays a capsule.
        float capsuleRadius = 0.f;

        friend bool operator==(const ActorAppearance&, const ActorAppearance&) = default;
    };

    /// Implemented by a WorldCellSource that can describe its actors.
    class ActorAppearanceSource
    {
    public:
        virtual ~ActorAppearanceSource() = default;

        /// The appearance of the actor base `base` (WorldObject::base of an ObjectKind::Actor object). `variation` is
        /// a number the caller supplies for the choices the data leaves open (a leveled template): the placement id.
        /// False with `error` when the base is not a known actor or has no skeleton. Main thread only.
        [[nodiscard]] virtual bool describeActor(
            WorldId base, std::uint32_t variation, ActorAppearance& out, std::string& error)
            = 0;
    };
}
