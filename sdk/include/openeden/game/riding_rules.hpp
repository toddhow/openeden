#pragma once

#include <cstdint>

#include <openeden/game/actor_rules.hpp>
#include <openeden/version.hpp>

// Riding (task 0251, milestone M-S7): the player mounts a horse, the player's controls drive the horse instead of the
// player, the player's body is carried with it, and a dismount puts the player down beside it.
//
// The pack owns the rules (which actor is a mount, how fast it goes, where the rider sits); the core only supplies the
// player's controls and carries the player's body to a place the pack names. Two small interfaces, both found by
// dynamic_cast, so the frozen ActorRules and ActorRequestWriter are untouched and a pack that does not ride is never
// asked. No SDK major or minor change: the SDK stays 4.0.
//
//   * RidingRules   implemented by the pack's ActorRules: "can this actor be mounted" and "the player asked to mount".
//   * RidingWriter  implemented by the core's ActorRequestWriter (the writer update() receives): the player's controls
//                   this frame, and carrying the player.
//
// Version policy: DRAFT 0.1. Additive changes (a new trailing virtual with a "not supported" default, a trailing field)
// raise the minor number. Threading: main thread only; RidingWriter calls are valid during ActorRules::update() only,
// RidingRules::canMount / onMountRequested are called from the core's activation and may come between updates.

namespace openeden::sdk::game
{
    inline constexpr Version kRidingRulesVersion{ 0, 3 };

    /// What the player asks of his mount in one frame. The core reads the player's real controls; while a menu is up
    /// (or controls are disabled) `valid` is true and every value is zero.
    struct RiderControls
    {
        bool valid = false; ///< false: the core has no controls (a test, a session with no player)
        float forward = 0.f; ///< [-1, 1]: forward minus backward
        float turn = 0.f; ///< [-1, 1]: positive turns to the right (clockwise seen from above)
        bool run = false; ///< the run control is on
        bool jump = false; ///< the jump control is down this frame (the core reports the level, not the edge)
        float heading
            = 0.f; ///< the player's own yaw (the view's), radians about Z as the engine stores it (0 faces +Y)
    };

    /// Implemented by the core's ActorRequestWriter (found by dynamic_cast on the writer update() gets).
    class RidingWriter
    {
    public:
        virtual ~RidingWriter() = default;

        /// The player's controls this frame. False (the output is left alone) when the core has none.
        virtual bool riderControls(RiderControls& out) const
        {
            (void)out;
            return false;
        }

        /// Puts the player at (x, y, z) facing `heading` and takes him off his own movement for this frame (no walking,
        /// no falling, no collision), so that he stays where the pack put him. The first call also switches the camera
        /// to third person. Repeated every frame while mounted. False for a value that is not finite or when the core
        /// has no player.
        virtual bool carryPlayer(double x, double y, double z, float heading)
        {
            (void)x;
            (void)y;
            (void)z;
            (void)heading;
            return false;
        }

        /// Gives the player his own movement and collision back and stands him at (x, y, z) (the core puts him on the
        /// floor under that point). A no-op for a player who is not carried. False for a value that is not finite.
        virtual bool releasePlayer(double x, double y, double z)
        {
            (void)x;
            (void)y;
            (void)z;
            return false;
        }
    };

    /// Implemented by the pack's ActorRules (found by dynamic_cast on the game's registered ActorRules).
    class RidingRules
    {
    public:
        virtual ~RidingRules() = default;

        /// True when the actor behind `handle` is a mount the player may get on now (a horse the player owns, or any
        /// horse a test setting names) and the player is not riding already.
        [[nodiscard]] virtual bool canMount(ActorHandle handle) const = 0;

        /// The player activated the mount: the pack mounts (in its next update) or dismounts when he is on it.
        virtual void onMountRequested(ActorHandle handle) = 0;

        /// (0.2, task 0255) The core is about to move the player through a load door: a rider gets off NOW (the player
        /// is released beside the horse through `player`, outside update()), so that the door takes a man on his feet.
        /// A no-op for a player who is not riding. The default does nothing.
        virtual void dismountNow(RidingWriter& player) { (void)player; }

        /// (0.2, task 0255) The actor the player sits on now; an invalid handle on foot. The core gives that actor the
        /// climb and drop limits of a mount (NeutralMover::limitsForHeight) and every other actor its own. Called once
        /// a frame. The default says nobody is ridden.
        [[nodiscard]] virtual ActorHandle mountedHorse() const { return ActorHandle(); }

        /// (0.3, task 0274) An actor the pack drives like a mount without the player on it (a horse that pulls a cart):
        /// the core gives it the climb and drop limits of a mount, as for `mountedHorse`. Called for each walking actor
        /// a frame. The default says no.
        [[nodiscard]] virtual bool usesMountLimits(ActorHandle handle) const
        {
            (void)handle;
            return false;
        }
    };
}
