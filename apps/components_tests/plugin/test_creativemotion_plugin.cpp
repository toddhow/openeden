#include <components/plugin/host_impl.hpp>
#include <components/plugin/lifecycle_manager.hpp>
#include <components/plugin/manifest_io.hpp>
#include <components/plugin/plugin_loader.hpp>
#include <openeden/contracts/animation_contract.hpp>
#include <openeden/phases.hpp>
#include <openeden/physics.hpp>
#include <plugins/creativemotion/creativemotion_plugin.hpp>
#include <plugins/creativemotion/host_physics_world.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace openeden::sdk;
    using namespace openeden::plugin;
    using namespace openeden::contracts;
    using namespace openeden::creativemotion;

    std::filesystem::path findCreativeMotionManifestPath()
    {
#ifdef OPENEDEN_SOURCE_DIR
        const std::filesystem::path fromMacro
            = std::filesystem::path(OPENEDEN_SOURCE_DIR) / "plugins" / "creativemotion" / "plugin.json";
        if (std::filesystem::exists(fromMacro))
            return fromMacro;
#endif
        std::filesystem::path cur = std::filesystem::current_path();
        for (int depth = 0; depth < 8; ++depth)
        {
            const std::filesystem::path candidate = cur / "plugins" / "creativemotion" / "plugin.json";
            if (std::filesystem::exists(candidate))
                return candidate;
            if (!cur.has_parent_path() || cur.parent_path() == cur)
                break;
            cur = cur.parent_path();
        }
        return std::filesystem::path("plugins/creativemotion/plugin.json");
    }

    PluginManifest loadCreativeMotionManifest()
    {
        LoadError err{};
        const auto path = findCreativeMotionManifestPath();
        auto manifestOpt = loadManifestFile(path, err);
        EXPECT_TRUE(manifestOpt.has_value()) << err.message;
        return manifestOpt.value_or(PluginManifest{});
    }

    class AnimationConsumerPlugin final : public Plugin
    {
    public:
        void registerServices(Registrar& /*registrar*/) override {}

        bool resolve(const ServiceLocator& locator) override
        {
            mAnimation = locator.find<AnimationContract>();
            return mAnimation != nullptr;
        }

        void start(Host& /*host*/) override { mStarted = true; }

        void stop() override
        {
            mStopped = true;
            mAnimation = nullptr;
        }

        [[nodiscard]] AnimationContract* animation() const noexcept { return mAnimation; }
        [[nodiscard]] bool started() const noexcept { return mStarted; }
        [[nodiscard]] bool stopped() const noexcept { return mStopped; }

    private:
        AnimationContract* mAnimation = nullptr;
        bool mStarted = false;
        bool mStopped = false;
    };

    TEST(CreativeMotionPluginTest, ManifestValidationAndCanonicalRoundTrip)
    {
        const auto manifestPath = findCreativeMotionManifestPath();
        ASSERT_TRUE(std::filesystem::exists(manifestPath)) << "Missing: " << manifestPath.string();

        LoadError error{};
        const auto manifestOpt = loadManifestFile(manifestPath, error);
        ASSERT_TRUE(manifestOpt.has_value()) << error.message;

        const PluginManifest& manifest = *manifestOpt;
        EXPECT_EQ(manifest.format, "openeden.plugin");
        EXPECT_EQ(manifest.version, 1);
        EXPECT_EQ(manifest.id, "creativemotion");
        EXPECT_EQ(manifest.name, "CreativeMotion");
        EXPECT_EQ(manifest.kind, "feature");

        ASSERT_EQ(manifest.providedContracts.size(), 1u);
        EXPECT_EQ(manifest.providedContracts[0].contract, "openeden.animation");
        EXPECT_EQ(manifest.providedContracts[0].version, (Version{ 1, 1 }));

        ASSERT_EQ(manifest.settings.size(), 1u);
        EXPECT_EQ(manifest.settings[0].key, "enabled");
        EXPECT_EQ(manifest.settings[0].type, "bool");
        EXPECT_FALSE(manifest.settings[0].defaultBool);

        // Verify canonical writer round-trips cleanly
        const std::string canonicalJson = writeCanonicalManifest(manifest);
        LoadError roundTripErr{};
        const auto roundTripped = parseManifest(canonicalJson, "plugin.json", roundTripErr);
        ASSERT_TRUE(roundTripped.has_value()) << roundTripErr.message;
        EXPECT_EQ(writeCanonicalManifest(*roundTripped), canonicalJson);
    }

    TEST(CreativeMotionPluginTest, PluginLifecycleAndContractRegistration)
    {
        HostImpl host;
        LifecycleManager lifecycle(host);

        auto cmPlugin = std::make_unique<CreativeMotionPlugin>();
        CreativeMotionPlugin* cmRaw = cmPlugin.get();
        lifecycle.addPlugin(loadCreativeMotionManifest(), std::move(cmPlugin));

        PluginManifest consumerManifest{};
        consumerManifest.id = "test_animation_consumer";
        consumerManifest.name = "Test Animation Consumer";
        consumerManifest.pluginVersion = "1.0.0";
        consumerManifest.requiredContracts.push_back({ "openeden.animation", Version{ 1, 1 } });

        auto consumerPlugin = std::make_unique<AnimationConsumerPlugin>();
        AnimationConsumerPlugin* consumerRaw = consumerPlugin.get();
        lifecycle.addPlugin(consumerManifest, std::move(consumerPlugin));

        ASSERT_TRUE(lifecycle.initialize());
        EXPECT_TRUE(lifecycle.isPluginActive("creativemotion"));
        EXPECT_TRUE(lifecycle.isPluginActive("test_animation_consumer"));
        EXPECT_TRUE(consumerRaw->started());
        EXPECT_EQ(consumerRaw->animation(), static_cast<AnimationContract*>(cmRaw));

        lifecycle.shutdown();
        EXPECT_TRUE(consumerRaw->stopped());
        EXPECT_FALSE(lifecycle.isPluginActive("creativemotion"));
        EXPECT_FALSE(lifecycle.isPluginActive("test_animation_consumer"));
    }

    TEST(CreativeMotionPluginTest, OffByDefaultInvariant)
    {
        HostImpl host;
        LifecycleManager lifecycle(host);

        auto cmPlugin = std::make_unique<CreativeMotionPlugin>();
        CreativeMotionPlugin* cmRaw = cmPlugin.get();
        lifecycle.addPlugin(loadCreativeMotionManifest(), std::move(cmPlugin));

        ASSERT_TRUE(lifecycle.initialize());

        // Off-by-default setting must hold immediately after startup
        EXPECT_FALSE(cmRaw->isEnabled());
        EXPECT_EQ(cmRaw->getActiveCharacterCount(), 0u);
        EXPECT_EQ(cmRaw->getFixedStepCount(), 0u);

        // Place a sentinel dynamic body in the shared physics world to verify CreativeMotion
        // does not alter or interfere with the world when disabled
        BodyDesc sentinelDesc{};
        sentinelDesc.shape = SphereShape{ 0.25f };
        sentinelDesc.motion = MotionType::Static;
        sentinelDesc.positionM = { 4.0f, 5.0f, 6.0f };
        const auto sentinel = host.getPhysicsService()->createBody(sentinelDesc);
        ASSERT_TRUE(sentinel.ok());

        const float dt = host.getPhysicsService()->fixedStepSeconds();
        lifecycle.runFixedStepPhase(Phase::FixedStepPrePhysics, dt, 0);
        host.getPhysicsService()->step(dt);
        lifecycle.runFixedStepPhase(Phase::FixedStepPostPhysics, dt, 0);
        lifecycle.runPhase(Phase::PostSimulation, dt);
        lifecycle.runPhase(Phase::Animation, dt);
        lifecycle.runPhase(Phase::PreRender, dt);
        lifecycle.runPhase(Phase::Ui, dt);

        // When disabled, fixed-step count is not incremented and sentinel body is untouched
        EXPECT_EQ(cmRaw->getFixedStepCount(), 0u);
        BodyState sentinelState{};
        ASSERT_TRUE(host.getPhysicsService()->readBody(sentinel.id, sentinelState));
        EXPECT_NEAR(sentinelState.positionM.x, 4.0f, 1e-5f);
        EXPECT_NEAR(sentinelState.positionM.y, 5.0f, 1e-5f);
        EXPECT_NEAR(sentinelState.positionM.z, 6.0f, 1e-5f);

        lifecycle.shutdown();
    }

    TEST(CreativeMotionPluginTest, HostPhysicsWorldAdaptsSharedPhysicsService)
    {
        HostImpl host;
        LifecycleManager lifecycle(host);

        auto cmPlugin = std::make_unique<CreativeMotionPlugin>();
        CreativeMotionPlugin* cmRaw = cmPlugin.get();
        lifecycle.addPlugin(loadCreativeMotionManifest(), std::move(cmPlugin));
        ASSERT_TRUE(lifecycle.initialize());

        HostPhysicsWorld* adapter = cmRaw->getHostPhysicsWorld();
        ASSERT_NE(adapter, nullptr);
        EXPECT_EQ(&adapter->service(), host.getPhysicsService());

        // Create a body through CreativeMotion's opas::physics::PhysicsWorld adapter
        opas::physics::BodyDesc cmBodyDesc{};
        cmBodyDesc.shape = opas::physics::SphereShape{ 0.35f };
        cmBodyDesc.motion = opas::physics::MotionType::Dynamic;
        cmBodyDesc.positionM = { 1.5f, -2.5f, 3.5f };
        cmBodyDesc.massKg = 3.0f;

        const auto cmBody = adapter->createBody(cmBodyDesc);
        ASSERT_TRUE(cmBody.ok()) << cmBody.message;

        // Verify the exact handle exists in OpenEden's shared PhysicsService (one Jolt world!)
        const BodyId sharedId{ cmBody.id.index, cmBody.id.generation };
        EXPECT_TRUE(host.getPhysicsService()->isBodyValid(sharedId));

        BodyState sharedState{};
        ASSERT_TRUE(host.getPhysicsService()->readBody(sharedId, sharedState));
        EXPECT_NEAR(sharedState.positionM.x, 1.5f, 1e-4f);
        EXPECT_NEAR(sharedState.positionM.y, -2.5f, 1e-4f);
        EXPECT_NEAR(sharedState.positionM.z, 3.5f, 1e-4f);

        EXPECT_TRUE(adapter->destroyBody(cmBody.id));
        EXPECT_FALSE(host.getPhysicsService()->isBodyValid(sharedId));

        lifecycle.shutdown();
    }

    TEST(CreativeMotionPluginTest, SpawnCharacterRagdollAndStepInSharedWorld)
    {
        HostImpl host;
        LifecycleManager lifecycle(host);

        auto cmPlugin = std::make_unique<CreativeMotionPlugin>();
        CreativeMotionPlugin* cmRaw = cmPlugin.get();
        lifecycle.addPlugin(loadCreativeMotionManifest(), std::move(cmPlugin));
        ASSERT_TRUE(lifecycle.initialize());

        // Enable CreativeMotion at runtime
        cmRaw->setEnabled(true);
        EXPECT_TRUE(cmRaw->isEnabled());

        const EntityId actor = host.entityManager().createEntity();
        ASSERT_TRUE( host.entityManager().isValid(actor));

        ASSERT_TRUE(cmRaw->spawnCharacter(actor, PhysicsVec3{ 0.0f, 0.0f, 0.0f }, 0.0f));
        EXPECT_TRUE(cmRaw->hasCharacter(actor));
        EXPECT_EQ(cmRaw->getActiveCharacterCount(), 1u);

        const auto initialPelvis = cmRaw->getPelvisPosition(actor);
        ASSERT_TRUE(initialPelvis.has_value());
        EXPECT_GT(initialPelvis->z, 0.5f);

        // Step 30 fixed frames through the host loop and shared Jolt physics service
        const float dt = host.getPhysicsService()->fixedStepSeconds();
        for (std::uint64_t stepIdx = 0; stepIdx < 30; ++stepIdx)
        {
            lifecycle.runFixedStepPhase(Phase::FixedStepPrePhysics, dt, stepIdx);
            host.getPhysicsService()->step(dt);
            lifecycle.runFixedStepPhase(Phase::FixedStepPostPhysics, dt, stepIdx);
            lifecycle.runPhase(Phase::PostSimulation, dt);
            lifecycle.runPhase(Phase::Animation, dt);
            lifecycle.runPhase(Phase::PreRender, dt);
            lifecycle.runPhase(Phase::Ui, dt);
        }

        EXPECT_EQ(cmRaw->getFixedStepCount(), 30u);

        const auto steppedPelvis = cmRaw->getPelvisPosition(actor);
        const auto capsulePos = cmRaw->getReconciledCapsulePosition(actor);
        ASSERT_TRUE(steppedPelvis.has_value());
        ASSERT_TRUE(capsulePos.has_value());
        EXPECT_GT(steppedPelvis->z, 0.3f);
        EXPECT_FALSE(cmRaw->getCharacterStateName(actor).empty());

        // Apply a hit impulse and step a few more frames
        EXPECT_TRUE(cmRaw->applyHitImpulse(
            actor, PhysicsVec3{ 40.0f, 0.0f, 0.0f }, PhysicsVec3{ 0.0f, 0.0f, steppedPelvis->z }));

        for (std::uint64_t stepIdx = 30; stepIdx < 40; ++stepIdx)
        {
            lifecycle.runFixedStepPhase(Phase::FixedStepPrePhysics, dt, stepIdx);
            host.getPhysicsService()->step(dt);
            lifecycle.runFixedStepPhase(Phase::FixedStepPostPhysics, dt, stepIdx);
        }

        // Despawn character cleanly
        EXPECT_TRUE(cmRaw->despawnCharacter(actor));
        EXPECT_FALSE(cmRaw->hasCharacter(actor));
        EXPECT_EQ(cmRaw->getActiveCharacterCount(), 0u);

        lifecycle.shutdown();
    }

    TEST(CreativeMotionPluginTest, RegistersCollisionLayersVfsFormatsSaveChunkAndLuaModule)
    {
        HostImpl host;
        LifecycleManager lifecycle(host);

        auto cmPlugin = std::make_unique<CreativeMotionPlugin>();
        CreativeMotionPlugin* cmRaw = cmPlugin.get();
        lifecycle.addPlugin(loadCreativeMotionManifest(), std::move(cmPlugin));
        ASSERT_TRUE(lifecycle.initialize());

        // 1. Collision layers registered and colliding
        EXPECT_NE(cmRaw->getRagdollCollisionLayer(), 0u);
        EXPECT_NE(cmRaw->getWorldCollisionLayer(), 0u);
        EXPECT_TRUE(host.collisionLayers().hasLayer("creativemotion.ragdoll"));
        EXPECT_TRUE(host.collisionLayers().hasLayer("creativemotion.world"));
        EXPECT_TRUE(host.collisionLayers().canCollide("creativemotion.ragdoll", "creativemotion.world"));
        EXPECT_TRUE(host.collisionLayers().canCollide("creativemotion.ragdoll", "creativemotion.ragdoll"));

        // 2. VFS format readers registered (.rig.json, .profile.json, .style.json, .animgraph.json)
        const auto tempDir = std::filesystem::temp_directory_path() / "openeden_cm_vfs_test";
        std::filesystem::create_directories(tempDir);
        for (const char* filename : { "sample.rig.json", "sample.profile.json", "sample.style.json", "sample.animgraph.json" })
        {
            std::ofstream out(tempDir / filename, std::ios::binary);
            out << "{\"format\":\"opas.test\",\"version\":1}\n";
        }
        host.vfsService().mount("data", tempDir, 100);

        FileError fileErr{};
        EXPECT_TRUE(host.vfsService().readAsset("data/sample.rig.json", &fileErr)) << fileErr.message;
        EXPECT_TRUE(host.vfsService().readAsset("data/sample.profile.json", &fileErr)) << fileErr.message;
        EXPECT_TRUE(host.vfsService().readAsset("data/sample.style.json", &fileErr)) << fileErr.message;
        EXPECT_TRUE(host.vfsService().readAsset("data/sample.animgraph.json", &fileErr)) << fileErr.message;
        std::filesystem::remove_all(tempDir);

        // 3. Save chunk handler registered and round-trips state across serialize / deserialize
        cmRaw->setEnabled(true);
        const EntityId entity = host.entityManager().createEntity();
        ASSERT_TRUE(cmRaw->spawnCharacter(entity, PhysicsVec3{ 2.0f, 3.0f, 0.0f }, 0.5f));

        const std::vector<std::uint8_t> saveBytes = host.saveService().serialize();
        EXPECT_FALSE(saveBytes.empty());

        // Reset character and enabled state, then deserialize to verify restoration
        EXPECT_TRUE(cmRaw->despawnCharacter(entity));
        cmRaw->setEnabled(false);
        EXPECT_FALSE(cmRaw->isEnabled());
        EXPECT_EQ(cmRaw->getActiveCharacterCount(), 0u);

        ASSERT_TRUE(host.saveService().deserialize(saveBytes));
        EXPECT_TRUE(cmRaw->isEnabled());
        EXPECT_EQ(cmRaw->getActiveCharacterCount(), 1u);
        EXPECT_TRUE(cmRaw->hasCharacter(entity));

        // 4. Lua module registered under openeden.creativemotion
        EXPECT_TRUE(host.scriptingService().hasModule("creativemotion"));
        EXPECT_EQ(host.scriptingService().getCanonicalNamespace("creativemotion"), "openeden.creativemotion");
        host.scriptingService().setPluginLoadOrder({ "creativemotion" });
        host.scriptingService().initializeModules();

        const LuaTable* luaTable = host.scriptingService().getModuleTable("creativemotion");
        ASSERT_NE(luaTable, nullptr);
        EXPECT_EQ(luaTable->getString("pluginId"), "creativemotion");
        EXPECT_EQ(luaTable->getString("contract"), "openeden.animation");

        lifecycle.shutdown();
    }

    std::filesystem::path findCreativeMotionSharedLibraryDir()
    {
#ifdef _WIN32
        constexpr const char* kLibName = "creativemotion.dll";
#else
        constexpr const char* kLibName = "libcreativemotion.so";
#endif

        std::vector<std::filesystem::path> roots;
        std::filesystem::path cur = std::filesystem::current_path();
        for (int depth = 0; depth < 6; ++depth)
        {
            roots.push_back(cur);
            roots.push_back(cur / "build-jolt");
            roots.push_back(cur / "build-vs");
            roots.push_back(cur / "build");
            if (!cur.has_parent_path() || cur.parent_path() == cur)
                break;
            cur = cur.parent_path();
        }

        for (const auto& root : roots)
        {
            for (const auto& rel : {
                     std::filesystem::path("plugins/creativemotion"),
                     std::filesystem::path("plugins/creativemotion/Release"),
                     std::filesystem::path("plugins/creativemotion/Debug"),
                     std::filesystem::path("plugins/creativemotion/RelWithDebInfo"),
                 })
            {
                const auto candidateDir = root / rel;
                if (std::filesystem::exists(candidateDir / kLibName))
                    return candidateDir;
            }
        }
        return {};
    }

    TEST(CreativeMotionPluginTest, LoadsSharedPluginLibraryViaPluginLoader)
    {
        const auto libDir = findCreativeMotionSharedLibraryDir();
        ASSERT_FALSE(libDir.empty()) << "Built creativemotion shared library not found in build output";

        HostImpl host;
        DiscoveredPlugin discovered{};
        discovered.directory = libDir;
        discovered.manifestPath = findCreativeMotionManifestPath();
        discovered.manifest = loadCreativeMotionManifest();
        discovered.manifest.toolchain = getHostToolchain();

        PluginLoadResult loadRes = loadPluginLibrary(discovered, host.getHostInfo());
        ASSERT_TRUE(loadRes.success) << "Stage: " << loadRes.error.stage << " - " << loadRes.error.message;
        ASSERT_NE(loadRes.plugin.instance, nullptr);
        ASSERT_NE(loadRes.plugin.library, nullptr);
        EXPECT_TRUE(loadRes.plugin.library->isLoaded());

        LifecycleManager lifecycle(host);
        lifecycle.addPlugin(
            std::move(loadRes.plugin.manifest), std::move(loadRes.plugin.instance), std::move(loadRes.plugin.library));

        PluginManifest consumerManifest{};
        consumerManifest.id = "dynamic_animation_consumer";
        consumerManifest.name = "Dynamic Animation Consumer";
        consumerManifest.pluginVersion = "1.0.0";
        consumerManifest.requiredContracts.push_back({ "openeden.animation", Version{ 1, 1 } });

        auto consumerPlugin = std::make_unique<AnimationConsumerPlugin>();
        AnimationConsumerPlugin* consumerRaw = consumerPlugin.get();
        lifecycle.addPlugin(consumerManifest, std::move(consumerPlugin));

        ASSERT_TRUE(lifecycle.initialize());
        EXPECT_TRUE(lifecycle.isPluginActive("creativemotion"));
        EXPECT_TRUE(lifecycle.isPluginActive("dynamic_animation_consumer"));

        AnimationContract* anim = consumerRaw->animation();
        ASSERT_NE(anim, nullptr);
        EXPECT_FALSE(anim->isEnabled());

        anim->setEnabled(true);
        EXPECT_TRUE(anim->isEnabled());

        const EntityId actor = host.entityManager().createEntity();
        ASSERT_TRUE(anim->spawnCharacter(actor, PhysicsVec3{ 0.0f, 0.0f, 0.0f }, 0.0f));
        EXPECT_TRUE(anim->hasCharacter(actor));
        EXPECT_EQ(anim->getActiveCharacterCount(), 1u);

        const float dt = host.getPhysicsService()->fixedStepSeconds();
        lifecycle.runFixedStepPhase(Phase::FixedStepPrePhysics, dt, 0);
        host.getPhysicsService()->step(dt);
        lifecycle.runFixedStepPhase(Phase::FixedStepPostPhysics, dt, 0);

        EXPECT_TRUE(anim->getPelvisPosition(actor).has_value());
        EXPECT_TRUE(anim->despawnCharacter(actor));
        EXPECT_EQ(anim->getActiveCharacterCount(), 0u);

        lifecycle.shutdown();
        EXPECT_FALSE(lifecycle.isPluginActive("creativemotion"));
    }
} // namespace
