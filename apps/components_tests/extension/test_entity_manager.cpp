#include <components/extension/entity_manager.hpp>
#include <openeden/entity.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using openeden::extension::EntityManagerImpl;
    using openeden::sdk::EntityId;
    using openeden::sdk::EntityLifecycleEvent;
    using openeden::sdk::EntityLifecycleListener;
    using openeden::sdk::EntityManager;

    struct CustomData
    {
        int value = 0;
        std::string tag;
    };

    class TrackingLifecycleListener : public EntityLifecycleListener
    {
    public:
        struct Record
        {
            std::string listenerName;
            EntityLifecycleEvent event;
            EntityId id;
        };

        explicit TrackingLifecycleListener(std::string name, std::vector<Record>& log)
            : mName(std::move(name))
            , mLog(log)
        {
        }

        void onEntityLifecycle(EntityLifecycleEvent event, EntityId entityId) override
        {
            mLog.push_back({ mName, event, entityId });
        }

    private:
        std::string mName;
        std::vector<Record>& mLog;
    };

    TEST(EntityManagerTest, StableEntityIdGenerationAndValidation)
    {
        EntityManagerImpl manager;

        const EntityId e1 = manager.createEntity();
        EXPECT_TRUE(manager.isValid(e1));
        EXPECT_TRUE(e1.isValid());

        const EntityId e2 = manager.createEntity();
        EXPECT_TRUE(manager.isValid(e2));
        EXPECT_NE(e1, e2);

        // Invalid / default ID is not valid
        const EntityId invalidId{};
        EXPECT_FALSE(manager.isValid(invalidId));
        EXPECT_FALSE(invalidId.isValid());
    }

    TEST(EntityManagerTest, DespawnInvalidatesStaleEntityHandle)
    {
        EntityManagerImpl manager;

        const EntityId e1 = manager.createEntity();
        EXPECT_TRUE(manager.isValid(e1));

        EXPECT_TRUE(manager.destroyEntity(e1));
        EXPECT_FALSE(manager.isValid(e1));

        // Destroying an already destroyed handle returns false
        EXPECT_FALSE(manager.destroyEntity(e1));
    }

    TEST(EntityManagerTest, GenerationRecyclingInvalidatesStaleHandle)
    {
        EntityManagerImpl manager;

        const EntityId e1 = manager.createEntity();
        const std::uint32_t originalIndex = e1.index;
        const std::uint32_t originalGeneration = e1.generation;

        EXPECT_TRUE(manager.destroyEntity(e1));
        EXPECT_FALSE(manager.isValid(e1));

        // Create new entity which may recycle the slot index with a higher generation
        const EntityId e2 = manager.createEntity();
        EXPECT_TRUE(manager.isValid(e2));

        if (e2.index == originalIndex)
        {
            EXPECT_GT(e2.generation, originalGeneration);
            // Stale handle with old generation remains strictly invalid
            EXPECT_FALSE(manager.isValid(e1));
            EXPECT_NE(e1, e2);
        }
    }

    TEST(EntityManagerTest, LifecycleEventsDispatchedInExtensionLoadOrder)
    {
        EntityManagerImpl manager;
        // Load order: alpha -> beta -> gamma
        manager.setExtensionLoadOrder({ "extension_alpha", "extension_beta", "extension_gamma" });

        std::vector<TrackingLifecycleListener::Record> records;
        TrackingLifecycleListener listenerAlpha("extension_alpha", records);
        TrackingLifecycleListener listenerBeta("extension_beta", records);
        TrackingLifecycleListener listenerGamma("extension_gamma", records);

        // Register in arbitrary/reverse order: gamma, alpha, beta
        manager.registerLifecycleListener("extension_gamma", &listenerGamma);
        manager.registerLifecycleListener("extension_alpha", &listenerAlpha);
        manager.registerLifecycleListener("extension_beta", &listenerBeta);

        const EntityId e = manager.createEntity();

        constexpr std::array<EntityLifecycleEvent, 6> events = {
            EntityLifecycleEvent::Spawned,
            EntityLifecycleEvent::CellLoaded,
            EntityLifecycleEvent::Died,
            EntityLifecycleEvent::Revived,
            EntityLifecycleEvent::CellUnloaded,
            EntityLifecycleEvent::Despawned,
        };

        for (const auto evt : events)
        {
            records.clear();
            manager.dispatchLifecycleEvent(evt, e);

            ASSERT_EQ(records.size(), 3u);
            EXPECT_EQ(records[0].listenerName, "extension_alpha");
            EXPECT_EQ(records[1].listenerName, "extension_beta");
            EXPECT_EQ(records[2].listenerName, "extension_gamma");

            for (std::size_t i = 0; i < 3; ++i)
            {
                EXPECT_EQ(records[i].event, evt);
                EXPECT_EQ(records[i].id, e);
            }
        }
    }

    TEST(EntityManagerTest, PerEntityExtensionDataAttachAndGet)
    {
        EntityManagerImpl manager;
        const EntityId e = manager.createEntity();

        auto* data = new CustomData{ 42, "motion_capsule" };
        const bool attached = manager.attachData(
            e, "creativemotion.capsule", data, [](void* ptr) { delete static_cast<CustomData*>(ptr); });

        EXPECT_TRUE(attached);

        auto* retrieved = manager.getData<CustomData>(e, "creativemotion.capsule");
        ASSERT_NE(retrieved, nullptr);
        EXPECT_EQ(retrieved->value, 42);
        EXPECT_EQ(retrieved->tag, "motion_capsule");

        manager.destroyEntity(e);
    }

    TEST(EntityManagerTest, PerEntityExtensionDataRemoveInvokesDeleter)
    {
        EntityManagerImpl manager;
        const EntityId e = manager.createEntity();

        int dummy = 123;

        manager.attachData(e, "test_slot", &dummy, [](void* ptr) {
            auto* flag = static_cast<int*>(ptr);
            *flag = 999;
        });

        EXPECT_TRUE(manager.removeData(e, "test_slot"));
        EXPECT_EQ(dummy, 999);

        // Data is now null
        EXPECT_EQ(manager.getData(e, "test_slot"), nullptr);

        // Removing non-existent slot returns false
        EXPECT_FALSE(manager.removeData(e, "test_slot"));

        manager.destroyEntity(e);
    }

    TEST(EntityManagerTest, DespawnInvokesAllAttachedDataDeletersImmediately)
    {
        EntityManagerImpl manager;
        const EntityId e = manager.createEntity();

        int deletionCountA = 0;
        int deletionCountB = 0;

        manager.attachData(e, "extension_a.slot", &deletionCountA, [](void* ptr) { ++(*static_cast<int*>(ptr)); });

        manager.attachData(e, "extension_b.slot", &deletionCountB, [](void* ptr) { ++(*static_cast<int*>(ptr)); });

        EXPECT_EQ(deletionCountA, 0);
        EXPECT_EQ(deletionCountB, 0);

        // Invariant: when an entity despawns, any attached extension data slots have their custom deleter invoked
        // immediately
        manager.destroyEntity(e);

        EXPECT_EQ(deletionCountA, 1);
        EXPECT_EQ(deletionCountB, 1);

        // Accessing data on despawned entity returns nullptr
        EXPECT_EQ(manager.getData(e, "extension_a.slot"), nullptr);
        EXPECT_EQ(manager.getData(e, "extension_b.slot"), nullptr);
    }

    TEST(EntityManagerTest, AttachDataToInvalidOrDespawnedEntityFails)
    {
        EntityManagerImpl manager;
        const EntityId e = manager.createEntity();
        manager.destroyEntity(e);

        int dummy = 0;

        const bool result = manager.attachData(e, "slot", &dummy, [](void* /*ptr*/) {});
        EXPECT_FALSE(result);
        EXPECT_EQ(manager.getData(e, "slot"), nullptr);

        const EntityId invalidId{};
        EXPECT_FALSE(manager.attachData(invalidId, "slot", &dummy, [](void* /*ptr*/) {}));
    }

    TEST(EntityManagerTest, MultipleExtensionsAttachDistinctSlotsToSameEntity)
    {
        EntityManagerImpl manager;
        const EntityId e = manager.createEntity();

        auto* ragdoll = new CustomData{ 1, "ragdoll" };
        auto* audio = new CustomData{ 2, "audio_emitter" };

        manager.attachData(e, "extension_motion.ragdoll", ragdoll);
        manager.attachData(e, "extension_audio.emitter", audio);

        auto* getRagdoll = manager.getData<CustomData>(e, "extension_motion.ragdoll");
        auto* getAudio = manager.getData<CustomData>(e, "extension_audio.emitter");

        ASSERT_NE(getRagdoll, nullptr);
        ASSERT_NE(getAudio, nullptr);
        EXPECT_EQ(getRagdoll->tag, "ragdoll");
        EXPECT_EQ(getAudio->tag, "audio_emitter");

        // Removing one slot does not disturb the other
        manager.removeData(e, "extension_motion.ragdoll");
        EXPECT_EQ(manager.getData(e, "extension_motion.ragdoll"), nullptr);
        EXPECT_NE(manager.getData(e, "extension_audio.emitter"), nullptr);

        manager.destroyEntity(e);
    }

    TEST(EntityManagerTest, ReplacingExistingSlotInvokesOldDeleter)
    {
        EntityManagerImpl manager;
        const EntityId e = manager.createEntity();

        int dummy1 = 1;
        int dummy2 = 2;

        manager.attachData(e, "shared_slot", &dummy1, [](void* ptr) { *static_cast<int*>(ptr) = 777; });

        // Re-attaching to same slot key replaces data and destroys previous content
        manager.attachData(e, "shared_slot", &dummy2, [](void* ptr) { *static_cast<int*>(ptr) = 888; });

        EXPECT_EQ(dummy1, 777);
        EXPECT_EQ(manager.getData(e, "shared_slot"), &dummy2);

        manager.destroyEntity(e);
        EXPECT_EQ(dummy2, 888);
    }

    TEST(EntityManagerTest, ReentrantDespawnSafelyHandledWithoutDoubleFree)
    {
        EntityManagerImpl manager;
        const EntityId e = manager.createEntity();

        int deleterInvocations = 0;
        manager.attachData(e, "test.slot", &deleterInvocations, [](void* ptr) { ++(*static_cast<int*>(ptr)); });

        bool reentrantDestroyResult = true;

        class ReentrantListener : public EntityLifecycleListener
        {
        public:
            EntityManagerImpl& mgr;
            bool& reentrantResult;

            ReentrantListener(EntityManagerImpl& m, bool& res)
                : mgr(m)
                , reentrantResult(res)
            {
            }

            void onEntityLifecycle(EntityLifecycleEvent event, EntityId id) override
            {
                if (event == EntityLifecycleEvent::Despawned)
                {
                    // Re-entrant call attempting to destroy the entity again during its own despawn
                    reentrantResult = mgr.destroyEntity(id);
                }
            }
        };

        ReentrantListener listener(manager, reentrantDestroyResult);
        manager.registerLifecycleListener("reentrant_extension", &listener);

        const bool destroyed = manager.destroyEntity(e);
        EXPECT_TRUE(destroyed);
        EXPECT_FALSE(reentrantDestroyResult); // Re-entrant call must return false
        EXPECT_EQ(deleterInvocations, 1); // Deleter called exactly once (no double-free)
    }

    TEST(EntityManagerTest, ExceptionInLifecycleListenerContained)
    {
        EntityManagerImpl manager;
        const EntityId e = manager.createEntity();

        class ThrowingListener : public EntityLifecycleListener
        {
        public:
            void onEntityLifecycle(EntityLifecycleEvent /*event*/, EntityId /*id*/) override
            {
                throw std::runtime_error("Listener failed");
            }
        };

        class NormalListener : public EntityLifecycleListener
        {
        public:
            bool called = false;
            void onEntityLifecycle(EntityLifecycleEvent /*event*/, EntityId /*id*/) override { called = true; }
        };

        ThrowingListener thrower;
        NormalListener normal;

        manager.registerLifecycleListener("extension_a", &thrower);
        manager.registerLifecycleListener("extension_b", &normal);

        // Dispatched event must not crash or escape the call
        EXPECT_NO_THROW(manager.dispatchLifecycleEvent(EntityLifecycleEvent::Spawned, e));
        EXPECT_TRUE(normal.called);

        manager.destroyEntity(e);
    }

    // Task 0017: the game answers what a test host cannot.
    class FakeEntityWorld final : public openeden::extension::EntityWorld
    {
    public:
        [[nodiscard]] EntityId playerEntity() const override
        {
            ++mCalls;
            return mPlayer;
        }

        [[nodiscard]] std::string recordId(EntityId id) const override
        {
            ++mCalls;
            return id == mPlayer ? "player" : std::string();
        }

        EntityId mPlayer{};
        mutable int mCalls = 0;
    };

    /// Implements only what EntityManager requires, to reach the SDK base's defaults.
    class PlainEntityManager final : public EntityManager
    {
    public:
        EntityId createEntity() override { return {}; }
        bool destroyEntity(EntityId) override { return false; }
        [[nodiscard]] bool isValid(EntityId) const override { return false; }
        bool attachData(EntityId, std::string_view, void*, openeden::sdk::EntityDataDeleter) override { return false; }
        [[nodiscard]] void* getData(EntityId, std::string_view) const override { return nullptr; }
        bool removeData(EntityId, std::string_view) override { return false; }
        void registerLifecycleListener(std::string_view, EntityLifecycleListener*) override {}
        void unregisterLifecycleListener(std::string_view) override {}
        void dispatchLifecycleEvent(EntityLifecycleEvent, EntityId) override {}
    };

    TEST(EntityManagerWorldTest, PlayerAndRecordComeFromTheWorld)
    {
        EntityManagerImpl manager;
        FakeEntityWorld world;
        world.mPlayer = manager.createEntity();
        manager.setWorld(&world);

        EXPECT_EQ(manager.playerEntity(), world.mPlayer);
        EXPECT_EQ(manager.recordId(world.mPlayer), "player");

        manager.destroyEntity(world.mPlayer);
    }

    TEST(EntityManagerWorldTest, AnswersThroughTheSdkInterface)
    {
        EntityManagerImpl manager;
        FakeEntityWorld world;
        world.mPlayer = manager.createEntity();
        manager.setWorld(&world);

        const EntityManager& sdk = manager;
        EXPECT_EQ(sdk.playerEntity(), world.mPlayer);
        EXPECT_EQ(sdk.recordId(world.mPlayer), "player");

        manager.destroyEntity(world.mPlayer);
    }

    TEST(EntityManagerWorldTest, WithoutAWorldThePlayerIsInvalidAndRecordsEmpty)
    {
        EntityManagerImpl manager;
        const EntityId entity = manager.createEntity();

        EXPECT_FALSE(manager.playerEntity().isValid());
        EXPECT_TRUE(manager.recordId(entity).empty());

        manager.destroyEntity(entity);
    }

    TEST(EntityManagerWorldTest, ClearingTheWorldStopsAskingIt)
    {
        EntityManagerImpl manager;
        FakeEntityWorld world;
        world.mPlayer = manager.createEntity();
        manager.setWorld(&world);
        manager.setWorld(nullptr);

        EXPECT_FALSE(manager.playerEntity().isValid());
        EXPECT_TRUE(manager.recordId(world.mPlayer).empty());
        EXPECT_EQ(world.mCalls, 0);

        manager.destroyEntity(world.mPlayer);
    }

    TEST(EntityManagerWorldTest, SdkBaseDefaultsAreInvalidAndEmpty)
    {
        const PlainEntityManager manager;
        const EntityManager& sdk = manager;

        EXPECT_FALSE(sdk.playerEntity().isValid());
        EXPECT_TRUE(sdk.recordId(EntityId{ 1, 1 }).empty());
    }
} // namespace
