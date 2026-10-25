#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace openeden::sdk
{
    /**
     * @brief Interface representing a Lua table exposed to scripts by a plugin.
     */
    class LuaTable
    {
    public:
        virtual ~LuaTable() = default;

        virtual void setString(std::string_view key, std::string_view value) = 0;
        virtual void setNumber(std::string_view key, double value) = 0;
        virtual void setBool(std::string_view key, bool value) = 0;
        virtual void setFunction(std::string_view key, std::function<int(void*)> fn) = 0;

        [[nodiscard]] virtual std::string getString(std::string_view key) const { return ""; }
        [[nodiscard]] virtual double getNumber(std::string_view key) const { return 0.0; }
        [[nodiscard]] virtual bool getBool(std::string_view key) const { return false; }
        [[nodiscard]] virtual bool hasFunction(std::string_view key) const { return false; }
    };

    /**
     * @brief Context passed to a plugin when registering its Lua module under openeden.<pluginId>.
     */
    class LuaModuleRegistrar
    {
    public:
        virtual ~LuaModuleRegistrar() = default;

        [[nodiscard]] virtual std::string_view getPluginId() const noexcept = 0;
        [[nodiscard]] virtual std::string_view getCanonicalNamespace() const noexcept = 0;
        [[nodiscard]] virtual LuaTable& getTable() = 0;
    };

    using LuaInitCallback = std::function<void(LuaModuleRegistrar& registrar)>;

    /**
     * @brief Service managing per-plugin Lua table registrations.
     */
    class ScriptingService
    {
    public:
        virtual ~ScriptingService() = default;

        virtual void registerModule(std::string_view pluginId, LuaInitCallback callback) = 0;
        virtual void unregisterModule(std::string_view pluginId) = 0;

        [[nodiscard]] virtual bool hasModule(std::string_view pluginId) const = 0;
        [[nodiscard]] virtual std::string getCanonicalNamespace(std::string_view pluginId) const = 0;
    };
} // namespace openeden::sdk
