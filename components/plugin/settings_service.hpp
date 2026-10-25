#pragma once

#include <components/plugin/manifest.hpp>
#include <openeden/settings.hpp>

#include <map>
#include <string>
#include <string_view>
#include <variant>

namespace openeden::plugin
{
    /**
     * @brief Engine implementation of per-plugin isolated settings namespace service.
     */
    class SettingsServiceImpl : public openeden::sdk::SettingsService
    {
    public:
        SettingsServiceImpl() = default;
        ~SettingsServiceImpl() override = default;

        // Non-copyable
        SettingsServiceImpl(const SettingsServiceImpl&) = delete;
        SettingsServiceImpl& operator=(const SettingsServiceImpl&) = delete;

        // Movable
        SettingsServiceImpl(SettingsServiceImpl&&) noexcept = default;
        SettingsServiceImpl& operator=(SettingsServiceImpl&&) noexcept = default;

        [[nodiscard]] bool getBool(
            std::string_view pluginId, std::string_view key, bool fallback = false) const override;

        [[nodiscard]] int getInt(std::string_view pluginId, std::string_view key, int fallback = 0) const override;

        [[nodiscard]] float getFloat(
            std::string_view pluginId, std::string_view key, float fallback = 0.0f) const override;

        [[nodiscard]] std::string getString(
            std::string_view pluginId, std::string_view key, std::string_view fallback = "") const override;

        void setBool(std::string_view pluginId, std::string_view key, bool value) override;
        void setInt(std::string_view pluginId, std::string_view key, int value) override;
        void setFloat(std::string_view pluginId, std::string_view key, float value) override;
        void setString(std::string_view pluginId, std::string_view key, std::string_view value) override;

        void resetToDefaults(std::string_view pluginId) override;

        /**
         * @brief Loads default settings from a plugin manifest.
         */
        void loadManifestDefaults(const PluginManifest& manifest);

    private:
        using SettingValue = std::variant<bool, int, float, std::string>;
        using InnerMap = std::map<std::string, SettingValue, std::less<>>;

        // pluginId -> (key -> value) with transparent comparators
        std::map<std::string, InnerMap, std::less<>> mValues;
        std::map<std::string, InnerMap, std::less<>> mDefaults;
    };
} // namespace openeden::plugin
