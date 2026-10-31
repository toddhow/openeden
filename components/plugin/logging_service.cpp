#include "logging_service.hpp"

#include <algorithm>
#include <iostream>

namespace openeden::plugin
{
    LoggingServiceImpl::LoggingServiceImpl() = default;

    LoggingServiceImpl::LoggerImpl::LoggerImpl(std::string pluginId, LoggingServiceImpl& service)
        : mPluginId(std::move(pluginId))
        , mService(service)
    {
    }

    std::string_view LoggingServiceImpl::LoggerImpl::getPluginId() const noexcept
    {
        return mPluginId;
    }

    openeden::sdk::LogLevel LoggingServiceImpl::LoggerImpl::getLogLevel() const noexcept
    {
        return mExplicitLevel.load(std::memory_order_relaxed) ? mLevel.load(std::memory_order_relaxed)
                                                              : mService.getGlobalLogLevel();
    }

    void LoggingServiceImpl::LoggerImpl::setLogLevel(openeden::sdk::LogLevel level)
    {
        mLevel.store(level, std::memory_order_relaxed);
        mExplicitLevel.store(true, std::memory_order_relaxed);
    }

    void LoggingServiceImpl::LoggerImpl::log(openeden::sdk::LogLevel level, std::string_view message)
    {
        const auto activeLevel = getLogLevel();
        if (static_cast<std::uint8_t>(level) < static_cast<std::uint8_t>(activeLevel))
        {
            return;
        }

        mService.dispatch(mPluginId, level, message);
    }

    openeden::sdk::Logger* LoggingServiceImpl::getLogger(std::string_view pluginId)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto it = mLoggers.find(pluginId);
        if (it != mLoggers.end())
        {
            return it->second.get();
        }

        auto logger = std::make_unique<LoggerImpl>(std::string(pluginId), *this);
        auto* rawPtr = logger.get();
        mLoggers.emplace(std::string(pluginId), std::move(logger));
        return rawPtr;
    }

    openeden::sdk::LogLevel LoggingServiceImpl::getGlobalLogLevel() const noexcept
    {
        return mGlobalLogLevel.load(std::memory_order_relaxed);
    }

    void LoggingServiceImpl::setGlobalLogLevel(openeden::sdk::LogLevel level)
    {
        mGlobalLogLevel.store(level, std::memory_order_relaxed);
    }

    void LoggingServiceImpl::addSink(std::shared_ptr<openeden::sdk::LogSink> sink)
    {
        if (!sink)
            return;
        std::lock_guard<std::mutex> lock(mMutex);
        mSinks.push_back(std::move(sink));
    }

    void LoggingServiceImpl::removeSink(std::shared_ptr<openeden::sdk::LogSink> sink)
    {
        if (!sink)
            return;
        std::lock_guard<std::mutex> lock(mMutex);
        mSinks.erase(std::remove(mSinks.begin(), mSinks.end(), sink), mSinks.end());
    }

    void LoggingServiceImpl::releasePluginObjects()
    {
        // Destroyed after unlocking: a sink may log from its destructor
        std::vector<std::shared_ptr<openeden::sdk::LogSink>> sinks;
        std::lock_guard<std::mutex> lock(mMutex);
        sinks.swap(mSinks);
    }

    void LoggingServiceImpl::dispatch(
        std::string_view pluginId, openeden::sdk::LogLevel level, std::string_view message)
    {
        // A sink that logs while receiving a line would call itself without end, so its own line is dropped.
        thread_local bool sDispatching = false;
        if (sDispatching)
            return;
        sDispatching = true;
        struct Reset
        {
            ~Reset() { sDispatching = false; }
        } reset;

        std::vector<std::shared_ptr<openeden::sdk::LogSink>> sinksCopy;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (mSinks.empty())
            {
                return;
            }
            sinksCopy = mSinks;
        }

        const std::string formatted = "[" + std::string(pluginId) + "] " + std::string(message);

        for (const auto& sink : sinksCopy)
        {
            if (sink)
            {
                try
                {
                    sink->log(pluginId, level, formatted);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "OpenEden Logging: Exception in log sink: " << e.what() << '\n';
                }
                catch (...)
                {
                    std::cerr << "OpenEden Logging: Unknown exception in log sink\n";
                }
            }
        }
    }
} // namespace openeden::plugin
