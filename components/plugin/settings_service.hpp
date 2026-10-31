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

        /**
         * @brief Keeps values in Settings::Manager under category "plugin.<id>", as the game's settings.cfg.
         *
         * Call it before loadManifestDefaults, which then applies the user's values: one of the wrong type is logged
         * and the default used. A value equal to its default is removed from the user's settings. Values are cached
         * here, so reading one does not touch Settings::Manager. Off in a host that does not call this.
         */
        void useEngineSettings() { mUseEngineSettings = true; }

    private:
        void storeInEngine(std::string_view pluginId, std::string_view key);

        using SettingValue = std::variant<bool, int, float, std::string>;
        using InnerMap = std::map<std::string, SettingValue, std::less<>>;

        // pluginId -> (key -> value) with transparent comparators
        std::map<std::string, InnerMap, std::less<>> mValues;
        std::map<std::string, InnerMap, std::less<>> mDefaults;
        bool mUseEngineSettings = false;
    };
} // namespace openeden::plugin
