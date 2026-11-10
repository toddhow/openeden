#ifndef GAME_MORROWIND_PACK_H
#define GAME_MORROWIND_PACK_H

#include <memory>

#include <components/extension/manifest.hpp>
#include <openeden/extension.hpp>

namespace game_morrowind
{
    /// The Morrowind game pack as an extension of the game-pack tier (task 0084 leftovers, stage I1b), for the
    /// executable to hand to the extension host as a built-in pack. Chosen by `game=morrowind`: the host then starts it
    /// before the game's managers and windows are built, and it provides openeden.game.rules (see game_rules.hpp).
    /// There is no library and no manifest file: the manifest is made here.
    [[nodiscard]] openeden::extension::ExtensionManifest packManifest();

    /// A new Extension that provides makeGameRules() as the openeden.game.rules contract. The Extension owns the rules
    /// object until it is destroyed (the host destroys it before the game's World goes). Main thread only.
    [[nodiscard]] std::unique_ptr<openeden::sdk::Extension> makePackExtension();
}

#endif
