#pragma once

#include <cstdint>
#include <functional>

namespace openeden::sdk
{
    /**
     * @brief Fixed frame and simulation phases executed in named order.
     */
    enum class Phase : std::uint8_t
    {
        Input,
        PreSimulation,
        FixedStepPrePhysics,
        FixedStepPostPhysics,
        PostSimulation,
        Animation,
        PreRender,
        Ui
    };

    /**
     * @brief Frame phase callback signature.
     * @param dt Frame delta time in seconds.
     */
    using PhaseCallback = std::function<void(float dt)>;

    /**
     * @brief Fixed physics step phase callback signature.
     * @param dt Fixed physics step delta time in seconds.
     * @param stepIndex Monotonically increasing fixed step index.
     */
    using FixedStepPhaseCallback = std::function<void(float dt, std::uint64_t stepIndex)>;
} // namespace openeden::sdk
