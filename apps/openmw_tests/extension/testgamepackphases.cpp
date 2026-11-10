// Task 0084 leftovers, lane I part 2, stage I1b: the game pack chosen by game= starts in a phase of its own, before the
// game's managers and windows are built, and the core takes its GameRules from it. The lifecycle split is tested with
// in-process extensions; the host's side (built-in packs, rules installed and removed) with fakes and the Morrowind
// pack itself. The lifecycle lives in components/, but this test also runs the host, so it sits here.
#include <apps/openmw/extensionhost.hpp>
#include <apps/openmw/mwworld/craftingrules.hpp>

#include <components/debug/debugging.hpp>
#include <components/extension/host_impl.hpp>
#include <components/extension/lifecycle_manager.hpp>

#include <game_morrowind/game_rules.hpp>
#include <game_morrowind/pack.hpp>

#include <openeden/extension.hpp>
#include <openeden/game/game_rules.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace openeden::extension;
    using namespace openeden::sdk;
    using testing::ElementsAre;
    using testing::HasSubstr;

    struct PhaseContract
    {
        static constexpr std::string_view kId = "test.phase.contract";
        static constexpr Version kVersion{ 1, 0 };
        int mValue = 7;
    };

    ExtensionManifest manifestOf(std::string id, std::string kind = "feature",
        std::vector<ContractRequirement> provides = {}, std::vector<ContractRequirement> needs = {})
    {
        ExtensionManifest manifest;
        manifest.version = 2;
        manifest.id = id;
        manifest.name = id;
        manifest.extensionVersion = "1.0.0";
        manifest.kind = std::move(kind);
        manifest.sdkVersion = kSdkVersion;
        manifest.providedContracts = std::move(provides);
        manifest.requiredContracts = std::move(needs);
        return manifest;
    }

    // Records every lifecycle call; can provide PhaseContract and can throw from one stage.
    class Recording final : public Extension
    {
    public:
        Recording(std::string id, std::vector<std::string>& log, bool provides = false, std::string throwAt = {})
            : mId(std::move(id))
            , mLog(log)
            , mProvides(provides)
            , mThrowAt(std::move(throwAt))
        {
        }

        void registerServices(Registrar& registrar) override
        {
            mLog.push_back(mId + " register");
            if (mThrowAt == "register")
                throw std::runtime_error("register failed");
            if (mProvides)
                registrar.provide<PhaseContract>(mContract);
        }

        bool resolve(const ServiceLocator&) override
        {
            mLog.push_back(mId + " resolve");
            return true;
        }

        void start(Host&) override
        {
            mLog.push_back(mId + " start");
            if (mThrowAt == "start")
                throw std::runtime_error("start failed");
        }

        void stop() override { mLog.push_back(mId + " stop"); }

    private:
        std::string mId;
        std::vector<std::string>& mLog;
        bool mProvides;
        std::string mThrowAt;
        PhaseContract mContract;
    };

    // The lines of the log that are starts, in order
    std::vector<std::string> startsOf(const std::vector<std::string>& log)
    {
        std::vector<std::string> out;
        for (const std::string& line : log)
            if (line.size() > 6 && line.compare(line.size() - 6, 6, " start") == 0)
                out.push_back(line);
        return out;
    }

    std::vector<std::string> stopsOf(const std::vector<std::string>& log)
    {
        std::vector<std::string> out;
        for (const std::string& line : log)
            if (line.size() > 5 && line.compare(line.size() - 5, 5, " stop") == 0)
                out.push_back(line);
        return out;
    }

    class PhasesTest : public testing::Test
    {
    protected:
        HostImpl mHost;
        LifecycleManager mLifecycle{ mHost };
        std::vector<std::string> mLog;

        void add(const std::string& id, std::string kind = "feature", bool provides = false,
            std::vector<ContractRequirement> needs = {}, std::string throwAt = {})
        {
            std::vector<ContractRequirement> offered;
            if (provides)
                offered.push_back({ std::string(PhaseContract::kId), PhaseContract::kVersion });
            mLifecycle.addExtension(manifestOf(id, kind, offered, std::move(needs)),
                std::make_unique<Recording>(id, mLog, provides, std::move(throwAt)));
        }
    };

    TEST_F(PhasesTest, theGamePackStartsBeforeTheOtherExtensionsAndTheRestStartLater)
    {
        add("afeature");
        add("zgame", "game");

        EXPECT_TRUE(mLifecycle.hasGamePack());
        ASSERT_TRUE(mLifecycle.initializeGamePack());
        EXPECT_THAT(startsOf(mLog), ElementsAre("zgame start")) << "'afeature' sorts first and must wait";
        EXPECT_EQ(mLifecycle.getGamePackId(), "zgame");

        mLifecycle.initialize();
        EXPECT_THAT(startsOf(mLog), ElementsAre("zgame start", "afeature start"));
    }

    TEST_F(PhasesTest, everyExtensionIsRegisteredAndResolvedOnceNoMatterHowManyPhases)
    {
        add("afeature");
        add("zgame", "game");
        mLifecycle.initializeGamePack();
        mLifecycle.initialize();
        for (const char* line : { "afeature register", "afeature resolve", "zgame register", "zgame resolve" })
            EXPECT_EQ(std::count(mLog.begin(), mLog.end(), line), 1) << line;
    }

    TEST_F(PhasesTest, theGamePackStartsWithTheExtensionsItRequiresAndNotWithTheOthers)
    {
        add("provider", "feature", true);
        add("other");
        add("zgame", "game", false, { { std::string(PhaseContract::kId), PhaseContract::kVersion } });

        ASSERT_TRUE(mLifecycle.initializeGamePack());
        EXPECT_THAT(startsOf(mLog), ElementsAre("provider start", "zgame start"));
        mLifecycle.initialize();
        EXPECT_THAT(startsOf(mLog), ElementsAre("provider start", "zgame start", "other start"));
    }

    TEST_F(PhasesTest, withNoGamePackTheFirstPhaseDoesNothingAndInitializeIsTheOldSingleStart)
    {
        add("beta");
        add("alpha");
        EXPECT_FALSE(mLifecycle.hasGamePack());
        EXPECT_FALSE(mLifecycle.initializeGamePack());
        EXPECT_TRUE(mLog.empty()) << "no registration, no resolve, no start";

        mLifecycle.initialize();

        // The same extensions through initialize() alone
        HostImpl hostB;
        LifecycleManager plain(hostB);
        std::vector<std::string> plainLog;
        plain.addExtension(manifestOf("beta"), std::make_unique<Recording>("beta", plainLog));
        plain.addExtension(manifestOf("alpha"), std::make_unique<Recording>("alpha", plainLog));
        plain.initialize();
        EXPECT_EQ(mLog, plainLog);
    }

    TEST_F(PhasesTest, theGamePackPhaseRunsOnlyOnceAndNotAfterInitialize)
    {
        add("zgame", "game");
        EXPECT_TRUE(mLifecycle.initializeGamePack());
        EXPECT_FALSE(mLifecycle.initializeGamePack());
        mLifecycle.initialize();
        mLifecycle.initialize();
        EXPECT_THAT(startsOf(mLog), ElementsAre("zgame start"));
        EXPECT_FALSE(mLifecycle.initializeGamePack());
    }

    TEST_F(PhasesTest, initializeBeforeTheGamePackPhaseLeavesNothingForIt)
    {
        add("zgame", "game");
        mLifecycle.initialize();
        EXPECT_FALSE(mLifecycle.initializeGamePack());
        EXPECT_THAT(startsOf(mLog), ElementsAre("zgame start"));
    }

    TEST_F(PhasesTest, aGamePackThatFailsToStartIsTheSessionErrorAndTheOthersStillStart)
    {
        add("afeature");
        add("zgame", "game", false, {}, "start");
        ASSERT_TRUE(mLifecycle.initializeGamePack());
        EXPECT_TRUE(mLifecycle.hasSessionError());
        EXPECT_THAT(mLifecycle.getSessionError(), HasSubstr("zgame"));
        EXPECT_THAT(mLifecycle.getSessionError(), HasSubstr("start"));
        EXPECT_FALSE(mLifecycle.isSavingAllowed());

        mLifecycle.initialize();
        EXPECT_THAT(startsOf(mLog), ElementsAre("zgame start", "afeature start"));
        EXPECT_TRUE(mLifecycle.isExtensionActive("afeature"));
        EXPECT_FALSE(mLifecycle.isExtensionActive("zgame"));
    }

    TEST_F(PhasesTest, aGamePackThatFailsToRegisterIsTheSessionErrorAndNothingOfItStarts)
    {
        add("zgame", "game", false, {}, "register");
        mLifecycle.initializeGamePack();
        EXPECT_THAT(mLifecycle.getSessionError(), HasSubstr("registerServices"));
        EXPECT_TRUE(startsOf(mLog).empty());
    }

    TEST_F(PhasesTest, everythingStopsInTheReverseOfTheOrderItStartedAcrossBothPhases)
    {
        add("afeature");
        add("zgame", "game");
        mLifecycle.initializeGamePack();
        mLifecycle.initialize();
        mLifecycle.shutdown();
        EXPECT_THAT(stopsOf(mLog), ElementsAre("afeature stop", "zgame stop"));
    }

    // ---- The host: a built-in game pack and the rules ----------------------------------------------------------

    namespace sdkgame = openeden::sdk::game;

    class MarkerCrafting final : public sdkgame::CraftingRules
    {
    public:
        [[nodiscard]] std::unique_ptr<sdkgame::AlchemySession> beginAlchemy() override { return nullptr; }
        [[nodiscard]] std::unique_ptr<sdkgame::EnchantingSession> beginEnchanting() override { return nullptr; }
        [[nodiscard]] std::unique_ptr<sdkgame::RepairSession> beginRepair() override { return nullptr; }
        bool rechargeWithGem(sdkgame::ItemHandle, sdkgame::ItemHandle) override { return false; }
        [[nodiscard]] int gemSoul(sdkgame::ItemHandle) override { return 0; }
        [[nodiscard]] float rechargedCharge(float charge, float, float) override { return charge; }
        [[nodiscard]] bool knownAlchemyEffect(std::size_t, sdkgame::ItemHandle) override { return false; }
        [[nodiscard]] std::vector<std::string> ingredientEffectNames(sdkgame::ItemHandle, float) override { return {}; }
        [[nodiscard]] int apparatusSlotOf(sdkgame::ItemHandle) override { return -1; }
    };

    class FakeRules final : public sdkgame::GameRules
    {
    public:
        [[nodiscard]] std::string_view gameId() const noexcept override { return "fakegame"; }
        std::unique_ptr<sdkgame::CraftingRules> makeCraftingRules() override
        {
            return std::make_unique<MarkerCrafting>();
        }
    };

    // A built-in pack that provides FakeRules; start can throw
    class FakePack final : public Extension
    {
    public:
        explicit FakePack(bool throwInStart)
            : mThrowInStart(throwInStart)
        {
        }
        void registerServices(Registrar& registrar) override { registrar.provide<sdkgame::GameRules>(mRules); }
        bool resolve(const ServiceLocator&) override { return true; }
        void start(Host&) override
        {
            if (mThrowInStart)
                throw std::runtime_error("the pack cannot start");
        }
        void stop() override {}

    private:
        bool mThrowInStart;
        FakeRules mRules;
    };

    OMW::BuiltInGamePack fakePack(bool throwInStart = false)
    {
        return { manifestOf(
                     "fakegame", "game", { { std::string(sdkgame::GameRules::kId), sdkgame::GameRules::kVersion } }),
            [throwInStart] { return std::make_unique<FakePack>(throwInStart); } };
    }

    class BuiltInPackTest : public testing::Test
    {
    protected:
        void SetUp() override { MWWorld::installCraftingRules(nullptr); }
        void TearDown() override { MWWorld::installCraftingRules(nullptr); }
    };

    TEST_F(BuiltInPackTest, aBuiltInPackNamedByGameStartsFirstAndTheCoreTakesItsRules)
    {
        OMW::ExtensionHost host({ {}, {}, "fakegame", { fakePack() } });
        EXPECT_TRUE(host.hasExtensions());
        ASSERT_EQ(MWWorld::craftingRules(), nullptr);

        EXPECT_TRUE(host.startGamePack());
        EXPECT_NE(MWWorld::craftingRules(), nullptr) << "installed before start(), so before the windows exist";
        EXPECT_EQ(host.gamePackId(), "fakegame");
        EXPECT_TRUE(host.sessionError().empty());

        host.start();
        EXPECT_NE(MWWorld::craftingRules(), nullptr);
        host.stop();
        EXPECT_NE(MWWorld::craftingRules(), nullptr) << "a built-in pack's code stays, so its rules stay";
    }

    TEST_F(BuiltInPackTest, aBuiltInPackThatIsNotNamedByGameIsNotUsed)
    {
        OMW::ExtensionHost host({ {}, {}, "somethingelse", { fakePack() } });
        EXPECT_FALSE(host.hasExtensions());
        EXPECT_FALSE(host.startGamePack());
        EXPECT_THAT(host.takeFailureReport(), HasSubstr("somethingelse"));
        EXPECT_EQ(MWWorld::craftingRules(), nullptr);
    }

    TEST_F(BuiltInPackTest, withNoGameLineThereIsNoFirstPhaseAndNoRules)
    {
        OMW::ExtensionHost host({ {}, {}, "", { fakePack() } });
        EXPECT_FALSE(host.startGamePack());
        host.start();
        EXPECT_EQ(MWWorld::craftingRules(), nullptr);
        EXPECT_TRUE(host.gamePackId().empty());
    }

    TEST_F(BuiltInPackTest, aBuiltInPackThatFailsToStartInstallsNothingAndIsTheSessionError)
    {
        MWWorld::installCraftingRules(std::make_unique<MarkerCrafting>());
        const sdkgame::CraftingRules* before = MWWorld::craftingRules();
        OMW::ExtensionHost host({ {}, {}, "fakegame", { fakePack(true) } });
        EXPECT_TRUE(host.startGamePack());
        EXPECT_THAT(host.sessionError(), HasSubstr("fakegame"));
        EXPECT_EQ(MWWorld::craftingRules(), before) << "what was installed stays; the pack's rules did not arrive";
        EXPECT_THAT(host.takeSessionFault(), HasSubstr("fakegame"));
    }

    TEST_F(BuiltInPackTest, theFirstPhaseTwiceOrAfterStartDoesNothingMore)
    {
        OMW::ExtensionHost host({ {}, {}, "fakegame", { fakePack() } });
        EXPECT_TRUE(host.startGamePack());
        EXPECT_FALSE(host.startGamePack());
        host.start();
        EXPECT_FALSE(host.startGamePack());
    }

    // ---- The Morrowind pack as a built-in game pack -------------------------------------------------------------

    TEST(MorrowindPack, itsManifestIsAGamePackThatProvidesTheRulesAndIsNamedLikeTheGame)
    {
        const ExtensionManifest manifest = game_morrowind::packManifest();
        EXPECT_EQ(manifest.id, "morrowind");
        EXPECT_EQ(manifest.kind, "game");
        EXPECT_EQ(manifest.sdkVersion, kSdkVersion);
        ASSERT_EQ(manifest.providedContracts.size(), 1u);
        EXPECT_EQ(manifest.providedContracts[0].contract, sdkgame::GameRules::kId);
        EXPECT_EQ(game_morrowind::makeGameRules()->gameId(), manifest.id);
    }

    TEST_F(BuiltInPackTest, gameMorrowindStartsTheMorrowindPackFirstAndInstallsItsCraftingRules)
    {
        OMW::ExtensionHost host(
            { {}, {}, "morrowind", { { game_morrowind::packManifest(), &game_morrowind::makePackExtension } } });
        EXPECT_TRUE(host.startGamePack());
        EXPECT_NE(MWWorld::craftingRules(), nullptr);
        EXPECT_EQ(host.gamePackId(), "morrowind");
        EXPECT_EQ(host.gamePackVersion(), "1.0.0");
        EXPECT_TRUE(host.sessionError().empty());
        host.start();
        EXPECT_TRUE(host.sessionError().empty());
        host.stop();
        EXPECT_NE(MWWorld::craftingRules(), nullptr) << "the executable's code outlives the host";
    }
}
