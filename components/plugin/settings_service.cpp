#include "settings_service.hpp"

#include <components/debug/debuglog.hpp>
#include <components/settings/settings.hpp>

#include <charconv>
#include <cmath>
#include <limits>
#include <sstream>

namespace openeden::plugin
{
    namespace
    {
        std::string categoryOf(std::string_view pluginId)
        {
            return "plugin." + std::string(pluginId);
        }

        // Replaces value if text reads as the same type. Numbers use the classic locale, whatever the user's is.
        template <typename Variant>
        bool parseAs(const std::string& text, Variant& value)
        {
            if (std::holds_alternative<bool>(value))
            {
                if (text != "true" && text != "false")
                    return false;
                value = text == "true";
            }
            else if (std::holds_alternative<int>(value))
            {
                int parsed = 0;
                const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
                if (error != std::errc() || end != text.data() + text.size())
                    return false;
                value = parsed;
            }
            else if (std::holds_alternative<float>(value))
            {
                std::istringstream stream(text);
                stream.imbue(std::locale::classic());
                float parsed = 0.0f;
                if (!(stream >> parsed) || !stream.eof() || !std::isfinite(parsed))
                    return false;
                value = parsed;
            }
            else
                value = text;
            return true;
        }

        template <typename Variant>
        std::string formatValue(const Variant& value)
        {
            if (const bool* b = std::get_if<bool>(&value))
                return *b ? "true" : "false";
            if (const std::string* s = std::get_if<std::string>(&value))
                return *s;
            std::ostringstream stream;
            stream.imbue(std::locale::classic());
            stream.precision(std::numeric_limits<float>::max_digits10); // Reads back as the same float
            std::visit(
                [&](const auto& v) {
                    if constexpr (!std::is_same_v<std::decay_t<decltype(v)>, std::string>)
                        stream << v;
                },
                value);
            return stream.str();
        }
    }

    void SettingsServiceImpl::storeInEngine(std::string_view pluginId, std::string_view key)
    {
        if (!mUseEngineSettings)
            return;

        const auto valuesIt = mValues.find(pluginId);
        const auto defaultsIt = mDefaults.find(pluginId);
        if (valuesIt == mValues.end())
            return;
        const auto valueIt = valuesIt->second.find(key);
        if (valueIt == valuesIt->second.end())
            return;

        Settings::CategorySetting setting{ categoryOf(pluginId), std::string(key) };
        bool isDefault = false;
        if (defaultsIt != mDefaults.end())
        {
            const auto defaultIt = defaultsIt->second.find(key);
            isDefault = defaultIt != defaultsIt->second.end() && defaultIt->second == valueIt->second;
        }
        if (isDefault)
        {
            Settings::Manager::mUserSettings.erase(setting);
            return;
        }

        // settings.cfg holds one setting per line, and a file it cannot parse stops the game starting
        std::string text = formatValue(valueIt->second);
        if (key.find_first_of("\r\n=") != std::string_view::npos || text.find_first_of("\r\n") != std::string::npos)
        {
            Log(Debug::Warning) << "settings.cfg: [" << setting.first << "] " << key
                                << ": a line break in a key or value cannot be saved; keeping it for this session only";
            return;
        }
        Settings::Manager::mUserSettings[std::move(setting)] = std::move(text);
    }

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

            if (mUseEngineSettings)
            {
                const auto user
                    = Settings::Manager::mUserSettings.find(Settings::CategorySetting{ categoryOf(pid), setting.key });
                if (user != Settings::Manager::mUserSettings.end())
                {
                    SettingValue parsed = val;
                    if (parseAs(user->second, parsed))
                        mValues[pid][setting.key] = std::move(parsed);
                    else
                        Log(Debug::Warning) << "settings.cfg: [" << categoryOf(pid) << "] " << setting.key << ": \""
                                            << user->second << "\" is not a " << setting.type << "; using the default";
                }
            }
        }
    }

    void SettingsServiceImpl::resetToDefaults(std::string_view pluginId)
    {
        auto it = mDefaults.find(pluginId);
        if (it != mDefaults.end())
        {
            mValues[std::string(pluginId)] = it->second;
            for (const auto& [key, value] : it->second)
                storeInEngine(pluginId, key);
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
        storeInEngine(pluginId, key);
    }

    void SettingsServiceImpl::setInt(std::string_view pluginId, std::string_view key, int value)
    {
        mValues[std::string(pluginId)][std::string(key)] = value;
        storeInEngine(pluginId, key);
    }

    void SettingsServiceImpl::setFloat(std::string_view pluginId, std::string_view key, float value)
    {
        mValues[std::string(pluginId)][std::string(key)] = value;
        storeInEngine(pluginId, key);
    }

    void SettingsServiceImpl::setString(std::string_view pluginId, std::string_view key, std::string_view value)
    {
        mValues[std::string(pluginId)][std::string(key)] = std::string(value);
        storeInEngine(pluginId, key);
    }
} // namespace openeden::plugin
