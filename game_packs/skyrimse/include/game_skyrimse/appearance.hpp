#ifndef OPENEDEN_GAME_SKYRIMSE_APPEARANCE_H
#define OPENEDEN_GAME_SKYRIMSE_APPEARANCE_H

// Task 0121 (stage N5-1): what the core needs to draw an NPC, made from the decoded actor records of task 0118. Pure
// over an ActorIndex: no engine type, no file access, nothing throws. The result is the SDK's ActorAppearance
// (sdk/include/openeden/game/actor_appearance.hpp).

#include "game_skyrimse/actors.hpp"

#include <openeden/game/actor_appearance.hpp>

namespace game_skyrimse
{
    struct AppearanceLimits
    {
        std::uint32_t mMaxParts = 64;
        std::uint32_t mMaxHeadPartDepth = 2; ///< HNAM extra parts followed this deep
        std::size_t mMaxPathBytes = 512;
    };

    /// The level the leveled lists of an outfit or template are resolved at. A named guess: the data does not say at
    /// what level an actor is seen, and a leveled entry above it is not eligible.
    inline constexpr std::uint16_t kAppearanceLevel = 30;

    /// Body parts of a race's skin (or the NPC's WNAM skin), the default outfit's worn models and the head parts, with
    /// the skin parts the outfit covers left out (a skin part is dropped when its slot mask shares a bit with a worn
    /// model). Order: skin, outfit, head parts. False with `error` when the NPC is unknown, its template chain does
    /// not resolve, its race is unknown or has no skeleton for its sex. `variation` picks leveled templates and
    /// outfit entries (ActorIndex::pickLeveled).
    [[nodiscard]] bool makeActorAppearance(const ActorIndex& index, FormId npc, std::uint32_t variation,
        openeden::sdk::game::ActorAppearance& out, std::string& error, const AppearanceLimits& limits = {});
}

#endif
