#pragma once

#include <components/extension/dependency_resolver.hpp>
#include <components/extension/dynamic_library.hpp>
#include <components/extension/manifest.hpp>
#include <components/extension/service_registry.hpp>
#include <openeden/extension.hpp>
#include <openeden/host.hpp>
#include <openeden/phases.hpp>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::extension
{
    /**
     * @brief Coordinates extension lifecycle: registration, resolution, startup,
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
         * @brief Adds an in-process or dynamically loaded extension with its manifest.
         */
        void addExtension(ExtensionManifest manifest, std::unique_ptr<openeden::sdk::Extension> extension,
            std::unique_ptr<DynamicLibrary> library = nullptr);

        /**
         * @brief Resolves dependencies, registers services, and starts active extensions in load order.
         * @return True if initialization completed cleanly.
         */
        bool initialize();

        /**
         * @brief Runs a frame phase, calling registered callbacks in load order.
         * Exceptions thrown by extensions are caught at the boundary and isolate the failing extension.
         */
        void runPhase(openeden::sdk::Phase phase, float dt);

        /**
         * @brief Stops all active extensions in reverse load order. Idempotent.
         */
        void shutdown();

        /**
         * @brief Sets a function shutdown() calls when extensions are stopped and destroyed, just before unloading
         * them.
         */
        void setBeforeUnload(std::function<void()> callback) { mBeforeUnload = std::move(callback); }

        /**
         * @brief Checks if an extension is currently running and active.
         */
        bool isExtensionActive(std::string_view id) const;

        /**
         * @brief Checks if an extension faulted during execution (e.g. threw an exception).
         */
        bool isExtensionFaulted(std::string_view id) const;

        /**
         * @brief Returns the count of currently active extensions.
         */
        size_t getActiveExtensionCount() const;

        /**
         * @brief Returns the determined load order of extensions.
         */
        const std::vector<std::string>& getLoadOrder() const;

        /**
         * @brief Why each failed extension failed, in the order they failed. An extension disabled because an extension
         * it needs failed has an entry of its own.
         */
        struct Failure
        {
            std::string extensionId;
            std::string stage;
            std::string reason;
        };
        const std::vector<Failure>& getFailures() const { return mFailures; }

        /**
         * @brief Returns the list of disabled extensions.
         */
        const std::vector<std::string>& getDisabledExtensions() const;

    private:
        void markExtensionFaulted(const std::string& extensionId, const char* stage, const char* reason = nullptr);
        void processDeferredFaults();

        openeden::sdk::Host& mHost;
        ServiceRegistry mRegistry;
        std::function<void()> mBeforeUnload;
        std::vector<Failure> mFailures;

        struct ExtensionRecord
        {
            ExtensionManifest manifest;
            std::unique_ptr<DynamicLibrary> library;
            std::unique_ptr<openeden::sdk::Extension> instance;
            bool started = false;
        };

        std::map<std::string, ExtensionRecord> mExtensions;
        std::vector<std::string> mLoadOrder;
        std::vector<std::string> mStartedOrder;
        std::set<std::string> mActiveExtensions;
        std::set<std::string> mDisabledExtensions;
        std::set<std::string> mFaultedExtensions;
        std::vector<std::string> mDisabledExtensionsList;
        std::vector<std::string> mPendingFaultedExtensions;
        bool mInitialized = false;
    };
} // namespace openeden::extension
