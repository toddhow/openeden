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
// Version policy: DRAFT 0.1. Additive changes (a new trailing virtual with a "not supported" default, a trailing field
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
    inline constexpr Version kActorRulesVersion{ 0, 1 };

    struct ActorHandle
    {
        WorldId reference = kNoWorldId; ///< WorldObject::id of the actor's placement
        std::uint32_t session = 0; ///< made by the core; 0 is "no handle"

        [[nodiscard]] bool valid() const { return reference != kNoWorldId && session != 0; }
        friend bool operator==(const ActorHandle&, const ActorHandle&) = default;
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
    };
}
