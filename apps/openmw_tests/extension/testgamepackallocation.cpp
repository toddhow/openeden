// Task 0084: running the game phases allocates nothing, with a game pack and an ordinary extension registered.
//
// In plain words: LifecycleManager::runPhase walks the registered callbacks in two passes (the game pack's first). That
// walk, and the game-pack callbacks themselves, must not take memory from the heap. The count comes from the
// allocation counter openmw-tests already has (apps/openmw_tests/support/allocationcounter.hpp).
#include <components/extension/host_impl.hpp>
#include <components/extension/lifecycle_manager.hpp>
#include <components/extension/manifest.hpp>

#include <openeden/extension.hpp>
#include <openeden/pack_kind.hpp>
#include <openeden/phases.hpp>

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>

#include "apps/openmw_tests/support/allocationcounter.hpp"

namespace
{
    using openeden::sdk::PackKind;
    using openeden::sdk::Phase;

    class CountingPack final : public openeden::sdk::Extension
    {
    public:
        void registerServices(openeden::sdk::Registrar& registrar) override
        {
            for (const Phase phase : { Phase::GameSimulation, Phase::PostSimulation, Phase::GameUpdateUi, Phase::Ui })
                registrar.registerPhaseCallback(phase, [this](float dt) { mTotal += dt; });
        }

        bool resolve(const openeden::sdk::ServiceLocator&) override { return true; }
        void start(openeden::sdk::Host&) override {}
        void stop() override {}

        float mTotal = 0.f;
    };

    openeden::extension::ExtensionManifest manifest(const std::string& id, PackKind kind)
    {
        openeden::extension::ExtensionManifest result;
        result.version = kind == PackKind::Game ? 2 : 1;
        result.id = id;
        result.name = id;
        result.extensionVersion = "1.0.0";
        result.kind = std::string(openeden::sdk::toString(kind));
        result.toolchain = "t";
        return result;
    }

    TEST(GamePackAllocation, GamePhasesAndTheFramePhasesAllocateNothingWithAGamePackLoaded)
    {
        openeden::extension::HostImpl host;
        openeden::extension::LifecycleManager manager(host);
        manager.addExtension(manifest("game_alloc", PackKind::Game), std::make_unique<CountingPack>());
        manager.addExtension(manifest("feature_alloc", PackKind::Feature), std::make_unique<CountingPack>());
        ASSERT_TRUE(manager.initialize());
        ASSERT_EQ(manager.getActiveExtensionCount(), 2u);

        constexpr std::array phases = { Phase::Input, Phase::GameSimulation, Phase::PreSimulation,
            Phase::PostSimulation, Phase::Animation, Phase::PreRender, Phase::GameUpdateUi, Phase::Ui };

        // One warm-up frame, then a counted window
        for (const Phase phase : phases)
            manager.runPhase(phase, 0.016f);

        TestSupport::AllocationCounter counter;
        for (int frame = 0; frame < 1000; ++frame)
        {
            for (const Phase phase : phases)
                manager.runPhase(phase, 0.016f);
        }
        EXPECT_EQ(counter.count(), 0u);
    }
}
