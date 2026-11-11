#ifndef OPENEDEN_MWPHYSICS_RAGDOLLTYPES_H
#define OPENEDEN_MWPHYSICS_RAGDOLLTYPES_H

#include <cstddef>
#include <cstdint>

namespace MWPhysics
{
    /// Task 0106: how many actor ragdolls (dead NPCs) the host holds at once, besides the debug ragdoll.
    constexpr std::size_t cMaxActorRagdolls = 8;

    /// Names one actor ragdoll. A handle outlives its ragdoll: every call with a stale one fails.
    struct ActorRagdollHandle
    {
        std::uint32_t mIndex = ~0u;
        std::uint32_t mGeneration = 0;

        bool isValid() const { return mIndex != ~0u; }
        friend bool operator==(const ActorRagdollHandle&, const ActorRagdollHandle&) = default;
    };

    /// One ragdoll bone, read back in game units. The rotation is the BONE's world rotation (the body's rotation with
    /// the body-in-bone offset taken out), a unit quaternion (x, y, z, w); the position is the bone's origin.
    struct RagdollBoneState
    {
        double mPosition[3] = { 0.0, 0.0, 0.0 };
        double mRotation[4] = { 0.0, 0.0, 0.0, 1.0 };
    };
}

#endif
