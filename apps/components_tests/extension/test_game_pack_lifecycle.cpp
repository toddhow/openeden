// Task 0084: capabilities by kind, the game phases, one game pack per session, and a failing game pack.
// Written from the spec (docs/tasks/0084-game-pack-tier-sdk-4.md) before the code.
#include <components/extension/host_impl.hpp>
#include <components/extension/lifecycle_manager.hpp>
#include <components/extension/manifest.hpp>
#include <openeden/extension.hpp>
#include <openeden/game/content.hpp>
#include <openeden/game/records.hpp>
#include <openeden/game/save_chunks.hpp>
#include <openeden/game/script_host.hpp>
#include <openeden/game/ui.hpp>
#include <openeden/host.hpp>
#include <openeden/pack_kind.hpp>
#include <openeden/phases.hpp>
#include <openeden/registrar.hpp>
#include <openeden/service_locator.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using openeden::extension::ExtensionManifest;
    using openeden::extension::HostImpl;
    using openeden::extension::LifecycleManager;
    using openeden::sdk::Extension;
    using openeden::sdk::Host;
    using openeden::sdk::HostInfo;
    using openeden::sdk::PackKind;
    using openeden::sdk::Phase;
    using openeden::sdk::Registrar;
    using openeden::sdk::ServiceLocator;

    ExtensionManifest makeManifest(const std::string& id, PackKind kind, const std::vector<std::string>& loadAfter = {})
    {
        ExtensionManifest m;
        m.version = kind == PackKind::Game ? 2 : 1;
        m.id = id;
        m.name = id;
        m.extensionVersion = "1.0.0";
        m.kind = std::string(openeden::sdk::toString(kind));
        m.sdkVersion = openeden::sdk::kSdkVersion;
        m.toolchain = "test-toolchain";
        m.loadAfter = loadAfter;
        return m;
    }

    // Records which services start() was given and what each frame callback did.
    struct Seen
    {
        openeden::sdk::SaveChunkService* saveChunks = nullptr;
        openeden::sdk::RecordService* records = nullptr;
        openeden::sdk::ContentService* content = nullptr;
        openeden::sdk::ScriptHostService* scriptHost = nullptr;
        openeden::sdk::UiService* ui = nullptr;
        openeden::sdk::EventBus* eventBus = nullptr;
        bool started = false;
    };

    class ProbeExtension : public Extension
    {
    public:
        ProbeExtension(std::string id, std::vector<std::string>& calls, Seen& seen)
            : mId(std::move(id))
            , mCalls(calls)
            , mSeen(seen)
        {
        }

        void registerServices(Registrar& registrar) override
        {
            for (const Phase phase : mPhases)
            {
                registrar.registerPhaseCallback(phase, [this, phase](float) {
                    mCalls.push_back(mId + ":" + std::to_string(static_cast<int>(phase)));
                    if (mThrowInPhase)
                        throw std::runtime_error("phase boom");
                });
            }
        }

        bool resolve(const ServiceLocator&) override { return true; }

        void start(Host& host) override
        {
            mSeen.started = true;
            mSeen.saveChunks = host.getSaveChunkService();
            mSeen.records = host.getRecordService();
            mSeen.content = host.getContentService();
            mSeen.scriptHost = host.getScriptHostService();
            mSeen.ui = host.getUiService();
            mSeen.eventBus = host.getEventBus();
            if (mThrowInStart)
                throw std::runtime_error("start boom");
        }

        void stop() override {}

        std::vector<Phase> mPhases;
        bool mThrowInPhase = false;
        bool mThrowInStart = false;

    private:
        std::string mId;
        std::vector<std::string>& mCalls;
        Seen& mSeen;
    };

    struct Fixture
    {
        HostImpl host;
        LifecycleManager manager{ host };
        std::vector<std::string> calls;
        std::vector<std::unique_ptr<Seen>> seen;

        // Adds a probe; the returned pointer stays valid until the manager destroys it.
        ProbeExtension* add(const std::string& id, PackKind kind, std::vector<Phase> phases = {},
            const std::vector<std::string>& loadAfter = {})
        {
            seen.push_back(std::make_unique<Seen>());
            auto probe = std::make_unique<ProbeExtension>(id, calls, *seen.back());
            probe->mPhases = std::move(phases);
            ProbeExtension* raw = probe.get();
            manager.addExtension(makeManifest(id, kind, loadAfter), std::move(probe));
            return raw;
        }

        Seen& seenOf(std::size_t index) { return *seen.at(index); }
    };

    bool mentions(const std::string& text, const std::string& word)
    {
        return text.find(word) != std::string::npos;
    }
}

// ---- Capabilities by kind ----

