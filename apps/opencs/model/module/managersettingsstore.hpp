#ifndef CSM_MODULE_MANAGERSETTINGSSTORE_H
#define CSM_MODULE_MANAGERSETTINGSSTORE_H

#include "modulesettings.hpp"

namespace CSMModule
{
    /// The editor modules' settings in the user settings that eden-studio.cfg holds (Settings::Manager), one section
    /// per module: `[module.<id>]` (task 0091 stage e, matching `[extension.<id>]`). Nothing is written to the file
    /// until CSMPrefs::State::save(), which the preferences window and the editor's start-up call.
    class ManagerSettingsStore final : public SettingsStore
    {
    public:
        std::optional<std::string> get(const std::string& section, const std::string& key) const override;
        void set(const std::string& section, const std::string& key, const std::string& value) override;
    };
}

#endif
