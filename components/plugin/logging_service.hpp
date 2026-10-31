#pragma once

#include <openeden/logging.hpp>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::plugin
{
    /**
     * @brief Engine implementation of the logging service.
     */
    class LoggingServiceImpl : public openeden::sdk::LoggingService
    {
    public:
        LoggingServiceImpl();
        ~LoggingServiceImpl() override = default;

        // Non-copyable
        LoggingServiceImpl(const LoggingServiceImpl&) = delete;
        LoggingServiceImpl& operator=(const LoggingServiceImpl&) = delete;

        // Movable
        LoggingServiceImpl(LoggingServiceImpl&&) noexcept = default;
        LoggingServiceImpl& operator=(LoggingServiceImpl&&) noexcept = default;

        [[nodiscard]] openeden::sdk::Logger* getLogger(std::string_view pluginId) override;

        [[nodiscard]] openeden::sdk::LogLevel getGlobalLogLevel() const noexcept override;
        void setGlobalLogLevel(openeden::sdk::LogLevel level) override;

        void addSink(std::shared_ptr<openeden::sdk::LogSink> sink) override;
        void removeSink(std::shared_ptr<openeden::sdk::LogSink> sink) override;

        /// Drops every object a plugin left here, while its library is still loaded.
        void releasePluginObjects();

        void dispatch(std::string_view pluginId, openeden::sdk::LogLevel level, std::string_view message);

    private:
        class LoggerImpl : public openeden::sdk::Logger
        {
        public:
            LoggerImpl(std::string pluginId, LoggingServiceImpl& service);
            ~LoggerImpl() override = default;

            [[nodiscard]] std::string_view getPluginId() const noexcept override;
            [[nodiscard]] openeden::sdk::LogLevel getLogLevel() const noexcept override;
            void setLogLevel(openeden::sdk::LogLevel level) override;

            void log(openeden::sdk::LogLevel level, std::string_view message) override;

        private:
            std::string mPluginId;
            LoggingServiceImpl& mService;
            std::atomic<openeden::sdk::LogLevel> mLevel{ openeden::sdk::LogLevel::Info };
            std::atomic<bool> mExplicitLevel{ false };
        };

        mutable std::mutex mMutex;
        std::atomic<openeden::sdk::LogLevel> mGlobalLogLevel{ openeden::sdk::LogLevel::Info };
        std::vector<std::shared_ptr<openeden::sdk::LogSink>> mSinks;
        std::map<std::string, std::unique_ptr<LoggerImpl>, std::less<>> mLoggers;
    };
} // namespace openeden::plugin