TEST(GamePackCapabilities, FeatureExtensionGetsNoGameServiceButKeepsTheOrdinaryOnes)
{
    Fixture f;
    f.add("feat", PackKind::Feature);
    ASSERT_TRUE(f.manager.initialize());
    const Seen& seen = f.seenOf(0);
    ASSERT_TRUE(seen.started);
    EXPECT_EQ(seen.saveChunks, nullptr);
    EXPECT_EQ(seen.records, nullptr);
    EXPECT_EQ(seen.content, nullptr);
    EXPECT_EQ(seen.scriptHost, nullptr);
    EXPECT_EQ(seen.ui, nullptr);
    EXPECT_NE(seen.eventBus, nullptr);
}

TEST(GamePackCapabilities, CompatibilityPackGetsWhatExtensionsGet)
{
    Fixture f;
    f.add("compat", PackKind::Compatibility);
    ASSERT_TRUE(f.manager.initialize());
    const Seen& seen = f.seenOf(0);
    ASSERT_TRUE(seen.started);
    EXPECT_EQ(seen.saveChunks, nullptr);
    EXPECT_EQ(seen.records, nullptr);
    EXPECT_EQ(seen.content, nullptr);
    EXPECT_EQ(seen.scriptHost, nullptr);
    EXPECT_EQ(seen.ui, nullptr);
    EXPECT_NE(seen.eventBus, nullptr);
}

TEST(GamePackCapabilities, GamePackGetsEveryGameService)
{
    Fixture f;
    f.add("game", PackKind::Game);
    ASSERT_TRUE(f.manager.initialize());
    const Seen& seen = f.seenOf(0);
    ASSERT_TRUE(seen.started);
    EXPECT_NE(seen.saveChunks, nullptr);
    EXPECT_NE(seen.records, nullptr);
    EXPECT_NE(seen.content, nullptr);
    EXPECT_NE(seen.scriptHost, nullptr);
    EXPECT_NE(seen.ui, nullptr);
    EXPECT_NE(seen.eventBus, nullptr);
    EXPECT_TRUE(f.manager.getCapabilityDenials().empty());
}

TEST(GamePackCapabilities, GameServicesAreTheCoresOwn)
{
    Fixture f;
    f.add("game", PackKind::Game);
    ASSERT_TRUE(f.manager.initialize());
    // A record type the pack registers through its service is in the core's table
    ASSERT_NE(f.seenOf(0).records, nullptr);
    EXPECT_TRUE(
        f.seenOf(0).records->registerRecordType({ openeden::sdk::makeRecordTypeId('N', 'P', 'C', '_'), "NPC_", 64 }));
    EXPECT_NE(f.host.records().findRecordType(openeden::sdk::makeRecordTypeId('N', 'P', 'C', '_')), nullptr);
}

TEST(GamePackCapabilities, EachDeniedRequestIsRecordedWithTheExtensionKindAndService)
{
    Fixture f;
    f.add("feat", PackKind::Feature);
    ASSERT_TRUE(f.manager.initialize());
    const auto& denials = f.manager.getCapabilityDenials();
    ASSERT_EQ(denials.size(), 5u);
    const std::vector<std::string> services = { "save chunk", "record", "content", "script host", "window" };
    for (std::size_t i = 0; i < denials.size(); ++i)
    {
        EXPECT_EQ(denials[i].extensionId, "feat");
        EXPECT_TRUE(mentions(denials[i].message, "feat")) << denials[i].message;
        EXPECT_TRUE(mentions(denials[i].message, "feature")) << denials[i].message;
        EXPECT_TRUE(mentions(denials[i].message, "game")) << denials[i].message;
        EXPECT_TRUE(mentions(denials[i].what, services[i])) << denials[i].what;
    }
}

TEST(GamePackCapabilities, SaveChunkRequestFromAFeatureExtensionNamesDecisionFive)
{
    Fixture f;
    f.add("feat", PackKind::Feature);
    ASSERT_TRUE(f.manager.initialize());
    const auto& denials = f.manager.getCapabilityDenials();
    ASSERT_FALSE(denials.empty());
    EXPECT_TRUE(mentions(denials.front().message, "0005")) << denials.front().message;
}

TEST(GamePackCapabilities, AGameServiceFromAHostThatIsNotHostImplIsNullNotACrash)
{
    class BareHost : public Host
    {
    public:
        HostInfo mInfo{ openeden::sdk::kSdkVersion, "Bare" };
        const HostInfo& getHostInfo() const override { return mInfo; }
    };
    BareHost host;
    LifecycleManager manager(host);
    std::vector<std::string> calls;
    Seen seen;
    manager.addExtension(makeManifest("game", PackKind::Game), std::make_unique<ProbeExtension>("game", calls, seen));
    ASSERT_TRUE(manager.initialize());
    EXPECT_TRUE(seen.started);
    EXPECT_EQ(seen.saveChunks, nullptr);
    EXPECT_EQ(seen.records, nullptr);
}

