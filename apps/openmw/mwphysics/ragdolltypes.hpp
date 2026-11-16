#ifndef OPENEDEN_MWPHYSICS_RAGDOLLTYPES_H
#define OPENEDEN_MWPHYSICS_RAGDOLLTYPES_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace MWPhysics
{
    /// Task 0106: how many actor ragdolls (dead NPCs) the host holds at once, besides the debug ragdoll.
    constexpr std::size_t cMaxActorRagdolls = 8;

    /// Task 0195: how many actor ragdoll handles exist at once. At most cMaxActorRagdolls of them hold Jolt bodies; the
    /// others are frozen poses of settled corpses whose slot went to a new death.
    constexpr std::size_t cMaxActorRagdollEntries = 256;

    /// Task 0195: bones a frozen pose can hold (the rig has 16 bodies).
    constexpr std::size_t cMaxFrozenBones = 32;

    /// actorSettledStep's answer for a ragdoll that is not (yet) settled.
    constexpr std::uint64_t cNotSettled = ~std::uint64_t{ 0 };

    /// Names one actor ragdoll. A handle outlives its ragdoll: every call with a stale one fails.
    struct ActorRagdollHandle
    {
        std::uint32_t mIndex = ~0u;
        std::uint32_t mGeneration = 0;

        bool isValid() const { return mIndex != ~0u; }
        friend bool operator==(const ActorRagdollHandle&, const ActorRagdollHandle&) = default;
    };

    /// Task 0106, second slice: how many bones an ActorStartPose can name (the 17 of the skeleton profile, with room).
    constexpr std::size_t cMaxStartPoseBones = 24;

    /// One rig bone's rotation at the moment of death, relative to the rig's rest pose, in the actor's model frame (the
    /// actor's own yaw is NOT included: the host adds it). A unit quaternion (x, y, z, w). Identity means "as in the
    /// rig's standing rest pose".
    struct ActorStartBone
    {
        // "Pelvis", "LeftUpperArm", ...; must outlive the call (the mapping table's literals).
        std::string_view mRigBone;
        double mRotation[4] = { 0.0, 0.0, 0.0, 1.0 };
    };

    /// The pose a dead actor's ragdoll starts in (game units for the position). Rig bones that are not named keep the
    /// rotation of their rig parent (relative rotation identity).
    struct ActorStartPose
    {
        std::array<ActorStartBone, cMaxStartPoseBones> mBones;
        std::size_t mCount = 0;
        /// World position of the pelvis bone's origin, game units.
        double mPelvisPosition[3] = { 0.0, 0.0, 0.0 };

        bool add(std::string_view rigBone, const double rotation[4])
        {
            if (mCount >= mBones.size())
                return false;
            ActorStartBone& bone = mBones[mCount++];
            bone.mRigBone = rigBone;
            for (int i = 0; i < 4; ++i)
                bone.mRotation[i] = rotation[i];
            return true;
        }
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
