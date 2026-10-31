#pragma once

#include <cstdint>
#include <functional>

namespace openeden::sdk
{
    /**
     * @brief Fixed frame phases executed in named order.
     *
     * Plugins run only per frame: stepping physics deterministically is a core concern (decision 0003).
     */
    enum class Phase : std::uint8_t
    {
        Input,
        PreSimulation,
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
} // namespace openeden::sdk