TEST(GamePackCapabilities, GamePhaseCallbackFromAFeatureExtensionIsRefusedAndNeverRuns)
{
    Fixture f;
    f.add("feat", PackKind::Feature, { Phase::GameSimulation, Phase::GameUpdateUi, Phase::PostSimulation });
    ASSERT_TRUE(f.manager.initialize());
    f.manager.runPhase(Phase::GameSimulation, 0.016f);
    f.manager.runPhase(Phase::GameUpdateUi, 0.016f);
    f.manager.runPhase(Phase::PostSimulation, 0.016f);
    ASSERT_EQ(f.calls.size(), 1u);
    EXPECT_EQ(f.calls[0], "feat:" + std::to_string(static_cast<int>(Phase::PostSimulation)));

    bool sawSimulation = false;
    bool sawUi = false;
    for (const auto& denial : f.manager.getCapabilityDenials())
    {
        sawSimulation = sawSimulation || mentions(denial.what, "GameSimulation");
        sawUi = sawUi || mentions(denial.what, "GameUpdateUi");
        EXPECT_TRUE(mentions(denial.message, "feat")) << denial.message;
    }
    EXPECT_TRUE(sawSimulation);
    EXPECT_TRUE(sawUi);
    // The extension itself is not faulted by asking
    EXPECT_TRUE(f.manager.isExtensionActive("feat"));
}

// ---- The game phases and their order ----

TEST(GamePackPhases, GamePackRunsInTheGamePhases)
{
    Fixture f;
    f.add("game", PackKind::Game, { Phase::GameSimulation, Phase::GameUpdateUi });
    ASSERT_TRUE(f.manager.initialize());
    f.manager.runPhase(Phase::GameSimulation, 0.016f);
    f.manager.runPhase(Phase::GameUpdateUi, 0.016f);
    EXPECT_EQ(f.calls,
        (std::vector<std::string>{ "game:" + std::to_string(static_cast<int>(Phase::GameSimulation)),
            "game:" + std::to_string(static_cast<int>(Phase::GameUpdateUi)) }));
}

TEST(GamePackPhases, GamePackCallbackRunsBeforeExtensionsOfTheSamePhaseWhateverTheIdOrder)
{
    Fixture f;
    // "aaa" sorts before "zzz", and neither depends on the other: without the rule the extension would run first.
    f.add("aaa_ext", PackKind::Feature, { Phase::PostSimulation });
    f.add("zzz_game", PackKind::Game, { Phase::PostSimulation });
    f.add("mmm_compat", PackKind::Compatibility, { Phase::PostSimulation });
    ASSERT_TRUE(f.manager.initialize());
    f.manager.runPhase(Phase::PostSimulation, 0.016f);
    const std::string phase = std::to_string(static_cast<int>(Phase::PostSimulation));
    EXPECT_EQ(f.calls, (std::vector<std::string>{ "zzz_game:" + phase, "aaa_ext:" + phase, "mmm_compat:" + phase }));
}

TEST(GamePackPhases, TheOrderDoesNotDependOnTheOrderPacksWereAdded)
{
    std::vector<std::vector<std::string>> results;
    const std::vector<std::vector<int>> orders = { { 0, 1, 2 }, { 2, 1, 0 }, { 1, 2, 0 } };
    for (const auto& order : orders)
    {
        Fixture f;
        const struct
        {
            const char* id;
            PackKind kind;
        } packs[] = { { "aaa_ext", PackKind::Feature }, { "zzz_game", PackKind::Game },
            { "mmm_compat", PackKind::Compatibility } };
        for (const int index : order)
            f.add(packs[index].id, packs[index].kind, { Phase::PreSimulation, Phase::Ui });
        ASSERT_TRUE(f.manager.initialize());
        f.manager.runPhase(Phase::PreSimulation, 0.016f);
        f.manager.runPhase(Phase::Ui, 0.016f);
        results.push_back(f.calls);
        EXPECT_EQ(f.manager.getLoadOrder(), (std::vector<std::string>{ "aaa_ext", "mmm_compat", "zzz_game" }));
    }
    EXPECT_EQ(results[0], results[1]);
    EXPECT_EQ(results[0], results[2]);
}

// ---- One game pack per session ----

