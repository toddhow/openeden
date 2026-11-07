#pragma once

#include <cstdint>
#include <functional>

namespace openeden::sdk
{
    /**
     * @brief Fixed frame phases executed in named order.
     *
     * Extensions run only per frame: stepping physics deterministically is a core concern (decision 0003).
     */
    enum class Phase : std::uint8_t
    {
        Input,
        PreSimulation,
        PostSimulation,
        Animation,
        PreRender,
        Ui,
        /// Game packs only (docs/design/game-packs.md section 4): the host runs it where the game's mechanics and
        /// scripts update, before Phase::PreSimulation. A callback from any other kind is refused at registration.
        GameSimulation,
        /// Game packs only: the host runs it just before Phase::Ui.
        GameUpdateUi
    };

    /**
     * @brief Frame phase callback signature.
     * @param dt Frame delta time in seconds.
     */
    using PhaseCallback = std::function<void(float dt)>;
} // namespace openeden::sdk
