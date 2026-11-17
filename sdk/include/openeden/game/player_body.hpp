#pragma once

#include <cstdint>
#include <string>

#include <openeden/game/actor_appearance.hpp>
#include <openeden/version.hpp>

// The player's own body (task 0222): what a game pack shows of the player in third person. The player is not a world
// object (the core makes him from a stub record), so the pack describes him through its own optional interface, found
// by dynamic_cast on the game's registered WorldCellSource (as ActorAppearanceSource is). The core POLLS: playerBody()
// never blocks and never reads a file; describePlayerBody() is asked for only when `revision` changed.
//
// What the pack shows is its own choice (race, sex, worn armour, held weapon and shield); the core draws the
// ActorAppearance the way it draws any actor and plays the clips the state names. Plain values only.
//
// Version policy: DRAFT 0.3 (0.3, task 0251: ridingClip; 0.2, task 0245: the first-person arms:
// PlayerBodyState::firstPerson, castToken and castClip at the end, and a trailing describeFirstPersonBody with a
// default that refuses). Additive changes (a trailing virtual with a default, a trailing field the core only reads)
// raise the minor number. Threading: main thread only.

namespace openeden::sdk::game
{
    inline constexpr Version kPlayerBodyVersion{ 0, 3 };

    struct PlayerBodyState
    {
        /// 0 while the pack cannot describe the player yet (its records are still being read, or the setting is off);
        /// otherwise a number that changes whenever the picture changes (worn armour, held weapon, sex).
        std::uint32_t revision = 0;
        /// The player's melee swings, newest last: a number that grows by one for each swing the pack began, and the
        /// clip that swing plays (a clip file name without extension). The core plays the clip when the number
        /// changes. 0 and empty: no swing yet.
        std::uint32_t swingToken = 0;
        std::string swingClip;

        // ---- Added in 0.2 (task 0245). Fields the core only reads.
        /// True when the pack also describes the player's first-person arms (describeFirstPersonBody); the core then
        /// draws them in first person and puts the camera on their camera node.
        bool firstPerson = false;
        /// The player's spell casts, like swingToken: grows by one for each cast the pack began, with the clip that
        /// cast plays (a clip file name without extension). 0 and empty: no cast yet.
        std::uint32_t castToken = 0;
        std::string castClip;

        // ---- Added in 0.3 (task 0249). Fields the core only reads.
        /// The player's bow: `bowDrawToken` grows by one each time he began to draw it (with the clip that draw plays),
        /// `bowReleaseToken` by one for each arrow he let go (with the release clip); `bowDrawing` is true while the
        /// string is held back. 0 and empty: nothing yet.
        std::uint32_t bowDrawToken = 0;
        std::uint32_t bowReleaseToken = 0;
        bool bowDrawing = false;
        std::string bowDrawClip;
        std::string bowReleaseClip;
        /// A spell he is holding: 0 none, 1 charging (pressed, the spell has not gone yet), 2 holding (a concentration
        /// spell or a ward that goes on). `castHoldSeconds` is how long that has lasted (pack seconds, 0 for none) and
        /// `castHoldClip` the loop the hands play meanwhile (empty for none).
        std::uint8_t castHold = 0;
        float castHoldSeconds = 0.f;
        std::string castHoldClip;
        // ---- Also 0.3 (task 0251, riding; merged beside task 0249). A field the core only reads.
        /// While the player rides, the clip his body loops instead of walking (a path under the actor's animations
        /// folder, for example `horse_rider/walkforward`); empty on foot. The pack changes it with the gait.
        std::string ridingClip;
        // ---- Also 0.3 (task 0255, riding second slice). Fields the core only reads.
        /// The placement id of the horse the player sits on (0 on foot). The core finds that horse's drawn object and
        /// moves the player's body by how far its `SaddleBone` node has moved since the ride began (the gait's bob).
        std::uint32_t ridingMount = 0;
        // ---- Also 0.3 (task 0267, paired animations). A field the core only reads.
        /// Where the body is drawn relative to the stub player (world units, added to his position): zero except
        /// while a paired clip plays, when the pack keeps the player (his camera, his collision) on the saddle and
        /// the clip is in the horse's frame, so the body is drawn at the horse's origin.
        float bodyOffsetX = 0.f;
        float bodyOffsetY = 0.f;
        float bodyOffsetZ = 0.f;
    };

    class PlayerBodySource
    {
    public:
        virtual ~PlayerBodySource() = default;

        /// Cheap and non-blocking. False when the pack has no player body (the setting is off).
        [[nodiscard]] virtual bool playerBody(PlayerBodyState& out) = 0;

        /// The player's appearance now. False with `error` when it cannot be made. May read the pack's actor records
        /// the first time (a pause the core accepts once, when the player is first drawn).
        [[nodiscard]] virtual bool describePlayerBody(ActorAppearance& out, std::string& error) = 0;

        /// Added in 0.2 (task 0245). The player's first-person arms: ActorAppearance::skeletonPath is the first-person
        /// skeleton, `parts` are the arm models (and worn gloves, sleeves) made for the view, `heldWeaponModel` and
        /// `heldShieldModel` what the hands hold. Asked for only when `playerBody()` said `firstPerson` and the
        /// revision changed. The default has none.
        [[nodiscard]] virtual bool describeFirstPersonBody(ActorAppearance& out, std::string& error)
        {
            out = {};
            error = "this game has no first-person body";
            return false;
        }
    };
}
