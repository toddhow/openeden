// Task 0103 (neutral world model stage N3a-1): the world source service, the slot through which a game pack hands its
// WorldCellSource to the host (docs/design/neutral-world-model.md 10.5). Written from the task spec before the code.
#include <components/extension/host_impl.hpp>
#include <components/extension/lifecycle_manager.hpp>
#include <components/extension/manifest.hpp>
#include <openeden/extension.hpp>
#include <openeden/game/world_source.hpp>
#include <openeden/game/world_source_service.hpp>
#include <openeden/host.hpp>
#include <openeden/pack_kind.hpp>
#include <openeden/registrar.hpp>
#include <openeden/service_locator.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using openeden::extension::ExtensionManifest;
    using openeden::extension::HostImpl;
    using openeden::extension::LifecycleManager;
    using openeden::extension::WorldSourceServiceImpl;
    using openeden::sdk::Extension;
    using openeden::sdk::Host;
    using openeden::sdk::PackKind;
    using openeden::sdk::Registrar;
    using openeden::sdk::ServiceLocator;

    class NamedSource final : public WorldCellSource
    {
    public:
        explicit NamedSource(std::string game)
            : mGame(std::move(game))
        {
        }
        std::string_view gameId() const override { return mGame; }
        std::size_t cellCount() const override { return 0; }
        std::size_t listCells(std::size_t, std::size_t, std::vector<WorldId>&) const override { return 0; }
        bool buildCell(WorldId, WorldCell&, std::string& error) override
        {
            error = "none";
            return false;
        }

    private:
        std::string mGame;
    };

    struct RecordingListener final : WorldSourceServiceImpl::Listener
    {
        bool sourceRegistered(WorldCellSource& source, std::string& error) override
        {
            if (mRefuse)
            {
                error = "refused by the listener";
                return false;
            }
            mEvents.push_back("add " + std::string(source.gameId()));
            return true;
        }
        void sourceWithdrawn(std::string_view gameId) override { mEvents.push_back("remove " + std::string(gameId)); }

        bool mRefuse = false;
        std::vector<std::string> mEvents;
    };

    // ---- The Host accessor ----

    class PlainHost final : public Host
    {
    public:
        const openeden::sdk::HostInfo& getHostInfo() const override { return mInfo; }
        openeden::sdk::HostInfo mInfo;
    };

    TEST(WorldSourceService, aHostThatDoesNotOverrideTheAccessorHasNoService)
    {
        PlainHost host;
        EXPECT_EQ(host.getWorldSourceService(), nullptr);
    }

    // The accessor is the LAST virtual function of Host (additive, like getBindingsService and getWorldViewService):
    // a module built against the earlier Host calls the earlier slots unchanged. The device that pins ObScriptContract:
    // a hand-written mirror of the interface called through a reinterpret_cast.
    struct MirrorHost
    {
        virtual ~MirrorHost() = default;
        virtual const openeden::sdk::HostInfo& getHostInfo() const = 0;
        virtual void* getEventBus() { return nullptr; }
        virtual void* getEntityManager() { return nullptr; }
        virtual void* getSettingsService() { return nullptr; }
        virtual void* getVfsService() { return nullptr; }
        virtual void* getPhysicsQueryService() { return nullptr; }
        virtual void* getLoggingService() { return nullptr; }
        virtual void* getProfilerService() { return nullptr; }
        virtual void* getScratchMemoryService() { return nullptr; }
        virtual void* getDebugDrawService() { return nullptr; }
        virtual void* getSaveChunkService() { return nullptr; }
        virtual void* getRecordService() { return nullptr; }
        virtual void* getContentService() { return nullptr; }
        virtual void* getScriptHostService() { return nullptr; }
        virtual void* getUiService() { return nullptr; }
        virtual void* getBindingsService() { return nullptr; }
        virtual void* getWorldViewService() { return nullptr; }
        virtual void* getWorldSourceService() { return nullptr; } // the new slot, after every earlier one
    };

    class MarkedHost final : public Host
    {
    public:
        const openeden::sdk::HostInfo& getHostInfo() const override { return mInfo; }
        openeden::sdk::game::WorldViewService* getWorldViewService() override
        {
            return reinterpret_cast<openeden::sdk::game::WorldViewService*>(&mViewMarker);
        }
        WorldSourceService* getWorldSourceService() override { return reinterpret_cast<WorldSourceService*>(&mMarker); }
        openeden::sdk::HostInfo mInfo;
        int mMarker = 0;
        int mViewMarker = 0;
    };

    TEST(WorldSourceService, theAccessorIsTheLastSlotOfHost)
    {
        MarkedHost host;
        Host* base = &host;
        auto* mirror = reinterpret_cast<MirrorHost*>(base);
        EXPECT_EQ(mirror->getWorldSourceService(), &host.mMarker);
        EXPECT_EQ(mirror->getWorldViewService(), &host.mViewMarker);
    }

    TEST(WorldSourceService, theVersionIsOneZero)
    {
        EXPECT_EQ(kWorldSourceServiceVersion.major, 1);
        EXPECT_EQ(kWorldSourceServiceVersion.minor, 0);
    }

    // ---- By kind ----

    ExtensionManifest makeManifest(const std::string& id, PackKind kind)
    {
        ExtensionManifest m;
        m.version = kind == PackKind::Game ? 2 : 1;
        m.id = id;
        m.name = id;
        m.extensionVersion = "1.0.0";
        m.kind = std::string(openeden::sdk::toString(kind));
        m.sdkVersion = openeden::sdk::kSdkVersion;
        m.toolchain = "test-toolchain";
        return m;
    }

    class AskingExtension final : public Extension
    {
    public:
        explicit AskingExtension(WorldSourceService*& seen)
            : mSeen(seen)
        {
        }
        void registerServices(Registrar&) override {}
        bool resolve(const ServiceLocator&) override { return true; }
        void start(Host& host) override { mSeen = host.getWorldSourceService(); }
        void stop() override {}

    private:
        WorldSourceService*& mSeen;
    };

    TEST(WorldSourceService, aGamePackIsHandedTheServiceAndOtherKindsAreRefusedWithARecordedDenial)
    {
        {
            HostImpl host;
            LifecycleManager manager(host);
            WorldSourceService* seen = nullptr;
            manager.addExtension(makeManifest("gamepack", PackKind::Game), std::make_unique<AskingExtension>(seen));
            manager.initialize();
            EXPECT_EQ(seen, &host.worldSources());
            EXPECT_TRUE(manager.getCapabilityDenials().empty());
            manager.shutdown();
        }
        for (const PackKind kind : { PackKind::Feature, PackKind::Compatibility })
        {
            HostImpl host;
            LifecycleManager manager(host);
            WorldSourceService* seen = reinterpret_cast<WorldSourceService*>(1);
            manager.addExtension(makeManifest("other", kind), std::make_unique<AskingExtension>(seen));
            manager.initialize();
            EXPECT_EQ(seen, nullptr);
            ASSERT_EQ(manager.getCapabilityDenials().size(), 1u);
            EXPECT_EQ(manager.getCapabilityDenials()[0].what, "world source service");
            EXPECT_NE(manager.getCapabilityDenials()[0].message.find("other"), std::string::npos);
            manager.shutdown();
        }
    }

    // ---- The service itself ----

    TEST(WorldSourceService, theHostDoesNotConsumeSourcesUntilItIsToldTo)
    {
        HostImpl host;
        EXPECT_FALSE(host.worldSources().consumerEnabled());
        host.worldSources().setConsumerEnabled(true);
        EXPECT_TRUE(host.worldSources().consumerEnabled());
        host.worldSources().setConsumerEnabled(false);
        EXPECT_FALSE(host.worldSources().consumerEnabled());
    }

    TEST(WorldSourceService, withoutAListenerASourceIsKeptAndWithdrawn)
    {
        WorldSourceServiceImpl service;
        NamedSource source("one");
        std::string error;
        EXPECT_TRUE(service.registerSource(source, error)) << error;
        EXPECT_EQ(service.sourceCount(), 1u);
        service.withdrawSource(source);
        EXPECT_EQ(service.sourceCount(), 0u);
    }

    TEST(WorldSourceService, theListenerHearsAboutEverySourceInOrder)
    {
        WorldSourceServiceImpl service;
        RecordingListener listener;
        service.setListener(&listener);
        NamedSource a("a");
        NamedSource b("b");
        std::string error;
        ASSERT_TRUE(service.registerSource(a, error));
        ASSERT_TRUE(service.registerSource(b, error));
        service.withdrawSource(a);
        EXPECT_EQ(listener.mEvents, (std::vector<std::string>{ "add a", "add b", "remove a" }));
    }

    TEST(WorldSourceService, aSecondSourceOfTheSameGameIsRefusedWithoutTellingTheListener)
    {
        WorldSourceServiceImpl service;
        RecordingListener listener;
        service.setListener(&listener);
        NamedSource first("same");
        NamedSource second("same");
        std::string error;
        ASSERT_TRUE(service.registerSource(first, error));
        EXPECT_FALSE(service.registerSource(second, error));
        EXPECT_NE(error.find("same"), std::string::npos) << error;
        EXPECT_EQ(listener.mEvents, (std::vector<std::string>{ "add same" }));
        EXPECT_EQ(service.sourceCount(), 1u);
        // the refused one is not "registered": withdrawing it changes nothing
        service.withdrawSource(second);
        EXPECT_EQ(service.sourceCount(), 1u);
        EXPECT_EQ(listener.mEvents.size(), 1u);
    }

    TEST(WorldSourceService, aListenerThatRefusesLeavesNothingRegisteredAndItsErrorIsReturned)
    {
        WorldSourceServiceImpl service;
        RecordingListener listener;
        listener.mRefuse = true;
        service.setListener(&listener);
        NamedSource source("x");
        std::string error;
        EXPECT_FALSE(service.registerSource(source, error));
        EXPECT_EQ(error, "refused by the listener");
        EXPECT_EQ(service.sourceCount(), 0u);
        listener.mRefuse = false;
        EXPECT_TRUE(service.registerSource(source, error)) << "a refusal is not remembered";
    }

    TEST(WorldSourceService, withdrawingASourceThatWasNeverRegisteredDoesNothing)
    {
        WorldSourceServiceImpl service;
        RecordingListener listener;
        service.setListener(&listener);
        NamedSource source("never");
        service.withdrawSource(source);
        EXPECT_TRUE(listener.mEvents.empty());
    }

    TEST(WorldSourceService, theSameObjectRegisteredTwiceIsRefused)
    {
        WorldSourceServiceImpl service;
        NamedSource source("twice");
        std::string error;
        ASSERT_TRUE(service.registerSource(source, error));
        EXPECT_FALSE(service.registerSource(source, error));
        EXPECT_EQ(service.sourceCount(), 1u);
    }

    // A pack that is destroyed without withdrawing (a bug in the pack) must not leave the listener holding a source:
    // the host drops what is left before it unloads the pack, and tells the listener by the game id it copied at
    // registration (the source object is gone by then).
    TEST(WorldSourceService, releasingExtensionObjectsWithdrawsWhatIsLeftByTheCopiedGameId)
    {
        HostImpl host;
        RecordingListener listener;
        host.worldSources().setListener(&listener);
        {
            NamedSource source("leaky");
            std::string error;
            ASSERT_TRUE(host.worldSources().registerSource(source, error));
        } // destroyed with no withdraw
        host.releaseExtensionObjects();
        EXPECT_EQ(listener.mEvents, (std::vector<std::string>{ "add leaky", "remove leaky" }));
        EXPECT_EQ(host.worldSources().sourceCount(), 0u);
    }

    TEST(WorldSourceService, theServiceItselfIsMainThreadOnlyAndCostsNothingUntilUsed)
    {
        // A host that never sees a game pack has an empty service: nothing registered, no listener, not enabled.
        HostImpl host;
        EXPECT_EQ(host.worldSources().sourceCount(), 0u);
        EXPECT_FALSE(host.worldSources().consumerEnabled());
    }
}
