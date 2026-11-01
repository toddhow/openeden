#ifndef OPENEDEN_OPENMW_TESTS_MWPHYSICS_RAGDOLLTESTWORLD_H
#define OPENEDEN_OPENMW_TESTS_MWPHYSICS_RAGDOLLTESTWORLD_H

// Shared helpers for the stage b tests of task 0035 (layers, gravity factor, RagdollHost).
//
// The tests play the part of the game's PhysicsSystem class without using it: GameLikeWorld owns a plain
// JPH::PhysicsSystem set up the way PhysicsSystem::PhysicsSystem sets up the game's (the game's three layer classes
// from joltlayers.hpp, Jolt gravity pointing UP the Z axis with the size of the game's gravity) and, if asked, a
// static floor on Layers::WORLD whose top face is at z = 0 (game units).

// Jolt requires Jolt.h before any other Jolt header.
// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
// clang-format on

#include "apps/openmw/mwphysics/joltlayers.hpp"

#include <components/misc/constants.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace MWPhysicsTest
{
    constexpr float kStepSeconds = 1.0f / 60.0f;

    // Jolt's global state (allocator, factory, types) exists once per process. openmw-tests' main() does not set it
    // up, so the first test that needs it does. Nothing is torn down: other tests share it.
    inline void ensureJoltInitialized()
    {
        if (JPH::Factory::sInstance != nullptr)
            return;
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }

    // Jolt gravity exactly as the game sets it: +Z, GravityConst metres per second squared turned into units.
    inline JPH::Vec3 gameGravity()
    {
        return JPH::Vec3(0.0f, 0.0f, +Constants::GravityConst * Constants::UnitsPerMeter);
    }

    class GameLikeWorld
    {
    public:
        explicit GameLikeWorld(bool withFloor = true, JPH::Vec3 gravity = gameGravity())
            : mTempAllocator(10 * 1024 * 1024)
            , mJobSystem(JPH::cMaxPhysicsJobs)
        {
            mSystem.Init(kMaxBodies, 0, kMaxBodyPairs, kMaxContactConstraints, mBroadPhaseLayers, mObjectVsBroadPhase,
                mObjectPairs);
            mSystem.SetGravity(gravity);
            if (withFloor)
            {
                // A big slab: half extents 2000 x 2000 x 50 units, centre 50 units down, so the top is at z = 0.
                const JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(JPH::Vec3(2000.0f, 2000.0f, 50.0f));
                const JPH::BodyCreationSettings settings(shape, JPH::RVec3(0.0, 0.0, -50.0), JPH::Quat::sIdentity(),
                    JPH::EMotionType::Static, MWPhysics::Layers::WORLD);
                mFloor = mSystem.GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
            }
        }

        JPH::PhysicsSystem& system() { return mSystem; }
        const JPH::PhysicsSystem& system() const { return mSystem; }
        JPH::BodyInterface& bodies() { return mSystem.GetBodyInterface(); }
        JPH::BodyID floorId() const { return mFloor; }

        // One collision step of 1/60 s, like the game's stepSimulation loop.
        void update(float dtSeconds = kStepSeconds) { mSystem.Update(dtSeconds, 1, &mTempAllocator, &mJobSystem); }

        std::size_t numBodies() const { return mSystem.GetNumBodies(); }
        std::size_t numConstraints() const { return mSystem.GetConstraints().size(); }

        std::vector<JPH::BodyID> allBodies() const
        {
            JPH::BodyIDVector ids;
            mSystem.GetBodies(ids);
            return std::vector<JPH::BodyID>(ids.begin(), ids.end());
        }

        // The bodies in the system that are not in `before` (finds what a ragdoll created).
        std::vector<JPH::BodyID> bodiesAddedSince(const std::vector<JPH::BodyID>& before) const
        {
            std::vector<JPH::BodyID> result;
            for (const JPH::BodyID id : allBodies())
                if (std::find(before.begin(), before.end(), id) == before.end())
                    result.push_back(id);
            return result;
        }

    private:
        static constexpr unsigned int kMaxBodies = 1024;
        static constexpr unsigned int kMaxBodyPairs = 4096;
        static constexpr unsigned int kMaxContactConstraints = 4096;

        // Must be the FIRST member: members are built in declaration order, and the temp allocator below needs
        // Jolt's allocator registered before it is constructed.
        struct JoltInitializer
        {
            JoltInitializer() { ensureJoltInitialized(); }
        };
        JoltInitializer mJoltInitializer;

        // Declared before the system so that they outlive it.
        MWPhysics::JoltBPLayerInterface mBroadPhaseLayers;
        MWPhysics::JoltObjectVsBroadPhaseLayerFilter mObjectVsBroadPhase;
        MWPhysics::JoltObjectLayerPairFilter mObjectPairs;
        JPH::TempAllocatorImpl mTempAllocator;
        JPH::JobSystemSingleThreaded mJobSystem;
        JPH::PhysicsSystem mSystem;
        JPH::BodyID mFloor;
    };
}

#endif
