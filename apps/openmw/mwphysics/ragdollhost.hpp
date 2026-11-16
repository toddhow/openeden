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

    /// Task 0195: how often (in steps) a live ragdoll is checked for having settled (every body asleep).
    constexpr unsigned cSettleCheckSteps = 10;

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

        /// Number of actor ragdoll handles that exist now (live and frozen).
        std::size_t actorRagdollCount() const;

        /// Task 0195. Ragdolls that hold Jolt bodies now (at most cMaxActorRagdolls).
        std::size_t liveActorRagdollCount() const;

        /// Task 0195. True for a settled corpse whose slot went to a new death: its handle stays valid, readActor gives
        /// its last pose, it holds no Jolt body.
        bool isActorFrozen(ActorRagdollHandle handle) const;

        /// Task 0195. The step at which a live ragdoll was first seen with every body asleep (checked every
        /// cSettleCheckSteps steps), or cNotSettled. For a frozen or stale handle also cNotSettled.
        std::uint64_t actorSettledStep(ActorRagdollHandle handle) const;

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

        /// A slot holds the Jolt bodies of one live ragdoll and names its entry (task 0195).
        struct ActorSlot
        {
            bool mUsed = false;
            std::uint32_t mGeneration = 0;
            std::uint32_t mEntry = ~0u; // the entry (handle) this slot's ragdoll belongs to
            std::uint64_t mSettledStep = cNotSettled; // first step seen with every body asleep; reset when seen awake
            double mSpawnZ = 0.0;
            unsigned mSteps = 0; // physics steps since the spawn (counted only while the trace is on)
            bool mSettledLogged = false;
            std::optional<opas::motor::Ragdoll> mRagdoll;
            std::vector<opas::physics::BodyId> mBodyIds;
            mutable std::vector<opas::physics::BodyState> mStates;
        };

        /// One handle's identity (task 0195). Live: it names a slot. Frozen: the settled corpse's slot went to a new
        /// death, no Jolt body remains and the last pose is kept here.
        struct ActorEntry
        {
            bool mUsed = false;
            bool mFrozen = false;
            std::uint32_t mGeneration = 0;
            std::uint32_t mSlot = ~0u; // while live
            std::size_t mFrozenBodies = 0;
            double mFrozenLowestZ = 0.0;
            double mFrozenHighestZ = 0.0;
            std::array<RagdollBoneState, cMaxFrozenBones> mFrozenStates;
        };

        bool loadRigAndProfile(std::string* error);
        void summarize(const std::vector<opas::physics::BodyState>& states, RagdollStatus& result) const;
        // The two places game units and meters meet in this class.
        double metres(double units) const { return units / static_cast<double>(mUnitsPerMeter); }
        double fromMetres(double meters) const { return meters * static_cast<double>(mUnitsPerMeter); }
        // Destroys the slot's Jolt ragdoll and frees the slot; the entry is not touched.
        void destroySlotBodies(ActorSlot& slot);
        // destroySlotBodies, and the slot's entry is freed too (its handle goes stale).
        void clearActorSlot(ActorSlot& slot);
        // The entry of a valid handle, or nullptr.
        const ActorEntry* entryOf(ActorRagdollHandle handle) const;
        // The live slot of a valid handle, or nullptr (also for a frozen entry).
        const ActorSlot* liveSlotOf(ActorRagdollHandle handle) const;
        // Task 0195: the settled slot to give to a new death (smallest settle step, then lowest slot), or the table
        // size if no ragdoll is asleep now. Reads the bodies of the candidates; no allocation.
        std::size_t findSettledSlot();
        // Task 0195: keeps the slot's ragdoll as a frozen pose in its entry and frees the slot's bodies.
        void freezeSlot(std::size_t slotIndex);
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
        std::array<ActorEntry, cMaxActorRagdollEntries> mActorEntries;
        std::uint64_t mStepCounter = 0; // endStep calls, for the settle checks
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
