#include "settings_service.hpp"

namespace openeden::plugin
{
    void SettingsServiceImpl::loadManifestDefaults(const PluginManifest& manifest)
    {
        const std::string pid = manifest.id;
        for (const auto& setting : manifest.settings)
        {
            SettingValue val;
            if (setting.type == "bool")
                val = setting.defaultBool;
            else if (setting.type == "int")
                val = setting.defaultInt;
            else if (setting.type == "float" || setting.type == "double")
                val = static_cast<float>(setting.defaultDouble);
            else
                val = setting.defaultString;

            mDefaults[pid][setting.key] = val;
            // Initialize current value if not already set
            if (mValues[pid].find(setting.key) == mValues[pid].end())
            {
                mValues[pid][setting.key] = val;
            }
        }
    }

    void SettingsServiceImpl::resetToDefaults(std::string_view pluginId)
    {
        auto it = mDefaults.find(pluginId);
        if (it != mDefaults.end())
        {
            mValues[std::string(pluginId)] = it->second;
        }
    }

    bool SettingsServiceImpl::getBool(std::string_view pluginId, std::string_view key, bool fallback) const
    {
        auto pit = mValues.find(pluginId);
        if (pit == mValues.end())
            return fallback;

        auto kit = pit->second.find(key);
        if (kit == pit->second.end())
            return fallback;

        if (std::holds_alternative<bool>(kit->second))
            return std::get<bool>(kit->second);

        return fallback;
    }

    int SettingsServiceImpl::getInt(std::string_view pluginId, std::string_view key, int fallback) const
    {
        auto pit = mValues.find(pluginId);
        if (pit == mValues.end())
            return fallback;

        auto kit = pit->second.find(key);
        if (kit == pit->second.end())
            return fallback;

        if (std::holds_alternative<int>(kit->second))
            return std::get<int>(kit->second);

        return fallback;
    }

    float SettingsServiceImpl::getFloat(std::string_view pluginId, std::string_view key, float fallback) const
    {
        auto pit = mValues.find(pluginId);
        if (pit == mValues.end())
            return fallback;

        auto kit = pit->second.find(key);
        if (kit == pit->second.end())
            return fallback;

        if (std::holds_alternative<float>(kit->second))
            return std::get<float>(kit->second);

        return fallback;
    }

    std::string SettingsServiceImpl::getString(
        std::string_view pluginId, std::string_view key, std::string_view fallback) const
    {
        auto pit = mValues.find(pluginId);
        if (pit == mValues.end())
            return std::string(fallback);

        auto kit = pit->second.find(key);
        if (kit == pit->second.end())
            return std::string(fallback);

        if (std::holds_alternative<std::string>(kit->second))
            return std::get<std::string>(kit->second);

        return std::string(fallback);
    }

    void SettingsServiceImpl::setBool(std::string_view pluginId, std::string_view key, bool value)
    {
        mValues[std::string(pluginId)][std::string(key)] = value;
    }

    void SettingsServiceImpl::setInt(std::string_view pluginId, std::string_view key, int value)
    {
        mValues[std::string(pluginId)][std::string(key)] = value;
    }

    void SettingsServiceImpl::setFloat(std::string_view pluginId, std::string_view key, float value)
    {
        mValues[std::string(pluginId)][std::string(key)] = value;
    }

    void SettingsServiceImpl::setString(std::string_view pluginId, std::string_view key, std::string_view value)
    {
        mValues[std::string(pluginId)][std::string(key)] = std::string(value);
    }
} // namespace openeden::plugin
