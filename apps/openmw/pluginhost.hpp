#ifndef OPENEDEN_APPS_OPENMW_PLUGINHOST_H
#define OPENEDEN_APPS_OPENMW_PLUGINHOST_H

#include <components/plugin/host_impl.hpp>
#include <components/plugin/lifecycle_manager.hpp>

#include <openeden/phases.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace OMW
{
    /**
     * @brief Runs plugins inside the game (task 0015). Owned by OMW::Engine, used on the main thread only.
     *
     * The constructor finds and loads the enabled plugins' libraries, without running any of their code beyond the
     * handshake. start() registers, resolves and starts them; stop() stops, destroys and unloads them. A plugin that
     * fails at any point is logged with its stage and reason and disabled with its dependants, and the game goes on.
     * With no plugin loaded, every call here does nothing.
     */
    class PluginHost
    {
    public:
        struct Config
        {
            /// Searched in this order; a plugin found in an earlier directory wins over a later one with the same ID.
            std::vector<std::filesystem::path> searchDirs;
            /// Plugin IDs to load. Empty means none: unlike filterEnabledPlugins, nothing loads by default.
            std::vector<std::string> enabled;
        };

        explicit PluginHost(const Config& config);
        ~PluginHost();

        PluginHost(const PluginHost&) = delete;
        PluginHost& operator=(const PluginHost&) = delete;

        void start();

        /// Calling it again, or before start(), is safe.
        void stop();

        /// No allocation: this is called several times per frame.
        void runPhase(openeden::sdk::Phase phase, float dt)
        {
            if (mRunning)
                mLifecycle.runPhase(phase, dt);
        }

        [[nodiscard]] bool hasPlugins() const noexcept { return mLoaded; }

        [[nodiscard]] openeden::plugin::HostImpl& host() noexcept { return mHost; }
        [[nodiscard]] openeden::plugin::LifecycleManager& lifecycle() noexcept { return mLifecycle; }

    private:
        struct LogBridge;

        openeden::plugin::HostImpl mHost;
        openeden::plugin::LifecycleManager mLifecycle;
        std::shared_ptr<LogBridge> mLogBridge;
        bool mLoaded = false;
        bool mRunning = false;
    };
}

#endif
