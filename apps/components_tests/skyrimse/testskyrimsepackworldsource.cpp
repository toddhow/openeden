// Task 0103 (neutral world model stage N3a-1): the Skyrim SE game pack hands its WorldCellSource to the host when the
// host consumes sources, and keeps the old behaviour (a refused session) when it does not. Written from the spec first.
#include <components/extension/host_impl.hpp>
#include <components/extension/lifecycle_manager.hpp>
#include <components/extension/manifest.hpp>
#include <components/worldmodel/store.hpp>
#include <openeden/game/world_source_service.hpp>
#include <openeden/phases.hpp>

#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/skyrimse_game_pack.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using openeden::extension::ExtensionManifest;
    using openeden::extension::HostImpl;
    using openeden::extension::LifecycleManager;
    using openeden::extension::WorldSourceServiceImpl;

    ExtensionManifest packManifest()
    {
        ExtensionManifest m;
        m.version = 2;
        m.id = "skyrimse";
        m.name = "Skyrim SE";
        m.extensionVersion = "1.0.0";
        m.kind = "game";
        m.sdkVersion = openeden::sdk::kSdkVersion;
        m.toolchain = "test-toolchain";
        return m;
    }

    // The engine's listener in miniature: sources go into a NeutralWorldStore.
    struct StoreListener final : WorldSourceServiceImpl::Listener
    {
        bool sourceRegistered(openeden::sdk::game::WorldCellSource& source, std::string& error) override
        {
            return mStore.addSource(source, error);
        }
        void sourceWithdrawn(std::string_view gameId) override { mStore.removeSource(gameId); }
        NeutralWorld::NeutralWorldStore mStore;
    };

    TEST(SkyrimSePackWorldSource, withTheHostNotConsumingSourcesTheOldBehaviourStays)
    {
        HostImpl host;
        LifecycleManager manager(host);
        manager.addExtension(packManifest(), createSkyrimGamePack());
        manager.initialize();
        EXPECT_TRUE(manager.hasSessionError());
        EXPECT_NE(manager.getSessionError().find(std::string(kRulesNotImplementedMessage)), std::string::npos);
        EXPECT_FALSE(manager.isSavingAllowed());
        EXPECT_EQ(host.worldSources().sourceCount(), 0u) << "nothing is registered with a host that does not consume";
        manager.shutdown();
    }

    TEST(SkyrimSePackWorldSource, withTheHostConsumingSourcesTheSessionIsNotRefusedAndTheSourceIsRegistered)
    {
        HostImpl host;
        host.worldSources().setConsumerEnabled(true);
        StoreListener listener;
        host.worldSources().setListener(&listener);
        LifecycleManager manager(host);
        manager.addExtension(packManifest(), createSkyrimGamePack());
        EXPECT_TRUE(manager.initialize());
        EXPECT_FALSE(manager.hasSessionError()) << manager.getSessionError();
        EXPECT_EQ(manager.getGamePackId(), "skyrimse");
        EXPECT_EQ(host.worldSources().sourceCount(), 1u);
        EXPECT_EQ(listener.mStore.sourceCount(), 1u);
        // the content probe is still registered, as before
        EXPECT_NE(host.content().findLoader({ "TES4", 44 }), nullptr);
        EXPECT_NO_THROW(manager.runPhase(openeden::sdk::Phase::GameSimulation, 0.016f));
        manager.shutdown();
    }

    TEST(SkyrimSePackWorldSource, theRegisteredSourceIsSkyrimSEsAndEmptyUntilAHostLoadsPlugins)
    {
        HostImpl host;
        host.worldSources().setConsumerEnabled(true);
        StoreListener listener;
        host.worldSources().setListener(&listener);
        LifecycleManager manager(host);
        manager.addExtension(packManifest(), createSkyrimGamePack());
        manager.initialize();
        // The store knows the game by its id and answers the lookups the engine will use; with no plugin loaded the
        // source has no cells and no cell is found.
        EXPECT_GT(listener.mStore.unitsPerMetre("skyrimse"), 0.0);
        EXPECT_EQ(
            listener.mStore.findCellByEditorId("skyrimse", "WhiterunBanneredMare"), openeden::sdk::game::kNoWorldId);
        std::string error;
        EXPECT_EQ(listener.mStore.cell("skyrimse", 12345, error), nullptr);
        EXPECT_FALSE(error.empty());
        manager.shutdown();
    }

    TEST(SkyrimSePackWorldSource, stoppingThePackWithdrawsTheSourceFromTheStore)
    {
        HostImpl host;
        host.worldSources().setConsumerEnabled(true);
        StoreListener listener;
        host.worldSources().setListener(&listener);
        {
            LifecycleManager manager(host);
            manager.addExtension(packManifest(), createSkyrimGamePack());
            manager.initialize();
            ASSERT_EQ(listener.mStore.sourceCount(), 1u);
            manager.shutdown();
        }
        EXPECT_EQ(host.worldSources().sourceCount(), 0u);
        EXPECT_EQ(listener.mStore.sourceCount(), 0u);
    }

    TEST(SkyrimSePackWorldSource, destroyingThePackWithoutAnExplicitStopWithdrawsToo)
    {
        HostImpl host;
        host.worldSources().setConsumerEnabled(true);
        StoreListener listener;
        host.worldSources().setListener(&listener);
        {
            LifecycleManager manager(host);
            manager.addExtension(packManifest(), createSkyrimGamePack());
            manager.initialize();
            ASSERT_EQ(listener.mStore.sourceCount(), 1u);
        } // the manager and so the pack go; the pack's destructor withdraws
        EXPECT_EQ(listener.mStore.sourceCount(), 0u);
    }

    TEST(SkyrimSePackWorldSource, aStoreThatRefusesTheSourceIsASessionErrorNamingTheReason)
    {
        HostImpl host;
        host.worldSources().setConsumerEnabled(true);
        struct Refusing final : WorldSourceServiceImpl::Listener
        {
            bool sourceRegistered(openeden::sdk::game::WorldCellSource&, std::string& error) override
            {
                error = "the store is full";
                return false;
            }
            void sourceWithdrawn(std::string_view) override {}
        } listener;
        host.worldSources().setListener(&listener);
        LifecycleManager manager(host);
        manager.addExtension(packManifest(), createSkyrimGamePack());
        manager.initialize();
        EXPECT_TRUE(manager.hasSessionError());
        EXPECT_NE(manager.getSessionError().find("the store is full"), std::string::npos) << manager.getSessionError();
        EXPECT_EQ(host.worldSources().sourceCount(), 0u);
    }
}
