#pragma once

#include <string>
#include <string_view>

namespace openeden::sdk
{
    /**
     * @brief Per-extension typed settings namespace service.
     */
    class SettingsService
    {
    public:
        virtual ~SettingsService() = default;

        [[nodiscard]] virtual bool getBool(
            std::string_view extensionId, std::string_view key, bool fallback = false) const = 0;

        [[nodiscard]] virtual int getInt(
            std::string_view extensionId, std::string_view key, int fallback = 0) const = 0;

        [[nodiscard]] virtual float getFloat(
            std::string_view extensionId, std::string_view key, float fallback = 0.0f) const = 0;

        [[nodiscard]] virtual std::string getString(
            std::string_view extensionId, std::string_view key, std::string_view fallback = "") const = 0;

        virtual void setBool(std::string_view extensionId, std::string_view key, bool value) = 0;
        virtual void setInt(std::string_view extensionId, std::string_view key, int value) = 0;
        virtual void setFloat(std::string_view extensionId, std::string_view key, float value) = 0;
        virtual void setString(std::string_view extensionId, std::string_view key, std::string_view value) = 0;

        virtual void resetToDefaults(std::string_view extensionId) = 0;
    };
} // namespace openeden::sdk
