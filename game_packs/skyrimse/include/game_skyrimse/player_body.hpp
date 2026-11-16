#ifndef OPENEDEN_GAME_SKYRIMSE_PLAYER_BODY_H
#define OPENEDEN_GAME_SKYRIMSE_PLAYER_BODY_H

// Task 0222: the player has a Skyrim body. The player is not an NPC_ placement, so his appearance is made from a
// request (race, sex, what he wears and wields) instead of from an NPC record: the race's skin, the race's default head
// parts for the sex, the clothes he starts in (the default outfit) and the armour he has equipped, and the melee
// weapon and shield he holds. Pure over an ActorIndex: no engine type, no file access, nothing throws. The result is
// the SDK's ActorAppearance, drawn by the core like any actor.

#include "game_skyrimse/actors.hpp"
#include "game_skyrimse/appearance.hpp"

#include <openeden/game/actor_appearance.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace game_skyrimse
{
    /// The pack setting that gives the player a body (task 0222).
    inline constexpr std::string_view kPlayerBodyKey = "player body";

    /// The race of the player's body: NordRace of Skyrim.esm (form 0x13746; VERIFIED on the installed data: its two
    /// skeleton models and the head parts of both sexes exist).
    inline constexpr FormId kPlayerRace = 0x13746;
    /// The clothes the player has when nothing is equipped over them: `PrisonerOutfit` of Skyrim.esm (form 0xA1983, a
    /// tunic and shoes; VERIFIED: the record exists and names ClothesPrisonerTunic 0x3C9FE and ClothesPrisonerShoes
    /// 0x3CA00). The prisoner's cuffs of the opening are left out.
    inline constexpr FormId kPlayerStartOutfit = 0xA1983;

    /// What the player's body shows.
    struct PlayerBodyRequest
    {
        bool mFemale = false;
        FormId mRace = kPlayerRace;
        FormId mStartOutfit = kPlayerStartOutfit;
        /// The ARMO items the player has equipped (a shield among them is held, not worn). Any order.
        std::vector<FormId> mWornArmour;
        /// The WEAP the player has equipped; kNullFormId for none.
        FormId mWeapon = kNullFormId;
        /// Task 0225 (character creation): head parts the player chose. Each entry is an HDPT type (2 eyes, 3 hair, 4
        /// facial hair, ...) and the part chosen for it, kNullFormId for "none". A type listed here REPLACES the race's
        /// default head parts of that type (with the extra parts of the default hair or beard); the other defaults stay.
        std::vector<std::pair<std::uint32_t, FormId>> mHeadChoice;
    };

    /// The player's appearance. Skin parts that a worn piece covers are left out; a start-outfit piece is dropped when
    /// an equipped piece shares a biped slot with it. The held weapon and shield are named for the core to attach
    /// (a bow, staff or crossbow is not held: the same rule as an NPC's, task 0220; a two-handed weapon gives no
    /// shield). False with `error` when the race is unknown or has no skeleton for the sex. Deterministic: the same
    /// request gives the same appearance.
    [[nodiscard]] bool makePlayerAppearance(const ActorIndex& index, const PlayerBodyRequest& request,
        openeden::sdk::game::ActorAppearance& out, std::string& error, const AppearanceLimits& limits = {});
}

#endif