TEST(GamePackSession, SecondGamePackIsRefusedWithAMessageNamingBoth)
{
    Fixture f;
    f.add("game_b", PackKind::Game, { Phase::GameSimulation });
    f.add("game_a", PackKind::Game, { Phase::GameSimulation });
    ASSERT_TRUE(f.manager.initialize());

    EXPECT_EQ(f.manager.getGamePackId(), "game_a");
    EXPECT_TRUE(f.manager.isExtensionActive("game_a"));
    EXPECT_FALSE(f.manager.isExtensionActive("game_b"));

    const auto& failures = f.manager.getFailures();
    const auto refusal = std::find_if(failures.begin(), failures.end(),
        [](const LifecycleManager::Failure& failure) { return failure.extensionId == "game_b"; });
    ASSERT_NE(refusal, failures.end());
    EXPECT_TRUE(mentions(refusal->reason, "game_a")) << refusal->reason;
    EXPECT_TRUE(mentions(refusal->reason, "game_b")) << refusal->reason;

    // Only the first one ran
    f.manager.runPhase(Phase::GameSimulation, 0.016f);
    ASSERT_EQ(f.calls.size(), 1u);
    EXPECT_EQ(f.calls[0].substr(0, 6), "game_a");
    // The refused pack never started
    EXPECT_FALSE(f.seenOf(0).started);
    EXPECT_TRUE(f.seenOf(1).started);
}

TEST(GamePackSession, NoGamePackMeansNoGameIdAndNoSessionError)
{
    Fixture f;
    f.add("feat", PackKind::Feature, { Phase::PostSimulation });
    ASSERT_TRUE(f.manager.initialize());
    EXPECT_TRUE(f.manager.getGamePackId().empty());
    EXPECT_FALSE(f.manager.hasSessionError());
    EXPECT_TRUE(f.manager.isSavingAllowed());
}

// ---- A failing game pack ----

TEST(GamePackFailure, GamePackThatThrowsInStartEndsTheSessionNamingItAndRefusesSaving)
{
    Fixture f;
    f.add("game", PackKind::Game)->mThrowInStart = true;
    ASSERT_TRUE(f.manager.initialize());
    EXPECT_TRUE(f.manager.hasSessionError());
    EXPECT_TRUE(mentions(f.manager.getSessionError(), "game")) << f.manager.getSessionError();
    EXPECT_TRUE(mentions(f.manager.getSessionError(), "start")) << f.manager.getSessionError();
    EXPECT_TRUE(mentions(f.manager.getSessionError(), "start boom")) << f.manager.getSessionError();
    EXPECT_FALSE(f.manager.isSavingAllowed());
    EXPECT_TRUE(f.host.saveChunks().isSavingRefused());
    const auto collected = f.host.saveChunks().collect();
    EXPECT_FALSE(collected.ok);
    EXPECT_TRUE(mentions(collected.error, "game")) << collected.error;
}

TEST(GamePackFailure, GamePackThatThrowsInAPhaseEndsTheSession)
{
    Fixture f;
    f.add("game", PackKind::Game, { Phase::GameSimulation })->mThrowInPhase = true;
    ASSERT_TRUE(f.manager.initialize());
    EXPECT_FALSE(f.manager.hasSessionError());
    EXPECT_NO_THROW(f.manager.runPhase(Phase::GameSimulation, 0.016f));
    EXPECT_TRUE(f.manager.hasSessionError());
    EXPECT_TRUE(mentions(f.manager.getSessionError(), "game")) << f.manager.getSessionError();
    EXPECT_FALSE(f.manager.isSavingAllowed());
    // And it does not run again
    f.manager.runPhase(Phase::GameSimulation, 0.016f);
    EXPECT_EQ(f.calls.size(), 1u);
}

TEST(GamePackFailure, AFailingFeatureExtensionIsNotASessionError)
{
    Fixture f;
    f.add("game", PackKind::Game);
    f.add("feat", PackKind::Feature, { Phase::PostSimulation })->mThrowInPhase = true;
    ASSERT_TRUE(f.manager.initialize());
    f.manager.runPhase(Phase::PostSimulation, 0.016f);
    EXPECT_TRUE(f.manager.isExtensionFaulted("feat"));
    EXPECT_FALSE(f.manager.hasSessionError());
    EXPECT_TRUE(f.manager.isSavingAllowed());
    EXPECT_FALSE(f.host.saveChunks().isSavingRefused());
}

// ---- The empty host ----

TEST(GamePackEmptyHost, ZeroExtensionsAndZeroGamePacksRunEveryPhaseIncludingTheGamePhases)
{
    HostImpl host;
    LifecycleManager manager(host);
    ASSERT_TRUE(manager.initialize());
    EXPECT_EQ(manager.getActiveExtensionCount(), 0u);
    EXPECT_TRUE(manager.getGamePackId().empty());
    EXPECT_TRUE(manager.getCapabilityDenials().empty());
    for (int frame = 0; frame < 120; ++frame)
    {
        for (const Phase phase : { Phase::Input, Phase::GameSimulation, Phase::PreSimulation, Phase::PostSimulation,
                 Phase::Animation, Phase::PreRender, Phase::GameUpdateUi, Phase::Ui })
        {
            EXPECT_NO_THROW(manager.runPhase(phase, 1.0f / 60.0f));
        }
    }
    manager.shutdown();
    EXPECT_EQ(host.saveChunks().handlerCount(), 0u);
}
