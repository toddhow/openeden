#include "managersettingsstore.hpp"

#include <components/settings/settings.hpp>

namespace CSMModule
{
    std::optional<std::string> ManagerSettingsStore::get(const std::string& section, const std::string& key) const
    {
        const auto found
            = Settings::Manager::mUserSettings.find(std::make_pair(std::string_view(section), std::string_view(key)));
        if (found == Settings::Manager::mUserSettings.end())
            return std::nullopt;
        return found->second;
    }

    void ManagerSettingsStore::set(const std::string& section, const std::string& key, const std::string& value)
    {
        Settings::Manager::setString(key, section, value);
    }
}
