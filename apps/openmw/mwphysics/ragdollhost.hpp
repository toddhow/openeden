#ifndef OPENMW_MWPHYSICS_RAGDOLLHOST_H
#define OPENMW_MWPHYSICS_RAGDOLLHOST_H

#include <Jolt/Jolt.h>

#include <Jolt/Core/Reference.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "ragdolltypes.hpp"

#include <components/cmphysics/hostedjoltworld.hpp>

#include <opas/motor/Ragdoll.hpp>
#include <opas/physics/PhysicsWorld.hpp>

#include <opas/rig/Profile.hpp>
#include <opas/rig/Rig.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace MWPhysics
{
    /// Where the debug ragdoll is removed on its own: its lowest body this far below its spawn point (metres).
    constexpr float cRagdollKillDepthMetres = 20.0f;

    /// Task 0106, second slice (2): how far (metres) a ragdoll may be moved out of walls and floors before it is built,
    /// and how many collision passes that may take. Beyond that it is built where it is and Jolt resolves the rest.
    constexpr double cMaxStartShiftMetres = 2.0;
    constexpr unsigned cMaxStartPlacementPasses = 8;

    /// How often the debug scenario writes the ragdoll's status to the log (about two seconds at 60 steps a second).
    constexpr unsigned cProgressLogSteps = 120;

    struct RagdollStatus
    {
        bool present = false; // a ragdoll exists
        std::size_t bodies = 0; // its body count
        double lowestZ = 0.0; // lowest and highest body centre, in game units
        double highestZ = 0.0;
        float maxSpeedMetresPerSecond = 0.0f; // fastest linear speed of any body
        bool allAsleep = false; // every body is asleep in Jolt
    };

    /// The game's side of the ragdoll adapter (task 0035, stage b): one CMPhysics::HostedJoltWorld over the game's
    /// single Jolt world, and at most one debug ragdoll built from CreativeMotion's humanoid rig.
    ///
    /// Ownership: the PhysicsSystem is the game's and must outlive this object. The host owns the adapter and the
    /// ragdoll handles. Thread: everything here runs on the main thread (the script interpreter and
    /// PhysicsSystem::stepSimulation are both called from World::update), never on a Jolt worker.
    class RagdollHost
    {
    public:
        /// nullptr and a message if the adapter refuses.
        static std::unique_ptr<RagdollHost> create(
            JPH::PhysicsSystem& system, float unitsPerMeter, float stepSeconds, std::string* error = nullptr);

        /// Removes the ragdoll, then the adapter: the system is left as it was found.
        ~RagdollHost();

        RagdollHost(const RagdollHost&) = delete;
        RagdollHost& operator=(const RagdollHost&) = delete;

        /// Builds (replacing any earlier one) a limp humanoid ragdoll standing at feet (game units) facing game yaw
        /// (radians, the NPC's rot[2]). Returns false and a message on failure; then no ragdoll exists.
        bool spawn(const JPH::RVec3& feetUnits, float yawRadians, std::string* error = nullptr);

        /// No ragdoll afterwards; does nothing if there is none.
        void remove();

        bool hasRagdoll() const { return mRagdoll.has_value(); }

        /// The debug scenario only: while on, endStep writes the ragdoll's status to the log every cProgressLogSteps
        /// steps. Logging allocates, so it is off by default and the allocation promise holds only while it is off.
        void setProgressLogging(bool enabled) { mProgressLogging = enabled; }

        /// Around the host's PhysicsSystem::Update, once per Update. endStep also removes the ragdoll if
        /// cRagdollKillDepthMetres was passed. No allocation.
        void beginStep();
        void endStep();

        /// No allocation.
        RagdollStatus status() const;

        // --- Actor ragdolls (task 0106). Main thread only, like everything here. ---

        /// Builds a limp humanoid ragdoll for a dead actor, standing at feet (game units) facing yaw (the actor's
        /// rot[2]), every body moving at linearVelocityUnits (game units per second). Returns an invalid handle and
        /// a message when the table is full (cMaxActorRagdolls) or the build fails; nothing is left behind then.
        ///
        /// With a pose (task 0106, second slice) the rig is built in that pose instead of the standing rest pose: the
        /// pelvis at pose->mPelvisPosition, every named bone turned as given (the yaw added), the others turning with
        /// their parent. The joint limits stay the rig's, measured from the start pose (a bent elbow can bend the rig's
        /// range further either way: they are not re-centred on anatomy). feetUnits is then used for the kill depth
        /// only.
        ActorRagdollHandle spawnActor(const JPH::RVec3& feetUnits, float yawRadians,
            const double linearVelocityUnits[3], std::string* error = nullptr, const ActorStartPose* pose = nullptr);

        /// False if the handle is stale. Frees the slot; the Jolt bodies and joints are gone.
        bool removeActor(ActorRagdollHandle handle);

        bool hasActor(ActorRagdollHandle handle) const;

        /// Number of ragdolls of actors that exist now.
        std::size_t actorRagdollCount() const;

        /// The rig bone each body follows ("Pelvis", "LeftUpperArm", ...), in body order: the same for every actor
        /// ragdoll. Empty before the first spawn.
        std::span<const std::string> actorBodyBones() const { return mActorBodyBones; }

        /// Writes one state per body (in actorBodyBones order) to out, which must hold at least that many. False for
        /// a stale handle or a too small span. No allocation.
        bool readActor(ActorRagdollHandle handle, std::span<RagdollBoneState> out) const;

        /// Lowest and highest body, top speed and sleep state of one actor ragdoll (the debug ragdoll's RagdollStatus).
        /// present is false for a stale handle. No allocation.
        RagdollStatus actorStatus(ActorRagdollHandle handle) const;

    private:
        class LayerFilter;
        class BroadPhaseFilter;
        class StaticLayerFilter;

        explicit RagdollHost(float unitsPerMeter);

        struct ActorSlot
        {
            bool mUsed = false;
            std::uint32_t mGeneration = 0;
            double mSpawnZ = 0.0;
            unsigned mSteps = 0; // physics steps since the spawn (counted only while the trace is on)
            bool mSettledLogged = false;
            std::optional<opas::motor::Ragdoll> mRagdoll;
            std::vector<opas::physics::BodyId> mBodyIds;
            mutable std::vector<opas::physics::BodyState> mStates;
        };

        bool loadRigAndProfile(std::string* error);
        void summarize(const std::vector<opas::physics::BodyState>& states, RagdollStatus& result) const;
        // The two places game units and meters meet in this class.
        double metres(double units) const { return units / static_cast<double>(mUnitsPerMeter); }
        double fromMetres(double meters) const { return meters * static_cast<double>(mUnitsPerMeter); }
        void clearActorSlot(ActorSlot& slot);
        // Task 0106, second slice (2): the shift (metres) that moves a ragdoll built from `rig` at `rootWorld` out of
        // the static world, found with a bounded number of collision queries. Zero when nothing touches it or it
        // cannot be freed within cMaxStartShiftMetres.
        opas::math::Vec3 findStartShift(const opas::rig::Rig& rig, const opas::math::Transform& rootWorld) const;

        JPH::PhysicsSystem* mSystem = nullptr; // the game's, not owned; outlives this object
        float mUnitsPerMeter;
        float mStepSeconds = 1.0f / 60.0f; // only for the developer trace
        // Parsed once, at the first actor spawn (the debug ragdoll still parses its own copy: unchanged).
        std::optional<opas::rig::Rig> mActorRig;
        std::optional<opas::rig::Profile> mActorProfile;
        std::vector<std::string> mActorBodyBones;
        std::vector<JPH::RefConst<JPH::Shape>> mActorShapes; // one per rig body, for the start placement queries
        std::array<ActorSlot, cMaxActorRagdolls> mActorSlots;
        unsigned mActorStepCounter = 0;
        unsigned mActorTraceCounter = 0;
        double mSpawnZ = 0.0;
        bool mProgressLogging = false;
        unsigned mStepsSinceSpawn = 0;
        // The adapter keeps pointers to the two filters, so they are declared before it (destroyed after it).
        std::unique_ptr<LayerFilter> mRayObjectFilter;
        std::unique_ptr<BroadPhaseFilter> mRayBroadPhaseFilter;
        std::unique_ptr<CMPhysics::HostedJoltWorld> mWorld;
        std::optional<opas::motor::Ragdoll> mRagdoll;
        std::vector<opas::physics::BodyId> mBodyIds;
        mutable std::vector<opas::physics::BodyState> mStates;
    };
}

#endif
