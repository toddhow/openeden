#ifndef OPENEDEN_GAME_SKYRIMSE_GAME_PACK_H
#define OPENEDEN_GAME_SKYRIMSE_GAME_PACK_H

#include <openeden/extension.hpp>

#include <memory>

namespace game_skyrimse
{
    /// The Skyrim SE game pack: registers the content probe, then refuses the session with the null rules' message.
    [[nodiscard]] std::unique_ptr<openeden::sdk::Extension> createSkyrimGamePack();
}

#endif
