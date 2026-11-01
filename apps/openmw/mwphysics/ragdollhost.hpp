#ifndef OPENMW_MWPHYSICS_RAGDOLLHOST_H
#define OPENMW_MWPHYSICS_RAGDOLLHOST_H

#include <Jolt/Jolt.h>

#include <Jolt/Physics/PhysicsSystem.h>

#include <components/cmphysics/hostedjoltworld.hpp>

#include <opas/motor/Ragdoll.hpp>
#include <opas/physics/PhysicsWorld.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace MWPhysics
{
    /// Where the debug ragdoll is removed on its own: its lowest body this far below its spawn point (metres).
    constexpr float cRagdollKillDepthMetres = 20.0f;

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

    private:
        class LayerFilter;
        class BroadPhaseFilter;

        explicit RagdollHost(float unitsPerMeter);

        float mUnitsPerMeter;
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
