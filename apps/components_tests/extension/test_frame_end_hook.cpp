// Task 0017 stage 5 (W3 leftover): the frame-end hook and the optional per-phase timing of the lifecycle manager.
//
// In plain words: debug draw and the profiler are reset at the end of Phase::Ui, but the renderer and the debug window
// need what extensions submitted during the frame. The manager therefore calls a hook just before that reset, and can
// also time every extension callback into the profiler (off by default).
#include <components/extension/debug_draw_service.hpp>
#include <components/extension/host_impl.hpp>
#include <components/extension/lifecycle_manager.hpp>
#include <components/extension/manifest.hpp>
#include <components/extension/profiler_service.hpp>

#include <openeden/debug_draw.hpp>
#include <openeden/extension.hpp>
#include <openeden/phases.hpp>
#include <openeden/profiling.hpp>
#include <openeden/registrar.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
    using openeden::extension::HostImpl;
    using openeden::extension::LifecycleManager;
    using openeden::sdk::Phase;

    constexpr Phase kAllPhases[]
        = { Phase::Input, Phase::PreSimulation, Phase::PostSimulation, Phase::Animation, Phase::PreRender, Phase::Ui };

    class DrawingExtension final : public openeden::sdk::Extension
    {
    public:
        void registerServices(openeden::sdk::Registrar& registrar) override
        {
            for (const Phase phase : kAllPhases)
                registrar.registerPhaseCallback(phase, [this, phase](float) {
                    mPhasesRun.push_back(phase);
                    if (phase == Phase::Ui && mDebugDraw != nullptr)
                    {
                        mDebugDraw->drawLine("drawer", {}, { 1, 0, 0 }, openeden::sdk::Color4f{});
                        mProfiler->recordSample("drawer", "ui.work", 2.0);
                    }
                });
        }

        bool resolve(const openeden::sdk::ServiceLocator&) override { return true; }

        void start(openeden::sdk::Host& host) override
        {
            mDebugDraw = host.getDebugDrawService();
            mProfiler = host.getProfilerService();
        }

        void stop() override {}

        std::vector<Phase> mPhasesRun;
        openeden::sdk::DebugDrawService* mDebugDraw = nullptr;
        openeden::sdk::ProfilerService* mProfiler = nullptr;
    };

    openeden::extension::ExtensionManifest manifest(const std::string& id)
    {
        openeden::extension::ExtensionManifest result;
        result.version = 1;
        result.id = id;
        result.name = id;
        result.extensionVersion = "1.0.0";
        result.kind = "feature";
        result.toolchain = "t";
        return result;
    }

    struct HookLog
    {
        HostImpl* host = nullptr;
        int calls = 0;
        float lastDt = -1.0f;
        std::size_t linesSeen = 0;
        bool profilerScopeSeen = false;
        std::vector<Phase>* phasesAtCall = nullptr;
    };

    void hook(void* context, float dt)
    {
        auto& log = *static_cast<HookLog*>(context);
        ++log.calls;
        log.lastDt = dt;
        log.linesSeen = log.host->debugDrawService().extractSnapshot().lines.size();
        log.profilerScopeSeen = log.host->profilerService().hasScope("drawer", "ui.work");
        if (log.phasesAtCall != nullptr)
            *log.phasesAtCall = {};
    }

    struct Fixture
    {
        HostImpl host;
        LifecycleManager manager{ host };
        DrawingExtension* extension = nullptr;

        Fixture()
        {
            auto instance = std::make_unique<DrawingExtension>();
            extension = instance.get();
            manager.addExtension(manifest("drawer"), std::move(instance));
            EXPECT_TRUE(manager.initialize());
        }
    };

    TEST(FrameEndHook, RunsOnceAtTheEndOfTheUiPhaseOnly)
    {
        Fixture f;
        HookLog log;
        log.host = &f.host;
        f.manager.setFrameEndHook(&hook, &log);

        for (const Phase phase : kAllPhases)
        {
            if (phase == Phase::Ui)
                continue;
            f.manager.runPhase(phase, 0.016f);
            EXPECT_EQ(log.calls, 0);
        }
        f.manager.runPhase(Phase::Ui, 0.25f);
        EXPECT_EQ(log.calls, 1);
        EXPECT_FLOAT_EQ(log.lastDt, 0.25f);
    }

    TEST(FrameEndHook, SeesTheFramesSubmissionsAndScopesBeforeTheyAreReset)
    {
        Fixture f;
        HookLog log;
        log.host = &f.host;
        f.manager.setFrameEndHook(&hook, &log);

        f.manager.runPhase(Phase::Ui, 0.016f);
        EXPECT_EQ(log.linesSeen, 1u) << "the Ui callback's line was gone before the hook";
        EXPECT_TRUE(log.profilerScopeSeen) << "the Ui callback's scope was reset before the hook";

        // After the phase the end-of-frame reset has happened as before
        EXPECT_TRUE(f.host.debugDrawService().extractSnapshot().empty());
        EXPECT_FALSE(f.host.profilerService().hasScope("drawer", "ui.work"));
    }

    TEST(FrameEndHook, NoHookMeansTheResetStillHappens)
    {
        Fixture f;
        f.manager.runPhase(Phase::Ui, 0.016f);
        EXPECT_TRUE(f.host.debugDrawService().extractSnapshot().empty());
        EXPECT_FALSE(f.host.profilerService().hasScope("drawer", "ui.work"));
    }

    TEST(FrameEndHook, CanBeRemoved)
    {
        Fixture f;
        HookLog log;
        log.host = &f.host;
        f.manager.setFrameEndHook(&hook, &log);
        f.manager.runPhase(Phase::Ui, 0.016f);
        f.manager.setFrameEndHook(nullptr, nullptr);
        f.manager.runPhase(Phase::Ui, 0.016f);
        EXPECT_EQ(log.calls, 1);
    }

    TEST(PhaseTiming, IsOffByDefaultAndRecordsNothing)
    {
        Fixture f;
        EXPECT_FALSE(f.manager.phaseTiming());
        for (const Phase phase : kAllPhases)
            f.manager.runPhase(phase, 0.016f);
        for (const Phase phase : kAllPhases)
            EXPECT_FALSE(f.host.profilerService().hasScope("drawer", LifecycleManager::phaseScopeName(phase)));
    }

    TEST(PhaseTiming, RecordsEveryCallbackUnderItsExtensionAndPhaseName)
    {
        Fixture f;
        f.manager.setPhaseTiming(true);
        EXPECT_TRUE(f.manager.phaseTiming());
        f.manager.runPhase(Phase::Input, 0.016f);
        f.manager.runPhase(Phase::Input, 0.016f);
        f.manager.runPhase(Phase::Animation, 0.016f);

        const auto input
            = f.host.profilerService().getScopeStats("drawer", LifecycleManager::phaseScopeName(Phase::Input));
        EXPECT_EQ(input.callCount, 2u);
        EXPECT_GE(input.totalTimeMs, 0.0);
        const auto animation
            = f.host.profilerService().getScopeStats("drawer", LifecycleManager::phaseScopeName(Phase::Animation));
        EXPECT_EQ(animation.callCount, 1u);
        EXPECT_FALSE(f.host.profilerService().hasScope("drawer", LifecycleManager::phaseScopeName(Phase::PreRender)));
    }

    TEST(PhaseTiming, ATimedPhaseStillRunsTheCallbacksInOrder)
    {
        Fixture f;
        f.manager.setPhaseTiming(true);
        for (const Phase phase : kAllPhases)
            f.manager.runPhase(phase, 0.016f);
        ASSERT_EQ(f.extension->mPhasesRun.size(), std::size(kAllPhases));
        for (std::size_t i = 0; i < std::size(kAllPhases); ++i)
            EXPECT_EQ(f.extension->mPhasesRun[i], kAllPhases[i]);
    }

    TEST(PhaseTiming, TheUiPhaseTimingIsVisibleToTheFrameEndHook)
    {
        Fixture f;
        f.manager.setPhaseTiming(true);
        struct Seen
        {
            HostImpl* host;
            std::uint64_t calls = 0;
        } seen{ &f.host };
        f.manager.setFrameEndHook(
            [](void* context, float) {
                auto& s = *static_cast<Seen*>(context);
                s.calls = s.host->profilerService()
                              .getScopeStats("drawer", LifecycleManager::phaseScopeName(Phase::Ui))
                              .callCount;
            },
            &seen);
        f.manager.runPhase(Phase::Ui, 0.016f);
        EXPECT_EQ(seen.calls, 1u);
    }

    TEST(PhaseTiming, ScopeNamesAreDistinctAndPrefixedAndFitTheProfilersNameLength)
    {
        std::set<std::string> names;
        for (const Phase phase : { Phase::Input, Phase::PreSimulation, Phase::PostSimulation, Phase::Animation,
                 Phase::PreRender, Phase::Ui, Phase::GameSimulation, Phase::GameUpdateUi })
        {
            const std::string name = LifecycleManager::phaseScopeName(phase);
            EXPECT_EQ(name.rfind("phase.", 0), 0u) << name;
            EXPECT_GT(name.size(), 6u) << name;
            EXPECT_LT(name.size(), openeden::extension::ProfilerServiceImpl::MaxNameLen);
            names.insert(name);
        }
        EXPECT_EQ(names.size(), 8u);
        EXPECT_STREQ(LifecycleManager::phaseScopeName(Phase::Input), "phase.Input");
        EXPECT_STREQ(LifecycleManager::phaseScopeName(Phase::GameUpdateUi), "phase.GameUpdateUi");
    }
}
