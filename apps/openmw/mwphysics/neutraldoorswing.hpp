#ifndef OPENMW_MWPHYSICS_NEUTRALDOORSWING_H
#define OPENMW_MWPHYSICS_NEUTRALDOORSWING_H

// Task 0157 (task 0135 stage S6b follow-up): a neutral (game pack) interior door that is not a teleport and not locked
// opens for a walking neutral actor and closes behind it, as Morrowind's doors open for the actors that walk through
// them. The pure part: when a door counts as approached and how far it has opened. No engine type is involved, so
// the tests give it numbers.
//
// How a door "opens". Skyrim's door models (WRDoor02 and most others) are double doors or hinged leaves whose open
// motion is a NiControllerSequence inside the NIF, and the neutral renderer draws a model as one static piece, so the
// door cannot play it. Until it can, an open door is lowered into the floor: the whole reference (drawn object, body
// and navigator shape) moves down by `kSinkDepth * progress`, which clears the doorway for the path and for the walker
// whatever the leaf shape and hinge are. A door that opens this way is a stand-in for the animation, not a copy of it.
//
// Threading: main thread only (PhysicsSystem::updateNeutralActors). Allocation: none.

namespace MWPhysics::NeutralDoor
{
    /// An open door has sunk this far below its closed position (more than a door is tall: Skyrim's are about 180).
    inline constexpr float kSinkDepth = 200.f;
    /// Seconds to open and to close (Morrowind turns a door through 90 degrees in a second).
    inline constexpr float kOpenSeconds = 1.f;
    inline constexpr float kCloseSeconds = 1.f;
    /// After the last walker has gone the door stays open this long, so one that is just behind is not shut on.
    inline constexpr float kHoldSeconds = 2.f;
    /// A walker opens a door when it is this close to the door's centre on the ground plane and within kTriggerHeight.
    inline constexpr float kTriggerRadius = 160.f;
    inline constexpr float kTriggerHeight = 128.f;

    struct State
    {
        float mProgress = 0.f; ///< 0 closed .. 1 open
        float mHold = 0.f; ///< seconds the door is still held open after the last walker
    };

    /// Whether a walker at (walkerX, walkerY, walkerZ) (world) is close enough to open a door whose centre is at
    /// (centreX, centreY, centreZ).
    [[nodiscard]] bool isApproached(
        float centreX, float centreY, float centreZ, float walkerX, float walkerY, float walkerZ);

    /// One step of a door. `walkerNear` is whether any walker is within reach. A bad (not positive or not finite) frame
    /// time changes nothing.
    [[nodiscard]] State step(State state, bool walkerNear, float dt);

    /// How far below its closed position the door is for a state.
    [[nodiscard]] float sinkOffset(const State& state);
}

#endif
