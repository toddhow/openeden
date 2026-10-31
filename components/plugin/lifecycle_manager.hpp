#pragma once

#include <components/plugin/dependency_resolver.hpp>
#include <components/plugin/dynamic_library.hpp>
#include <components/plugin/manifest.hpp>
#include <components/plugin/service_registry.hpp>
#include <openeden/host.hpp>
#include <openeden/phases.hpp>
#include <openeden/plugin.hpp>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::plugin
{
    /**
     * @brief Coordinates plugin lifecycle: registration, resolution, startup,
     * phase execution, and reverse-order shutdown.
     */
    class LifecycleManager
    {
    public:
        explicit LifecycleManager(openeden::sdk::Host& host);
        ~LifecycleManager();

        // Non-copyable
        LifecycleManager(const LifecycleManager&) = delete;
        LifecycleManager& operator=(const LifecycleManager&) = delete;

        /**
         * @brief Adds an in-process or dynamically loaded plugin with its manifest.
         */
        void addPlugin(PluginManifest manifest, std::unique_ptr<openeden::sdk::Plugin> plugin,
            std::unique_ptr<DynamicLibrary> library = nullptr);

        /**
         * @brief Resolves dependencies, registers services, and starts active plugins in load order.
         * @return True if initialization completed cleanly.
         */
        bool initialize();

        /**
         * @brief Runs a frame phase, calling registered callbacks in load order.
         * Exceptions thrown by plugins are caught at the boundary and isolate the failing plugin.
         */
        void runPhase(openeden::sdk::Phase phase, float dt);

        /**
         * @brief Stops all active plugins in reverse load order. Idempotent.
         */
        void shutdown();

        /**
         * @brief Sets a function shutdown() calls when plugins are stopped and destroyed, just before unloading them.
         */
        void setBeforeUnload(std::function<void()> callback) { mBeforeUnload = std::move(callback); }

        /**
         * @brief Checks if a plugin is currently running and active.
         */
        bool isPluginActive(std::string_view id) const;

        /**
         * @brief Checks if a plugin faulted during execution (e.g. threw an exception).
         */
        bool isPluginFaulted(std::string_view id) const;

        /**
         * @brief Returns the count of currently active plugins.
         */
        size_t getActivePluginCount() const;

        /**
         * @brief Returns the determined load order of plugins.
         */
        const std::vector<std::string>& getLoadOrder() const;

        /**
         * @brief Returns the list of disabled plugins.
         */
        const std::vector<std::string>& getDisabledPlugins() const;

    private:
        void markPluginFaulted(const std::string& pluginId, const char* stage, const char* reason = nullptr);
        void processDeferredFaults();

        openeden::sdk::Host& mHost;
        ServiceRegistry mRegistry;
        std::function<void()> mBeforeUnload;

        struct PluginRecord
        {
            PluginManifest manifest;
            std::unique_ptr<DynamicLibrary> library;
            std::unique_ptr<openeden::sdk::Plugin> instance;
            bool started = false;
        };

        std::map<std::string, PluginRecord> mPlugins;
        std::vector<std::string> mLoadOrder;
        std::vector<std::string> mStartedOrder;
        std::set<std::string> mActivePlugins;
        std::set<std::string> mDisabledPlugins;
        std::set<std::string> mFaultedPlugins;
        std::vector<std::string> mDisabledPluginsList;
        std::vector<std::string> mPendingFaultedPlugins;
        bool mInitialized = false;
    };
} // namespace openeden::plugin
