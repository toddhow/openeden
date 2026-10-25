#include <components/plugin/collision_layer_registry.hpp>
#include <openeden/physics.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using openeden::plugin::CollisionLayerRegistry;
    using openeden::sdk::CollisionLayer;

    TEST(CollisionLayersTest, NamedCollisionLayerRegistrationAssignsDeterministicBits)
    {
        CollisionLayerRegistry registry1;

        const CollisionLayer ragdoll1 = registry1.registerLayer("ragdoll");
        const CollisionLayer environment1 = registry1.registerLayer("environment");
        const CollisionLayer debris1 = registry1.registerLayer("debris");

        EXPECT_EQ(registry1.layerCount(), 3u);

        // Layers must receive distinct single-bit values
        const std::uint32_t bitRagdoll1 = registry1.getLayerBit("ragdoll");
        const std::uint32_t bitEnvironment1 = registry1.getLayerBit("environment");
        const std::uint32_t bitDebris1 = registry1.getLayerBit("debris");

        EXPECT_NE(bitRagdoll1, 0u);
        EXPECT_NE(bitEnvironment1, 0u);
        EXPECT_NE(bitDebris1, 0u);

        EXPECT_EQ(bitRagdoll1 & bitEnvironment1, 0u);
        EXPECT_EQ(bitRagdoll1 & bitDebris1, 0u);
        EXPECT_EQ(bitEnvironment1 & bitDebris1, 0u);

        // Verification of determinism on a second clean registry
        CollisionLayerRegistry registry2;
        const CollisionLayer ragdoll2 = registry2.registerLayer("ragdoll");
        const CollisionLayer environment2 = registry2.registerLayer("environment");
        const CollisionLayer debris2 = registry2.registerLayer("debris");

        EXPECT_EQ(ragdoll1, ragdoll2);
        EXPECT_EQ(environment1, environment2);
        EXPECT_EQ(debris1, debris2);

        EXPECT_EQ(registry2.getLayerBit("ragdoll"), bitRagdoll1);
        EXPECT_EQ(registry2.getLayerBit("environment"), bitEnvironment1);
        EXPECT_EQ(registry2.getLayerBit("debris"), bitDebris1);
    }

    TEST(CollisionLayersTest, DuplicateLayerRegistrationReturnsSameBit)
    {
        CollisionLayerRegistry registry;

        const CollisionLayer first = registry.registerLayer("ragdoll");
        const CollisionLayer duplicate = registry.registerLayer("ragdoll");

        EXPECT_EQ(first, duplicate);
        EXPECT_EQ(registry.layerCount(), 1u);
        EXPECT_EQ(registry.getLayerBit("ragdoll"), registry.getLayerBit("ragdoll"));
    }

    TEST(CollisionLayersTest, DeclaredCollisionPairEnablesSymmetricCollision)
    {
        CollisionLayerRegistry registry;

        registry.registerLayer("ragdoll");
        registry.registerLayer("environment");

        // Before declaration: undeclared pairs do not collide
        EXPECT_FALSE(registry.canCollide("ragdoll", "environment"));
        EXPECT_FALSE(registry.canCollide("environment", "ragdoll"));

        registry.declareCollisionPair("ragdoll", "environment");

        // Declaring ("ragdoll", "environment") enables collision symmetrically
        EXPECT_TRUE(registry.canCollide("ragdoll", "environment"));
        EXPECT_TRUE(registry.canCollide("environment", "ragdoll"));

        const std::uint32_t bitRagdoll = registry.getLayerBit("ragdoll");
        const std::uint32_t bitEnvironment = registry.getLayerBit("environment");
        EXPECT_TRUE(registry.canCollide(bitRagdoll, bitEnvironment));
        EXPECT_TRUE(registry.canCollide(bitEnvironment, bitRagdoll));
    }

    TEST(CollisionLayersTest, UndeclaredPairsDoNotCollide)
    {
        CollisionLayerRegistry registry;

        registry.registerLayer("ragdoll");
        registry.registerLayer("environment");
        registry.registerLayer("debris");

        // Declare only ragdoll <-> environment
        registry.declareCollisionPair("ragdoll", "environment");

        // Invariant: undeclared pairs do not collide
        EXPECT_FALSE(registry.canCollide("ragdoll", "debris"));
        EXPECT_FALSE(registry.canCollide("debris", "ragdoll"));
        EXPECT_FALSE(registry.canCollide("environment", "debris"));
        EXPECT_FALSE(registry.canCollide("debris", "environment"));
    }

    TEST(CollisionLayersTest, SymmetricCollisionMasksGeneratedCorrectly)
    {
        CollisionLayerRegistry registry;

        registry.registerLayer("ragdoll");
        registry.registerLayer("environment");
        registry.registerLayer("debris");

        registry.declareCollisionPair("ragdoll", "environment");
        registry.declareCollisionPair("ragdoll", "debris");

        const std::uint32_t bitRagdoll = registry.getLayerBit("ragdoll");
        const std::uint32_t bitEnvironment = registry.getLayerBit("environment");
        const std::uint32_t bitDebris = registry.getLayerBit("debris");

        const std::uint32_t maskRagdoll = registry.getLayerMask("ragdoll");
        const std::uint32_t maskEnvironment = registry.getLayerMask("environment");
        const std::uint32_t maskDebris = registry.getLayerMask("debris");

        // Ragdoll mask must contain environment and debris
        EXPECT_NE(maskRagdoll & bitEnvironment, 0u);
        EXPECT_NE(maskRagdoll & bitDebris, 0u);

        // Environment mask must contain ragdoll, but NOT debris
        EXPECT_NE(maskEnvironment & bitRagdoll, 0u);
        EXPECT_EQ(maskEnvironment & bitDebris, 0u);

        // Debris mask must contain ragdoll, but NOT environment
        EXPECT_NE(maskDebris & bitRagdoll, 0u);
        EXPECT_EQ(maskDebris & bitEnvironment, 0u);
    }

    TEST(CollisionLayersTest, SelfCollisionWhenExplicitlyDeclared)
    {
        CollisionLayerRegistry registry;

        registry.registerLayer("debris");
        registry.registerLayer("ragdoll");

        // Undeclared self-collision is disabled by default
        EXPECT_FALSE(registry.canCollide("debris", "debris"));
        EXPECT_FALSE(registry.canCollide("ragdoll", "ragdoll"));

        // Declare self-collision for debris
        registry.declareCollisionPair("debris", "debris");

        EXPECT_TRUE(registry.canCollide("debris", "debris"));
        EXPECT_FALSE(registry.canCollide("ragdoll", "ragdoll"));

        const std::uint32_t bitDebris = registry.getLayerBit("debris");
        const std::uint32_t maskDebris = registry.getLayerMask("debris");
        EXPECT_NE(maskDebris & bitDebris, 0u);
    }

    TEST(CollisionLayersTest, UnknownLayerQueryValidation)
    {
        CollisionLayerRegistry registry;
        registry.registerLayer("ragdoll");

        EXPECT_FALSE(registry.hasLayer("nonexistent"));
        EXPECT_EQ(registry.getLayerBit("nonexistent"), 0u);
        EXPECT_EQ(registry.getLayerMask("nonexistent"), 0u);
        EXPECT_FALSE(registry.canCollide("ragdoll", "nonexistent"));
    }

    TEST(CollisionLayersTest, LayerCapacityOverflowRejectedSafelyWithoutUB)
    {
        CollisionLayerRegistry registry;

        // Register maximum allowed layers (32)
        for (std::uint32_t i = 0; i < 32; ++i)
        {
            const std::string name = "layer_" + std::to_string(i);
            const std::uint32_t bit = registry.registerLayer(name);
            EXPECT_EQ(bit, 1u << i);
            EXPECT_TRUE(registry.hasLayer(name));
        }

        EXPECT_EQ(registry.layerCount(), 32u);

        // 33rd layer registration must be rejected safely with 0 without UB
        const std::uint32_t overflowBit = registry.registerLayer("overflow_layer");
        EXPECT_EQ(overflowBit, 0u);
        EXPECT_FALSE(registry.hasLayer("overflow_layer"));
        EXPECT_EQ(registry.layerCount(), 32u);
    }
} // namespace
