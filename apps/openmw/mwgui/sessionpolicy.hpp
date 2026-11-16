#ifndef OPENMW_MWGUI_SESSIONPOLICY_H
#define OPENMW_MWGUI_SESSIONPOLICY_H

#include <string>

#include <components/settings/settings.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/extensionmanager.hpp"

#include "sessionpresentation.hpp"

// Task 0215: the session's presentation policy read from the settings ([Game] neutral world, the game pack's id and its
// `menus` and `music` settings). The pure decisions are in sessionpresentation.hpp.

namespace MWGui::SessionPresentation
{
    /// The pack setting of the session's game pack ("extension.<id>"); `fallback` when the setting is not set.
    [[nodiscard]] inline bool packBool(const std::string& packId, const char* key, bool fallback)
    {
        return Settings::Manager::getOrDefault<bool>(key, "extension." + packId, fallback);
    }

    /// The policy of the running session. The game pack id is fixed once the extension host has started and the
    /// settings are read at start-up by the pack too, so the answer is kept after the first time it can be known.
    /// Main thread (the policy is not locked).
    [[nodiscard]] inline const Policy& currentPolicy()
    {
        static Policy policy;
        static bool known = false;
        if (known)
            return policy;
        // the extension manager is set in the engine's constructor, before the first call (windows, sound, overlay)
        const std::string packId = MWBase::Environment::get().getExtensionManager()->gamePackId();
        const bool neutral = Settings::Manager::getOrDefault<bool>("neutral world", "Game", false);
        policy = makePolicy(neutral, packId, packBool(packId, "menus", false), packBool(packId, "music", false));
        // a neutral world whose pack has not started yet (no id): ask again later, nothing changes meanwhile
        known = !neutral || !packId.empty();
        return policy;
    }

    /// The pack setting `ui theme` ("skyrim" unless the user sets another, `neutral` for the Morrowind look).
    [[nodiscard]] inline std::string packThemeSetting(const std::string& packId)
    {
        return Settings::Manager::getOrDefault<std::string>("ui theme", "extension." + packId, std::string());
    }
}

#endif
