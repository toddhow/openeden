#pragma once

#include <cstdint>
#include <string_view>

#include <openeden/game/world_view.hpp>
#include <openeden/version.hpp>

// The hooks a game pack uses to run the LIFE of its actors (task 0135, milestone M-S3, stage S3, decision 0017): the
// core tells the pack when an actor appears and goes, and asks it once a frame what each actor wants to do. For now a
// pack can only ask for MOVEMENT (a velocity on the ground); the core turns the request into a swept capsule move
// against its one physics world (stage S2). Rules, actor values and AI stay in the pack; the core never reads them.
//
// ABI choice: like ActorAppearanceSource and DialogueSource, a NEW interface found by dynamic_cast on the game's
// registered WorldCellSource, so the frozen WorldCellSource (world_source.hpp 1.x) is untouched. A pack that does not
// implement it simply has actors that stand where they were placed. No SDK major or minor change: the SDK stays 4.0.
//
// Version policy: DRAFT 0.2 (0.2, task 0135 stage S4: go-to requests with a result event, and a heading request; all
// additions have defaults, so a 0.1 pack or core keeps working). Additive changes (a new trailing virtual with a "not supported" default, a trailing field
// a consumer only reads) raise the minor number.
//
// Threading: main thread only. The core calls onActorAdded / onActorRemoved when it creates or destroys the actor's
// body (a cell loads or unloads) and update() once per frame before the physics step. A function must not keep the
// writer past the end of update(): a request written outside update() is dropped and counted.
//
// Handles are plain values: the id of the actor's placement (WorldObject::id) and a session number the core makes up
// for each add (never reused in a session, never 0). Never a pointer. A handle the core does not know, or whose session
// number does not match its placement, is stale: the request is dropped and counted, nothing else happens.

namespace openeden::sdk::game
{
    inline constexpr Version kActorRulesVersion{ 0, 2 };

    struct ActorHandle
    {
        WorldId reference = kNoWorldId; ///< WorldObject::id of the actor's placement
        std::uint32_t session = 0; ///< made by the core; 0 is "no handle"

        [[nodiscard]] bool valid() const { return reference != kNoWorldId && session != 0; }
        friend bool operator==(const ActorHandle&, const ActorHandle&) = default;
    };

    /// How fast a go-to request walks. The speeds are the core's (named estimates in the engine), not the pack's.
    enum class ActorSpeed : std::uint8_t
    {
        Walk = 0,
        Run = 1,
    };

    /// Where a go-to request ends: game units, the same axes as ActorInfo.
    struct ActorGoal
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        ActorSpeed speed = ActorSpeed::Walk;
    };

    /// How a go-to request ended, told to the pack through ActorRules::onGoalFinished.
    enum class GoalResult : std::uint8_t
    {
        Arrived = 0, ///< the actor reached the goal
        NoPath = 1, ///< the navigator found no path to it (or none that ends at it)
        Blocked = 2, ///< the actor made no progress along its path, also after the core asked for new paths
    };

    /// What the pack is told about an actor that appeared.
    struct ActorInfo
    {
        ActorHandle handle;
        WorldId base = kNoWorldId; ///< WorldObject::base
        /// The editor id of the base form as the pack gave it; valid only during the call.
        std::string_view editorId;
        double x = 0.0; ///< where the actor was placed, game units
        double y = 0.0;
        double z = 0.0;
        float heading = 0.f; ///< radians about Z as the engine stores it (0 faces +Y)
    };

    /// How the pack answers in update(). One writer, valid during one update() call.
    class ActorRequestWriter
    {
    public:
        virtual ~ActorRequestWriter() = default;

        /// Asks the actor to move on the ground at (velX, velY) game units a second, heading towards the direction of
        /// travel. The request stays until the next one for the same actor; (0, 0) stops it. False (and counted by the
        /// core) for a stale or unknown handle, a value that is not finite, or a call outside update().
        virtual bool setMovement(ActorHandle handle, float velX, float velY) = 0;

        /// (0.2) Asks the actor to walk to `goal` along a path the core asks the navigator for. Replaces an earlier
        /// goal or movement request of the actor; a later setMovement cancels the goal. The result arrives through
        /// ActorRules::onGoalFinished in a LATER update(): Arrived, NoPath or Blocked. A goal that is replaced or
        /// cancelled gives no result. False (counted) for a stale handle, a value that is not finite, a speed that is
        /// not one of ActorSpeed, or a call outside update(). The default refuses.
        virtual bool goTo(ActorHandle handle, const ActorGoal& goal)
        {
            (void)handle;
            (void)goal;
            return false;
        }

        /// (0.2) Drops the actor's goal and stops it. No result event. False as for goTo.
        virtual bool cancelGoal(ActorHandle handle)
        {
            (void)handle;
            return false;
        }

        /// (0.2) Turns the actor once to face `heading` (radians about Z, 0 faces +Y) if it is standing; a moving actor
        /// faces its direction of travel and ignores it. False as for goTo (and for a heading that is not finite).
        virtual bool setHeading(ActorHandle handle, float heading)
        {
            (void)handle;
            (void)heading;
            return false;
        }
    };

    /// Implemented by a WorldCellSource whose game has actors with a life.
    class ActorRules
    {
    public:
        virtual ~ActorRules() = default;

        virtual void onActorAdded(const ActorInfo& info) = 0;
        virtual void onActorRemoved(ActorHandle handle) = 0;

        /// Once per frame, before the physics step; `seconds` is the frame time as the engine's loop gives it.
        virtual void update(float seconds, ActorRequestWriter& writer) = 0;

        /// (0.2) A go-to request ended. Called from the core's update step just before update() (so a result written in
        /// frame N is told in frame N+1), on the main thread, at most once per goal. Not called for a goal that was
        /// replaced or cancelled, or whose actor has gone. The default ignores it.
        virtual void onGoalFinished(ActorHandle handle, GoalResult result)
        {
            (void)handle;
            (void)result;
        }
    };
}
