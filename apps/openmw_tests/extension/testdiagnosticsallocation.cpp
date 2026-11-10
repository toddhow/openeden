// Task 0017 stage 5 (W3 leftover): collecting the extensions' debug draw and profiler once a frame allocates nothing.
//
// In plain words: with `[Game] extension debug draw` and `[Game] extension profiler` on, the engine does a fixed amount
// of work at the end of every Ui phase: copy the debug primitives into DebugDrawFrame, turn them into lines, snapshot
// the profiler, and time every extension callback. None of it may take memory from the heap once it is warm. The count
// comes from the allocation counter openmw-tests already has (apps/openmw_tests/support/allocationcounter.hpp).
#include <components/extension/debug_draw_frame.hpp>
#include <components/extension/debug_draw_geometry.hpp>
#include <components/extension/host_impl.hpp>
#include <components/extension/lifecycle_manager.hpp>
#include <components/extension/manifest.hpp>
#include <components/extension/profiler_snapshot.hpp>

#include <openeden/extension.hpp>
#include <openeden/phases.hpp>
#include <openeden/registrar.hpp>

#include <gtest/gtest.h>

#include <array>
#include <memory>

#include "apps/openmw_tests/support/allocationcounter.hpp"

namespace
{
    using openeden::sdk::Phase;

    // Short identifiers and label text stay inside std::string's small buffer, which is what a hot extension would do
    constexpr const char* kId = "alloc";

    class DrawingExtension final : public openeden::sdk::Extension
    {
    public:
        void registerServices(openeden::sdk::Registrar& registrar) override
        {
            for (const Phase phase : { Phase::Input, Phase::PreSimulation, Phase::PostSimulation, Phase::Animation,
                     Phase::PreRender, Phase::Ui })
                registrar.registerPhaseCallback(phase, [this, phase](float) {
                    if (phase != Phase::Animation || mDebugDraw == nullptr)
                        return;
                    const openeden::sdk::Color4f white;
                    for (int i = 0; i < 50; ++i)
                    {
                        const float x = static_cast<float>(i);
                        mDebugDraw->drawLine(kId, { x, 0, 0 }, { x, 1, 0 }, white);
                        mDebugDraw->drawPoint(kId, { x, 0, 1 }, white, 1.0f, false);
                    }
                    mDebugDraw->drawBox(kId, { 0, 0, 0 }, { 1, 1, 1 }, white);
                    mDebugDraw->drawSphere(kId, { 2, 2, 2 }, 1.0f, white, false);
                    mDebugDraw->drawText(kId, { 3, 3, 3 }, "label", white);
                    mProfiler->recordSample(kId, "work", 0.5);
                });
        }

        bool resolve(const openeden::sdk::ServiceLocator&) override { return true; }

        void start(openeden::sdk::Host& host) override
        {
            mDebugDraw = host.getDebugDrawService();
            mProfiler = host.getProfilerService();
        }

        void stop() override {}

        openeden::sdk::DebugDrawService* mDebugDraw = nullptr;
        openeden::sdk::ProfilerService* mProfiler = nullptr;
    };

    struct Collector
    {
        openeden::extension::HostImpl* host = nullptr;
        openeden::extension::DebugDrawFrame* frame = nullptr;
        openeden::extension::ProfilerSnapshot* snapshot = nullptr;
        std::size_t lines = 0;
    };

    // What ExtensionHost::collectFrameEnd and RenderManager::updateExtensionDebug do each frame, minus the renderer
    void collect(void* context, float)
    {
        auto& c = *static_cast<Collector*>(context);
        c.frame->collect(c.host->debugDrawService());
        c.host->profilerService().snapshot(*c.snapshot);
        for (const bool depthTest : { true, false })
            c.lines += openeden::extension::expandDebugDrawFrame(*c.frame, depthTest, 70.0f,
                [](const openeden::sdk::Vector3&, const openeden::sdk::Vector3&, const openeden::sdk::Color4f&) {});
    }

    TEST(DiagnosticsAllocation, CollectingAndTimingAFrameAllocatesNothing)
    {
        openeden::extension::HostImpl host;
        openeden::extension::LifecycleManager manager(host);
        openeden::extension::ExtensionManifest manifest;
        manifest.version = 1;
        manifest.id = kId;
        manifest.name = kId;
        manifest.extensionVersion = "1.0.0";
        manifest.kind = "feature";
        manifest.toolchain = "t";
        manager.addExtension(std::move(manifest), std::make_unique<DrawingExtension>());
        ASSERT_TRUE(manager.initialize());

        auto frame = std::make_unique<openeden::extension::DebugDrawFrame>();
        auto snapshot = std::make_unique<openeden::extension::ProfilerSnapshot>();
        Collector collector{ &host, frame.get(), snapshot.get() };
        manager.setFrameEndHook(&collect, &collector);
        manager.setPhaseTiming(true);

        constexpr std::array phases = { Phase::Input, Phase::PreSimulation, Phase::PostSimulation, Phase::Animation,
            Phase::PreRender, Phase::Ui };

        // Warm-up frames: the profiler registers its scopes, the services' lists reach their working size
        for (int warm = 0; warm < 3; ++warm)
            for (const Phase phase : phases)
                manager.runPhase(phase, 0.016f);
        ASSERT_GT(frame->primitiveCount(), 100u);
        ASSERT_GE(snapshot->count, 2u);
        collector.lines = 0;

        TestSupport::AllocationCounter counter;
        for (int i = 0; i < 1000; ++i)
            for (const Phase phase : phases)
                manager.runPhase(phase, 0.016f);
        EXPECT_EQ(counter.count(), 0u);
        EXPECT_GT(collector.lines, 0u);
    }
}
