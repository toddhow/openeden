// Tests for the object layers of the game's Jolt world, stage b of task 0035: the new RAGDOLL layer.
// Written from the task file's "Stage b" and "Stage b as decided" sections and the acceptance criterion
// "(b) A layer test", before the code exists.
//
// In plain words: Jolt asks three small questions about every pair of things in the world. (1) Which broad-phase
// tree does an object live in (GetBroadPhaseLayer)? (2) May an object of this layer even look into that tree
// (ObjectVsBroadPhaseLayerFilter)? (3) May these two layers touch (ObjectLayerPairFilter)? The ragdoll layer must
// answer them as the spec says, and every older layer must answer exactly as before.

#include "apps/openmw/mwphysics/joltlayers.hpp"

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace
{
    using namespace MWPhysics;

    // All 12 layers, the 11 old ones first, in the order of joltlayers.hpp.
    constexpr std::array<JPH::ObjectLayer, 12> kAllLayers = { Layers::WORLD, Layers::DOOR, Layers::ACTOR,
        Layers::HEIGHTMAP, Layers::PROJECTILE, Layers::WATER, Layers::CAMERA_ONLY, Layers::VISUAL_ONLY,
        Layers::DYNAMIC_WORLD, Layers::DEBRIS, Layers::SENSOR, Layers::RAGDOLL };
    constexpr std::size_t kOldLayerCount = 11;

    // Only for failure messages.
    const char* layerName(JPH::ObjectLayer layer)
    {
        if (layer == Layers::WORLD)
            return "WORLD";
        if (layer == Layers::DOOR)
            return "DOOR";
        if (layer == Layers::ACTOR)
            return "ACTOR";
        if (layer == Layers::HEIGHTMAP)
            return "HEIGHTMAP";
        if (layer == Layers::PROJECTILE)
            return "PROJECTILE";
        if (layer == Layers::WATER)
            return "WATER";
        if (layer == Layers::CAMERA_ONLY)
            return "CAMERA_ONLY";
        if (layer == Layers::VISUAL_ONLY)
            return "VISUAL_ONLY";
        if (layer == Layers::DYNAMIC_WORLD)
            return "DYNAMIC_WORLD";
        if (layer == Layers::DEBRIS)
            return "DEBRIS";
        if (layer == Layers::SENSOR)
            return "SENSOR";
        if (layer == Layers::RAGDOLL)
            return "RAGDOLL";
        return "?";
    }

    bool contains(const std::vector<JPH::ObjectLayer>& list, JPH::ObjectLayer layer)
    {
        for (const JPH::ObjectLayer item : list)
            if (item == layer)
                return true;
        return false;
    }

    // The OLD pair filter (before RAGDOLL), copied from `git show HEAD:apps/openmw/mwphysics/joltlayers.hpp` as data:
    // for each first layer, the second layers for which ShouldCollide(first, second) was true. The old table is not
    // symmetric everywhere (for example DYNAMIC_WORLD meets SENSOR but SENSOR does not meet DYNAMIC_WORLD), so the
    // tests compare ordered pairs and do not "fix" it.
    struct OldPairRow
    {
        JPH::ObjectLayer first;
        std::vector<JPH::ObjectLayer> collidesWith;
    };

    const std::vector<OldPairRow>& oldPairTable()
    {
        static const std::vector<JPH::ObjectLayer> staticRow
            = { Layers::DYNAMIC_WORLD, Layers::ACTOR, Layers::PROJECTILE, Layers::DEBRIS };
        static const std::vector<JPH::ObjectLayer> movingRow = { Layers::WORLD, Layers::HEIGHTMAP, Layers::DOOR,
            Layers::WATER, Layers::ACTOR, Layers::PROJECTILE, Layers::DYNAMIC_WORLD, Layers::SENSOR };
        static const std::vector<OldPairRow> table = {
            { Layers::WORLD, staticRow },
            { Layers::DOOR, staticRow },
            { Layers::WATER, staticRow },
            { Layers::HEIGHTMAP, staticRow },
            { Layers::DYNAMIC_WORLD, movingRow },
            { Layers::PROJECTILE, movingRow },
            { Layers::ACTOR, movingRow },
            { Layers::SENSOR, { Layers::SENSOR, Layers::PROJECTILE, Layers::ACTOR } },
            { Layers::DEBRIS, { Layers::WORLD, Layers::HEIGHTMAP } },
            { Layers::CAMERA_ONLY, {} },
            { Layers::VISUAL_ONLY, {} },
        };
        return table;
    }

    // The OLD object-versus-broad-phase filter as data: for each object layer, the broad phases it could look into.
    struct OldBroadPhaseRow
    {
        JPH::ObjectLayer layer;
        std::vector<JPH::BroadPhaseLayer> lookInto;
    };

    const std::vector<OldBroadPhaseRow>& oldBroadPhaseTable()
    {
        static const std::vector<JPH::BroadPhaseLayer> actorRow
            = { BroadPhaseLayers::WORLD, BroadPhaseLayers::MOVING, BroadPhaseLayers::SENSOR };
        static const std::vector<OldBroadPhaseRow> table = {
            { Layers::WORLD, { BroadPhaseLayers::MOVING } },
            { Layers::ACTOR, actorRow },
            { Layers::PROJECTILE, actorRow },
            { Layers::DOOR, { BroadPhaseLayers::DEBRIS, BroadPhaseLayers::MOVING } },
            { Layers::DEBRIS, { BroadPhaseLayers::WORLD } },
            { Layers::SENSOR, { BroadPhaseLayers::MOVING, BroadPhaseLayers::SENSOR } },
            { Layers::HEIGHTMAP, {} },
            { Layers::WATER, {} },
            { Layers::CAMERA_ONLY, {} },
            { Layers::VISUAL_ONLY, {} },
            { Layers::DYNAMIC_WORLD, {} },
        };
        return table;
    }

    const std::array<JPH::BroadPhaseLayer, 4> kAllBroadPhases
        = { BroadPhaseLayers::WORLD, BroadPhaseLayers::MOVING, BroadPhaseLayers::DEBRIS, BroadPhaseLayers::SENSOR };

    // The old broad phase of the 11 old layers: SENSOR and DEBRIS have their own, ACTOR and PROJECTILE are MOVING,
    // the rest are WORLD.
    JPH::BroadPhaseLayer oldBroadPhaseOf(JPH::ObjectLayer layer)
    {
        if (layer == Layers::SENSOR)
            return BroadPhaseLayers::SENSOR;
        if (layer == Layers::DEBRIS)
            return BroadPhaseLayers::DEBRIS;
        if (layer == Layers::ACTOR || layer == Layers::PROJECTILE)
            return BroadPhaseLayers::MOVING;
        return BroadPhaseLayers::WORLD;
    }

    TEST(JoltLayers, RagdollLayerIsBit11)
    {
        // The new layer is 1 << 11, after SENSOR (1 << 10).
        EXPECT_EQ(Layers::RAGDOLL, static_cast<JPH::ObjectLayer>(1 << 11));
    }

    TEST(JoltLayers, RagdollLayerIsInTheMovingBroadPhase)
    {
        const JoltBPLayerInterface layerInterface;
        EXPECT_TRUE(layerInterface.GetBroadPhaseLayer(Layers::RAGDOLL) == BroadPhaseLayers::MOVING);
    }

    TEST(JoltLayers, OldLayersKeepTheirBroadPhase)
    {
        const JoltBPLayerInterface layerInterface;
        for (std::size_t i = 0; i < kOldLayerCount; ++i)
            EXPECT_TRUE(layerInterface.GetBroadPhaseLayer(kAllLayers[i]) == oldBroadPhaseOf(kAllLayers[i]))
                << layerName(kAllLayers[i]);
    }

    TEST(JoltLayers, PairsWithRagdollAreSymmetric)
    {
        // Jolt needs the answer not to depend on the order. (The old table has a few asymmetric pairs between old
        // layers; the spec only demands symmetry for the new layer, so only RAGDOLL pairs are checked here.)
        const JoltObjectLayerPairFilter filter;
        for (const JPH::ObjectLayer other : kAllLayers)
            EXPECT_EQ(filter.ShouldCollide(Layers::RAGDOLL, other), filter.ShouldCollide(other, Layers::RAGDOLL))
                << layerName(other);
    }

    TEST(JoltLayers, RagdollCollidesWithExactlyWorldHeightmapDoorRagdoll)
    {
        const JoltObjectLayerPairFilter filter;
        const std::vector<JPH::ObjectLayer> expected
            = { Layers::WORLD, Layers::HEIGHTMAP, Layers::DOOR, Layers::RAGDOLL };
        for (const JPH::ObjectLayer other : kAllLayers)
        {
            EXPECT_EQ(filter.ShouldCollide(Layers::RAGDOLL, other), contains(expected, other)) << layerName(other);
            EXPECT_EQ(filter.ShouldCollide(other, Layers::RAGDOLL), contains(expected, other)) << layerName(other);
        }
    }

    TEST(JoltLayers, OldPairsAnswerAsBefore)
    {
        // Every ordered pair of the previous 11 layers has the old answer.
        const JoltObjectLayerPairFilter filter;
        ASSERT_EQ(oldPairTable().size(), kOldLayerCount);
        for (const OldPairRow& row : oldPairTable())
            for (std::size_t i = 0; i < kOldLayerCount; ++i)
            {
                const JPH::ObjectLayer second = kAllLayers[i];
                EXPECT_EQ(filter.ShouldCollide(row.first, second), contains(row.collidesWith, second))
                    << layerName(row.first) << " vs " << layerName(second);
            }
    }

    TEST(JoltLayers, RagdollPairsAreAlsoAllowedByTheBroadPhaseFilter)
    {
        // If two layers may touch, the ragdoll's query must be allowed to look into the other one's broad phase.
        const JoltBPLayerInterface layerInterface;
        const JoltObjectLayerPairFilter pairs;
        const JoltObjectVsBroadPhaseLayerFilter broadPhase;
        for (const JPH::ObjectLayer other : kAllLayers)
        {
            if (pairs.ShouldCollide(Layers::RAGDOLL, other))
            {
                EXPECT_TRUE(broadPhase.ShouldCollide(Layers::RAGDOLL, layerInterface.GetBroadPhaseLayer(other)))
                    << layerName(other);
            }
        }
    }

    TEST(JoltLayers, RagdollLooksIntoWorldAndMovingBroadPhasesOnly)
    {
        const JoltObjectVsBroadPhaseLayerFilter broadPhase;
        for (const JPH::BroadPhaseLayer layer : kAllBroadPhases)
        {
            const bool expected = layer == BroadPhaseLayers::WORLD || layer == BroadPhaseLayers::MOVING;
            EXPECT_EQ(broadPhase.ShouldCollide(Layers::RAGDOLL, layer), expected) << int(layer.GetValue());
        }
    }

    TEST(JoltLayers, OldBroadPhaseAnswersAreUnchanged)
    {
        const JoltObjectVsBroadPhaseLayerFilter broadPhase;
        ASSERT_EQ(oldBroadPhaseTable().size(), kOldLayerCount);
        for (const OldBroadPhaseRow& row : oldBroadPhaseTable())
            for (const JPH::BroadPhaseLayer layer : kAllBroadPhases)
            {
                bool expected = false;
                for (const JPH::BroadPhaseLayer allowed : row.lookInto)
                    expected = expected || allowed == layer;
                EXPECT_EQ(broadPhase.ShouldCollide(row.layer, layer), expected)
                    << layerName(row.layer) << " into broad phase " << int(layer.GetValue());
            }
    }

    TEST(JoltLayers, ExistingQueryMasksDoNotContainRagdoll)
    {
        // Existing game queries must never see a ragdoll.
        EXPECT_EQ(static_cast<unsigned>(CollisionMask_Default) & static_cast<unsigned>(Layers::RAGDOLL), 0u);
        EXPECT_EQ(static_cast<unsigned>(CollisionMask_AnyPhysical) & static_cast<unsigned>(Layers::RAGDOLL), 0u);
    }
}
