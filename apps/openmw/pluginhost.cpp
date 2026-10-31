#include "pluginhost.hpp"

#include <algorithm>
#include <mutex>
#include <set>
#include <string_view>

#include <components/debug/debugging.hpp>
#include <components/debug/debuglog.hpp>
#include <components/plugin/plugin_loader.hpp>

#include <openeden/logging.hpp>

namespace OMW
{
    namespace
    {
        constexpr std::string_view kEngineLogSource = "engine";

        // True on a thread while a line crosses between the plugin and engine logs, in either direction. The engine
        // listener runs under Log's lock, so a plugin sink that logs while receiving an engine line must not reach Log
        // again: that line is dropped. And a plugin's line written to the engine log must not come back to the sinks.
        thread_local bool sCrossingLogs = false;

        struct CrossingLogs
        {
            CrossingLogs() { sCrossingLogs = true; }
            ~CrossingLogs() { sCrossingLogs = false; }
        };

        Debug::Level toDebugLevel(openeden::sdk::LogLevel level)
        {
            switch (level)
            {
                case openeden::sdk::LogLevel::Trace:
                    return Debug::Debug; // The engine's most detailed level, above Verbose
                case openeden::sdk::LogLevel::Debug:
                    return Debug::Verbose;
                case openeden::sdk::LogLevel::Info:
                    return Debug::Info;
                case openeden::sdk::LogLevel::Warning:
                    return Debug::Warning;
                case openeden::sdk::LogLevel::Error:
                case openeden::sdk::LogLevel::Fatal:
                    return Debug::Error;
            }
            return Debug::Info;
        }

        openeden::sdk::LogLevel toPluginLevel(Debug::Level level)
        {
            switch (level)
            {
                case Debug::Error:
                    return openeden::sdk::LogLevel::Error;
                case Debug::Warning:
                    return openeden::sdk::LogLevel::Warning;
                case Debug::Info:
                    return openeden::sdk::LogLevel::Info;
                case Debug::Verbose:
                    return openeden::sdk::LogLevel::Debug;
                default:
                    return openeden::sdk::LogLevel::Trace;
            }
        }

        // Plugin log lines to the engine log. formattedMessage already has the [pluginId] prefix.
        class EngineLogSink final : public openeden::sdk::LogSink
        {
        public:
            void log(std::string_view pluginId, openeden::sdk::LogLevel level, std::string_view formattedMessage) override
            {
                if (pluginId == kEngineLogSource || sCrossingLogs)
                    return;
                const CrossingLogs crossing;
                Log(toDebugLevel(level)) << formattedMessage;
            }
        };
    }

    // Engine log lines to the plugins' sinks. Debug's listener can run on any thread and outlives this host, so it
    // holds this object, not the host. The link is cut under the lock before plugin libraries unload, which waits for
    // a line in flight. Nothing under the lock reaches Log, because sCrossingLogs stops it, so the lock order is safe.
    struct PluginHost::LogBridge
    {
        std::mutex mMutex;
        openeden::plugin::LoggingServiceImpl* mService = nullptr;

        void forward(Debug::Level level, std::string_view message)
        {
            if (sCrossingLogs)
                return;
            while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
                message.remove_suffix(1);
            const CrossingLogs crossing;
            const std::lock_guard lock(mMutex);
            if (mService != nullptr)
                mService->dispatch(kEngineLogSource, toPluginLevel(level), message);
        }
    };

    PluginHost::PluginHost(const Config& config)
        : mLifecycle(mHost)
    {
        namespace plugin = openeden::plugin;

        std::vector<plugin::PluginLoadError> errors;
        const std::vector<plugin::DiscoveredPlugin> discovered
            = plugin::discoverPluginDirectories(config.searchDirs, &errors);

        const auto isEnabled = [&](const std::string& id) {
            return std::find(config.enabled.begin(), config.enabled.end(), id) != config.enabled.end();
        };

        // A broken manifest has no reliable ID to check against the enable list, so it is always reported
        for (const plugin::PluginLoadError& error : errors)
            Log(Debug::Error) << "Plugin '" << error.pluginId << "' failed at " << error.stage << ": " << error.message;

        std::set<std::string> seen;
        for (const plugin::DiscoveredPlugin& item : discovered)
        {
            const std::string& id = item.manifest.id;
            if (!seen.insert(id).second)
            {
                Log(Debug::Warning) << "Plugin '" << id << "' in " << item.directory
                                    << " ignored: found earlier in another plugin directory";
                continue;
            }
            if (!isEnabled(id))
            {
                Log(Debug::Info) << "Plugin '" << id << "' is installed but not enabled";
                continue;
            }

            Log(Debug::Info) << "Loading plugin '" << id << "' from " << item.directory;
            plugin::PluginLoadResult result = plugin::loadPluginLibrary(item, mHost.getHostInfo());
            if (!result.success)
            {
                Log(Debug::Error) << "Plugin '" << id << "' failed at " << result.error.stage << ": "
                                  << result.error.message;
                continue;
            }
            mLifecycle.addPlugin(
                std::move(result.plugin.manifest), std::move(result.plugin.instance), std::move(result.plugin.library));
            mLoaded = true;
        }

        for (const std::string& id : config.enabled)
        {
            if (seen.find(id) == seen.end())
                Log(Debug::Warning) << "Plugin '" << id << "' is enabled but not installed";
        }
    }

    PluginHost::~PluginHost()
    {
        stop();
    }

    void PluginHost::start()
    {
        if (!mLoaded || mRunning)
            return;

        mLogBridge = std::make_shared<LogBridge>();
        mLogBridge->mService = &mHost.loggingService();
        mHost.loggingService().addSink(std::make_shared<EngineLogSink>());
        Debug::setExtraLogListener([bridge = mLogBridge](Debug::Level level, std::string_view, std::string_view message) {
            bridge->forward(level, message);
        });

        mLifecycle.setBeforeUnload([bridge = mLogBridge] {
            const std::lock_guard lock(bridge->mMutex);
            bridge->mService = nullptr;
        });

        mRunning = true;
        mLifecycle.initialize();

        for (const std::string& id : mLifecycle.getLoadOrder())
        {
            if (mLifecycle.isPluginActive(id))
                Log(Debug::Info) << "Plugin '" << id << "' started";
        }
        for (const std::string& id : mLifecycle.getDisabledPlugins())
            Log(Debug::Error) << "Plugin '" << id << "' is disabled";
    }

    void PluginHost::stop()
    {
        if (!mLoaded)
            return;
        mLoaded = false;
        mRunning = false;

        Log(Debug::Info) << "Stopping plugins";
        mLifecycle.shutdown();
    }
}
