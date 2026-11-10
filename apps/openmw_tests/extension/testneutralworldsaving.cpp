// Task 0103 follow-up (toddhow's decision on caveat 1): a game pack that has registered a neutral world source has no
// save support until stage N6, so the engine keeps refusing saves for such a session. Written before the code.
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
        std::string_view gameId() const override { return "savegame"; }
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
        explicit Pack(bool registerSource)
            : mRegister(registerSource)
        {
        }
        void registerServices(openeden::sdk::Registrar&) override {}
        bool resolve(const openeden::sdk::ServiceLocator&) override { return true; }
        void start(openeden::sdk::Host& host) override
        {
            auto* service = host.getWorldSourceService();
            std::string error;
            if (mRegister && service != nullptr && service->consumerEnabled()
                && service->registerSource(mSource, error))
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
        bool mRegister;
        EmptySource mSource;
        sdkgame::WorldSourceService* mService = nullptr;
    };

    OMW::BuiltInGamePack pack(bool registerSource)
    {
        openeden::extension::ExtensionManifest m;
        m.version = 2;
        m.id = "savegame";
        m.name = "savegame";
        m.extensionVersion = "1.0.0";
        m.kind = "game";
        m.sdkVersion = openeden::sdk::kSdkVersion;
        return { m, [registerSource] { return std::make_unique<Pack>(registerSource); } };
    }

    TEST(NeutralWorldSaving, aPackThatRegisteredASourceKeepsSavingRefusedWithAReason)
    {
        OMW::ExtensionHost host({ {}, {}, "savegame", { pack(true) }, true });
        ASSERT_TRUE(host.startGamePack());
        EXPECT_TRUE(host.sessionError().empty());
        EXPECT_NE(host.savingRefusal().find("no save support"), std::string::npos) << host.savingRefusal();
        host.start();
        EXPECT_FALSE(host.savingRefusal().empty());
    }

    TEST(NeutralWorldSaving, withTheSettingOffNothingIsRefused)
    {
        OMW::ExtensionHost host({ {}, {}, "savegame", { pack(true) }, false });
        ASSERT_TRUE(host.startGamePack());
        EXPECT_TRUE(host.savingRefusal().empty());
    }

    TEST(NeutralWorldSaving, aPackThatRegistersNoSourceIsNotRefused)
    {
        OMW::ExtensionHost host({ {}, {}, "savegame", { pack(false) }, true });
        ASSERT_TRUE(host.startGamePack());
        EXPECT_TRUE(host.savingRefusal().empty());
    }

    TEST(NeutralWorldSaving, noGamePackAtAllIsNotRefused)
    {
        OMW::ExtensionHost host({ {}, {}, "", { pack(true) }, true });
        host.start();
        EXPECT_TRUE(host.savingRefusal().empty());
    }

    TEST(NeutralWorldSaving, aSessionErrorStillWinsAndAfterStopNothingIsRefused)
    {
        OMW::ExtensionHost host({ {}, {}, "savegame", { pack(true) }, true });
        ASSERT_TRUE(host.startGamePack());
        host.start();
        host.stop();
        EXPECT_TRUE(host.savingRefusal().empty()) << "the source is gone with the session";
    }
}
