#pragma once

#include <openeden/scripting.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::plugin
{
    /**
     * @brief Engine implementation of per-plugin Lua scripting registry.
     */
    class ScriptingServiceImpl : public openeden::sdk::ScriptingService
    {
    public:
        ScriptingServiceImpl() = default;
        ~ScriptingServiceImpl() override = default;

        // Non-copyable
        ScriptingServiceImpl(const ScriptingServiceImpl&) = delete;
        ScriptingServiceImpl& operator=(const ScriptingServiceImpl&) = delete;

        // Movable
        ScriptingServiceImpl(ScriptingServiceImpl&&) noexcept = default;
        ScriptingServiceImpl& operator=(ScriptingServiceImpl&&) noexcept = default;

        void registerModule(std::string_view pluginId, openeden::sdk::LuaInitCallback callback) override;
        void unregisterModule(std::string_view pluginId) override;

        [[nodiscard]] bool hasModule(std::string_view pluginId) const override;
        [[nodiscard]] std::string getCanonicalNamespace(std::string_view pluginId) const override;

        /**
         * @brief Sets the deterministic load order for module initialization.
         */
        void setPluginLoadOrder(const std::vector<std::string>& loadOrder);

        /**
         * @brief Executes module registration callbacks in load order, initializing per-plugin tables.
         */
        void initializeModules();

        [[nodiscard]] const openeden::sdk::LuaTable* getModuleTable(std::string_view pluginId) const;

    private:
        /**
         * @brief Internal table representation capturing registered plugin bindings.
         * Note: LuaTable operations are intended for the main thread only, in alignment
         * with OpenMW's Lua scripting model.
         */
        class TableImpl : public openeden::sdk::LuaTable
        {
        public:
            void setString(std::string_view key, std::string_view value) override;
            void setNumber(std::string_view key, double value) override;
            void setBool(std::string_view key, bool value) override;
            void setFunction(std::string_view key, std::function<int(void*)> fn) override;

            [[nodiscard]] std::string getString(std::string_view key) const override;
            [[nodiscard]] double getNumber(std::string_view key) const override;
            [[nodiscard]] bool getBool(std::string_view key) const override;
            [[nodiscard]] bool hasFunction(std::string_view key) const override;

        private:
            std::map<std::string, std::string, std::less<>> mStrings;
            std::map<std::string, double, std::less<>> mNumbers;
            std::map<std::string, bool, std::less<>> mBools;
            std::map<std::string, std::function<int(void*)>, std::less<>> mFunctions;
        };

        class RegistrarImpl : public openeden::sdk::LuaModuleRegistrar
        {
        public:
            RegistrarImpl(std::string pluginId, std::string canonicalNamespace, TableImpl& table)
                : mPluginId(std::move(pluginId))
                , mCanonicalNamespace(std::move(canonicalNamespace))
                , mTable(table)
            {
            }

            [[nodiscard]] std::string_view getPluginId() const noexcept override { return mPluginId; }
            [[nodiscard]] std::string_view getCanonicalNamespace() const noexcept override
            {
                return mCanonicalNamespace;
            }
            [[nodiscard]] openeden::sdk::LuaTable& getTable() override { return mTable; }

        private:
            std::string mPluginId;
            std::string mCanonicalNamespace;
            TableImpl& mTable;
        };

        std::vector<std::string> mLoadOrder;
        std::map<std::string, openeden::sdk::LuaInitCallback, std::less<>> mModules;
        std::map<std::string, std::unique_ptr<TableImpl>, std::less<>> mTables;
    };
} // namespace openeden::plugin
