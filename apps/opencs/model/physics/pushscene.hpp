#ifndef CSM_PHYSICS_PUSHSCENE_H
#define CSM_PHYSICS_PUSHSCENE_H

// Eden Studio's push sandbox, model part (task 0065, stage A): a ragdoll built from a rig and a profile in the editor's
// one Jolt world, and impulses applied to its bodies between steps. No Qt and no render types.

#include "studioworld.hpp"

#include <opas/math/Vec3.hpp>
#include <opas/motor/Ragdoll.hpp>
#include <opas/physics/PhysicsWorld.hpp>
#include <opas/rig/Profile.hpp>
#include <opas/rig/Rig.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CSMPhysics
{
    /// A ragdoll in a StudioWorld and a short queue of pushes.
    ///
    /// Ownership: the scene owns the ragdoll's handles (the bodies live in the world, made through
    /// StudioWorld::cmWorld()) and registers itself as a pre-step hook of the world. It must be destroyed BEFORE the
    /// world; its destructor removes the ragdoll and the hook. StudioWorld::clear() makes the handles stale: the scene
    /// then has nothing in the world until reset() builds it again.
    ///
    /// Threads: the main thread only. A push is queued by push() and applied once, just before the next step, from the
    /// world's pre-step hook (so while Paused it waits for the next stepOnce(), while Playing for the next tick); it is
    /// never applied from inside a step. The queue is a fixed array: nothing allocates per step.
    class PushScene final : public StudioWorld::PreStepHook
    {
    public:
        /// The most pushes that can wait for the next step.
        static constexpr std::size_t sMaxPending = 16;
        /// The largest impulse accepted, in newton seconds (a 100 kg body would reach 1000 m/s).
        static constexpr double sMaxImpulseNs = 100000.0;
        /// Where a ragdoll built by reset() stands: the rig's root bone at its rest pose, moved by this much.
        struct Placement
        {
            opas::math::Vec3 rootPositionM;
            double yawRadians = 0.0;
        };

        /// The last push that was applied, for drawing an arrow.
        struct AppliedPush
        {
            std::size_t body = 0; ///< index into boneNames() (no string is copied when a push is applied)
            opas::math::Vec3 impulseNs;
            opas::math::Vec3 worldPointM;
            std::uint64_t stepIndex = 0; ///< the world's step index when it was applied
        };

        /// Null with \a error set when the rig or profile is invalid or the world refuses the bodies; nothing is left
        /// in the world then. \a world must outlive the scene.
        static std::unique_ptr<PushScene> create(StudioWorld& world, const opas::rig::Rig& rig,
            const opas::rig::Profile& profile, const Placement& placement, std::string* error = nullptr);

        ~PushScene() override;
        PushScene(const PushScene&) = delete;
        PushScene& operator=(const PushScene&) = delete;

        /// The bones that carry a body, in the rig's body order.
        const std::vector<std::string>& boneNames() const { return mBoneNames; }
        bool hasBone(std::string_view bone) const { return findBone(bone).has_value(); }
        /// The mass the rig gives a body; 0 for an unknown bone.
        double boneMassKg(std::string_view bone) const;
        /// False for an unknown bone, or when the ragdoll is not in the world (after StudioWorld::clear()).
        bool boneState(std::string_view bone, opas::physics::BodyState& out) const;
        /// Whether the ragdoll's bodies exist in the world (false after StudioWorld::clear() until reset()).
        bool present() const;

        /// Queues an impulse (newton seconds, world space) at a world point (meters) of the body on \a bone. False,
        /// and nothing queued, for an unknown bone, a non-finite value, an impulse above sMaxImpulseNs, no ragdoll in
        /// the world or a full queue.
        bool push(std::string_view bone, const opas::math::Vec3& impulseNs, const opas::math::Vec3& worldPointM);
        /// Pushes waiting for the next step; 0 once the ragdoll is gone (StudioWorld::clear() removes it behind our
        /// back), whatever is still in the queue.
        std::size_t pending() const { return present() ? mPendingCount : 0; }
        const std::optional<AppliedPush>& lastApplied() const { return mLastApplied; }

        /// Applies the queued pushes now, in order. Called by the world before each step; public for a host that
        /// steps by other means. Never call it from a step listener.
        void applyPending();

        /// Removes the ragdoll and builds it again at its placement; drops queued pushes and the last applied one.
        /// False with \a error set if the build fails (the scene then has no ragdoll).
        bool reset(std::string* error = nullptr);

        // StudioWorld::PreStepHook
        void beforeStep() override;

    private:
        struct Pending
        {
            std::size_t body = 0;
            opas::math::Vec3 impulseNs;
            opas::math::Vec3 worldPointM;
        };

        PushScene(StudioWorld& world, const opas::rig::Rig& rig, const opas::rig::Profile& profile,
            const Placement& placement);

        std::optional<std::size_t> findBone(std::string_view bone) const;
        bool build(std::string* error);
        void removeRagdoll();

        StudioWorld& mWorld;
        opas::rig::Rig mRig;
        opas::rig::Profile mProfile;
        Placement mPlacement;
        std::vector<std::string> mBoneNames;
        std::optional<opas::motor::Ragdoll> mRagdoll;
        std::array<Pending, sMaxPending> mPending;
        std::size_t mPendingCount = 0;
        std::optional<AppliedPush> mLastApplied;
    };
}

#endif
