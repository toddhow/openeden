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
// Version policy: DRAFT 0.3 (0.2, task 0125: ActorPartKind::Face, appended; 0.3, task 0135:
// ActorAppearance::heightScale, a trailing field the core only reads). Additive changes (a new trailing virtual with a
// default meaning "not supported", a new trailing field of ActorPart or ActorAppearance that a consumer only reads)
// raise the minor number; anything else raises the major number. Threading: main thread only, like WorldCellSource.

namespace openeden::sdk::game
{
    inline constexpr Version kActorAppearanceVersion{ 0, 3 };

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
