// Task 0103 (neutral world model stage N3a-1): the game's extension host builds the neutral world store only when
// [Game] neutral world asks for it (Config::neutralWorld), registers the game pack's source in it and drops the source
// when the session ends. Written from the spec first.
#include "apps/openmw/extensionhost.hpp"

#include <components/extension/manifest.hpp>
#include <components/worldmodel/store.hpp>

#include <openeden/extension.hpp>
#include <openeden/game/world_source.hpp>
#include <openeden/game/world_source_service.hpp>
#include <openeden/host.hpp>
#include <openeden/registrar.hpp>
#include <openeden/service_locator.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    namespace sdkgame = openeden::sdk::game;
    using openeden::sdk::Extension;
    using openeden::sdk::Host;
    using openeden::sdk::Registrar;
    using openeden::sdk::ServiceLocator;

    class OneCellSource final : public sdkgame::WorldCellSource
    {
    public:
        std::string_view gameId() const override { return "fakegame"; }
        std::size_t cellCount() const override { return 1; }
        std::size_t listCells(std::size_t first, std::size_t, std::vector<sdkgame::WorldId>& out) const override
        {
            if (first > 0)
                return 0;
            out.push_back(7);
            return 1;
        }
        bool buildCell(sdkgame::WorldId id, sdkgame::WorldCell& out, std::string& error) override
        {
            if (id != 7)
            {
                error = "no such cell";
                return false;
            }
            out = {};
            out.id = 7;
            out.editorId = "Seven";
            return true;
        }
        double unitsPerMetre() const override { return 70.0; }
    };

    // Behaves as the Skyrim pack does: registers its source only when the host says it consumes them.
    class SourcePack final : public Extension
    {
    public:
        explicit SourcePack(bool& sawService, bool& sawEnabled)
            : mSawService(sawService)
            , mSawEnabled(sawEnabled)
        {
        }
        void registerServices(Registrar&) override {}
        bool resolve(const ServiceLocator&) override { return true; }
        void start(Host& host) override
        {
            mService = host.getWorldSourceService();
            mSawService = mService != nullptr;
            if (mService == nullptr)
                throw std::runtime_error("no world source service");
            mSawEnabled = mService->consumerEnabled();
            if (!mSawEnabled)
                return;
            std::string error;
            if (!mService->registerSource(mSource, error))
                throw std::runtime_error(error);
        }
        void stop() override { withdraw(); }
        ~SourcePack() override { withdraw(); }

    private:
        void withdraw()
        {
            if (mService != nullptr)
                mService->withdrawSource(mSource);
            mService = nullptr;
        }
        bool& mSawService;
        bool& mSawEnabled;
        OneCellSource mSource;
        sdkgame::WorldSourceService* mService = nullptr;
    };

    struct Flags
    {
        bool mSawService = false;
        bool mSawEnabled = false;
    };

    OMW::BuiltInGamePack sourcePack(Flags& flags)
    {
        openeden::extension::ExtensionManifest m;
        m.version = 2;
        m.id = "fakegame";
        m.name = "fakegame";
        m.extensionVersion = "1.0.0";
        m.kind = "game";
        m.sdkVersion = openeden::sdk::kSdkVersion;
        m.toolchain = "";
        return { m, [&flags] { return std::make_unique<SourcePack>(flags.mSawService, flags.mSawEnabled); } };
    }

    TEST(NeutralWorldHost, withTheSettingOffThereIsNoStoreAndThePackIsToldSo)
    {
        Flags flags;
        OMW::ExtensionHost host({ {}, {}, "fakegame", { sourcePack(flags) }, false });
        EXPECT_EQ(host.neutralWorldStore(), nullptr);
        ASSERT_TRUE(host.startGamePack());
        EXPECT_TRUE(flags.mSawService);
        EXPECT_FALSE(flags.mSawEnabled);
        EXPECT_TRUE(host.sessionError().empty());
        EXPECT_EQ(host.host().worldSources().sourceCount(), 0u);
        host.start();
        host.stop();
    }

    TEST(NeutralWorldHost, theDefaultConfigIsTheSettingOff)
    {
        // Config built the way the engine built it before this stage (no sixth member): nothing changes.
        Flags flags;
        OMW::ExtensionHost host({ {}, {}, "fakegame", { sourcePack(flags) } });
        EXPECT_EQ(host.neutralWorldStore(), nullptr);
        ASSERT_TRUE(host.startGamePack());
        EXPECT_FALSE(flags.mSawEnabled);
    }

    TEST(NeutralWorldHost, withTheSettingOnThePacksSourceIsInTheStoreAndBuildsCellsOnRequest)
    {
        Flags flags;
        OMW::ExtensionHost host({ {}, {}, "fakegame", { sourcePack(flags) }, true });
        ASSERT_NE(host.neutralWorldStore(), nullptr);
        EXPECT_EQ(host.neutralWorldStore()->sourceCount(), 0u) << "the pack has not started yet";
        ASSERT_TRUE(host.startGamePack());
        EXPECT_TRUE(flags.mSawEnabled);
        EXPECT_TRUE(host.sessionError().empty()) << host.sessionError();
        EXPECT_EQ(host.neutralWorldStore()->sourceCount(), 1u);

        std::string error;
        const sdkgame::WorldCell* cell = host.neutralWorldStore()->cell("fakegame", 7, error);
        ASSERT_NE(cell, nullptr) << error;
        EXPECT_EQ(cell->editorId, "Seven");
        EXPECT_DOUBLE_EQ(host.neutralWorldStore()->unitsPerMetre("fakegame"), 70.0);
        host.start();
        EXPECT_EQ(host.neutralWorldStore()->sourceCount(), 1u);
    }

    TEST(NeutralWorldHost, endingTheSessionDropsTheSourceAndItsCells)
    {
        Flags flags;
        OMW::ExtensionHost host({ {}, {}, "fakegame", { sourcePack(flags) }, true });
        ASSERT_TRUE(host.startGamePack());
        host.start();
        std::string error;
        ASSERT_NE(host.neutralWorldStore()->cell("fakegame", 7, error), nullptr);
        host.stop();
        EXPECT_EQ(host.neutralWorldStore()->sourceCount(), 0u);
        EXPECT_FALSE(host.neutralWorldStore()->isResident("fakegame", 7));
        EXPECT_EQ(host.host().worldSources().sourceCount(), 0u);
    }

    TEST(NeutralWorldHost, withNoGamePackTheStoreIsEmptyAndNothingHappens)
    {
        Flags flags;
        OMW::ExtensionHost host({ {}, {}, "", { sourcePack(flags) }, true });
        EXPECT_FALSE(host.startGamePack());
        host.start();
        ASSERT_NE(host.neutralWorldStore(), nullptr);
        EXPECT_EQ(host.neutralWorldStore()->sourceCount(), 0u);
        host.stop();
    }

    TEST(NeutralWorldHost, destroyingTheHostWithoutStopIsSafe)
    {
        Flags flags;
        {
            OMW::ExtensionHost host({ {}, {}, "fakegame", { sourcePack(flags) }, true });
            ASSERT_TRUE(host.startGamePack());
            ASSERT_EQ(host.neutralWorldStore()->sourceCount(), 1u);
        }
        SUCCEED();
    }
}
