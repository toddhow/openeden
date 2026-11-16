#ifndef OPENEDEN_GAME_SKYRIMSE_APPEARANCE_H
#define OPENEDEN_GAME_SKYRIMSE_APPEARANCE_H

// Task 0121 (stage N5-1): what the core needs to draw an NPC, made from the decoded actor records of task 0118. Pure
// over an ActorIndex: no engine type, no file access, nothing throws. The result is the SDK's ActorAppearance
// (sdk/include/openeden/game/actor_appearance.hpp).

#include "game_skyrimse/actors.hpp"
#include "game_skyrimse/creatures.hpp"

#include <openeden/game/actor_appearance.hpp>

#include <functional>
#include <string_view>
#include <string>

namespace game_skyrimse
{
    /// The pack setting that switches the held weapon and shield on (task 0220).
    inline constexpr std::string_view kHeldGearKey = "held gear";

    struct AppearanceLimits
    {
        std::uint32_t mMaxParts = 64;
        std::uint32_t mMaxHeadPartDepth = 2; ///< HNAM extra parts followed this deep
        std::size_t mMaxPathBytes = 512;
        /// Task 0212 (`[extension.skyrimse] creatures`): a creature race gets its own animation fields and capsule, and
        /// a sex whose skeleton or skin model is empty takes the other sex's. Off: described as before.
        bool mCreatures = false;
        /// Task 0220 (`[extension.skyrimse] held gear`): the actor's melee weapon and shield are named in the
        /// appearance (heldWeaponModel, heldWeaponType, heldShieldModel) and the shield's part leaves `parts`. Off: the
        /// appearance is what it was.
        bool mHeldGear = false;
    };

    /// What an actor holds (task 0220): the melee weapon and the shield it fights with, chosen by the rule of task 0214
    /// (combat_gear.hpp: the highest base damage then the lowest form id among the WEAP items of the resolved inventory
    /// and default outfit, leveled lists followed with `level` and `variation`, a use-all list giving every entry; the
    /// highest armour rating then the lowest form id among the shields; no shield with a two-handed weapon). The
    /// selection is repeated here, not called, because the combat index is read on a worker and may not be ready when
    /// an actor is drawn; a test pins that both choose the same.
    struct HeldGear
    {
        FormId mWeapon = kNullFormId;
        std::string mWeaponModel; ///< empty: no melee weapon with a model
        std::uint8_t mWeaponType = 0;
        FormId mShield = kNullFormId;
        std::string mShieldModel; ///< the ARMA model of the shield for the NPC's race and sex; empty: none
    };

    /// The held gear of an NPC. False for an NPC that cannot be resolved; a resolved NPC with nothing to hold gives
    /// true and an empty result.
    [[nodiscard]] bool resolveHeldGear(
        const ActorIndex& index, FormId npc, std::uint32_t variation, std::uint16_t level, HeldGear& out);

    /// The level the leveled lists of an outfit or template are resolved at. A named guess: the data does not say at
    /// what level an actor is seen, and a leveled entry above it is not eligible.
    inline constexpr std::uint16_t kAppearanceLevel = 30;

    /// Task 0135: the range an actor's height scale (race height times the NPC's own, NAM6) is cut to. The installed
    /// data holds NPC heights from 0.5 to 3.3 (see ActorNpc::mHeight); the bounds keep a hostile or damaged value from
    /// giving the core a capsule it cannot build.
    inline constexpr float kMinHeightScale = 0.25f;
    inline constexpr float kMaxHeightScale = 4.0f;

    /// Task 0125: where an NPC's pre-built face (facegen) is. The game stores it per NPC as
    /// `meshes\actors\character\facegendata\facegeom\<plugin>\<form id as 8 hex digits>.nif` (the plugin that
    /// owns the form id, lower case; the form id local to that plugin with the load-order byte zero). Verified on the
    /// five official masters (all 3,158 files of Skyrim - Meshes0.bsa follow it); a LIGHT plugin's naming is
    /// RECALLED-VERIFY (the low 12 bits are used). Empty when no plugin of `order` owns the id.
    [[nodiscard]] std::string faceGeometryPath(FormId npc, const LoadOrder& order);

    /// What makeActorAppearance needs to look for a facegen file: the load order that names the owning plugin and a
    /// test for the file's existence (lower case, backslashes). Both must be set.
    struct FaceFileProbe
    {
        const LoadOrder* mOrder = nullptr;
        std::function<bool(const std::string&)> mExists;
    };

    /// Body parts of a race's skin (or the NPC's WNAM skin), the default outfit's worn models and the head parts, with
    /// the skin parts the outfit covers left out (a skin part is dropped when its slot mask shares a bit with a worn
    /// model). Order: skin, outfit, then either the NPC's facegen head (a Face part, when `face` is given and the file
    /// of the NPC whose traits are used exists) or its head parts. False with `error` when the NPC is unknown, its
    /// template chain does not resolve, its race is unknown or has no skeleton for its sex. `variation` picks leveled
    /// templates and outfit entries (ActorIndex::pickLeveled).
    [[nodiscard]] bool makeActorAppearance(const ActorIndex& index, FormId npc, std::uint32_t variation,
        openeden::sdk::game::ActorAppearance& out, std::string& error, const AppearanceLimits& limits = {},
        const FaceFileProbe* face = nullptr);
}

#endif
