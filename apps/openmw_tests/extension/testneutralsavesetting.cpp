// Task 0177 (neutral world saves, stage N6): with [Game] neutral saves (Config::neutralSaves) a session whose game pack
// registered a neutral world source is no longer refused saving; every other refusal stays. A new file: the pinned
// refusal of testneutralworldsaving.cpp is the default and is not touched.
#include "apps/openmw/extensionhost.hpp"

#include <components/extension/manifest.hpp>

#include <openeden/extension.hpp>
#include <openeden/game/world_source.hpp>
#include <openeden/game/world_source_service.hpp>
#include <openeden/host.hpp>
#include <openeden/registrar.hpp>
#include <openeden/service_locator.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace
{
    namespace sdkgame = openeden::sdk::game;

    class EmptySource final : public sdkgame::WorldCellSource
    {
    public:
        std::string_view gameId() const override { return "savesetting"; }
        std::size_t cellCount() const override { return 0; }
        std::size_t listCells(std::size_t, std::size_t, std::vector<sdkgame::WorldId>&) const override { return 0; }
        bool buildCell(sdkgame::WorldId, sdkgame::WorldCell&, std::string& error) override
        {
            error = "none";
            return false;
        }
    };

    class Pack final : public openeden::sdk::Extension
    {
    public:
        void registerServices(openeden::sdk::Registrar&) override {}
        bool resolve(const openeden::sdk::ServiceLocator&) override { return true; }
        void start(openeden::sdk::Host& host) override
        {
            auto* service = host.getWorldSourceService();
            std::string error;
            if (service != nullptr && service->consumerEnabled() && service->registerSource(mSource, error))
                mService = service;
        }
        void stop() override { withdraw(); }
        ~Pack() override { withdraw(); }

    private:
        void withdraw()
        {
            if (mService != nullptr)
                mService->withdrawSource(mSource);
            mService = nullptr;
        }
        EmptySource mSource;
        sdkgame::WorldSourceService* mService = nullptr;
    };

    OMW::BuiltInGamePack pack()
    {
        openeden::extension::ExtensionManifest m;
        m.version = 2;
        m.id = "savesetting";
        m.name = "savesetting";
        m.extensionVersion = "1.0.0";
        m.kind = "game";
        m.sdkVersion = openeden::sdk::kSdkVersion;
        return { m, [] { return std::make_unique<Pack>(); } };
    }

    OMW::ExtensionHost::Config config(bool neutralSaves)
    {
        OMW::ExtensionHost::Config c;
        c.gamePackId = "savesetting";
        c.staticPacks = { pack() };
        c.neutralWorld = true;
        c.neutralSaves = neutralSaves;
        return c;
    }

    TEST(NeutralSaveSetting, withTheSettingOnASessionWithANeutralSourceMayBeSaved)
    {
        OMW::ExtensionHost host(config(true));
        ASSERT_TRUE(host.startGamePack());
        host.start();
        EXPECT_TRUE(host.sessionError().empty());
        EXPECT_TRUE(host.savingRefusal().empty()) << host.savingRefusal();
    }

    TEST(NeutralSaveSetting, withTheSettingOffTheSameSessionIsStillRefused)
    {
        OMW::ExtensionHost host(config(false));
        ASSERT_TRUE(host.startGamePack());
        host.start();
        EXPECT_NE(host.savingRefusal().find("no save support"), std::string::npos) << host.savingRefusal();
    }
}
