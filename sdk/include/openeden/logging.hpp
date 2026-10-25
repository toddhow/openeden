#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

namespace openeden::sdk
{
    /**
     * @brief Severity levels for plugin logging.
     */
    enum class LogLevel : std::uint8_t
    {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warning = 3,
        Error = 4,
        Fatal = 5
    };

    /**
     * @brief Destination sink receiving formatted log messages.
     */
    class LogSink
    {
    public:
        virtual ~LogSink() = default;

        /**
         * @brief Dispatches a formatted log message.
         */
        virtual void log(std::string_view pluginId, LogLevel level, std::string_view formattedMessage) = 0;
    };

    /**
     * @brief Per-plugin isolated logger.
     */
    class Logger
    {
    public:
        virtual ~Logger() = default;

        [[nodiscard]] virtual std::string_view getPluginId() const noexcept = 0;
        [[nodiscard]] virtual LogLevel getLogLevel() const noexcept = 0;
        virtual void setLogLevel(LogLevel level) = 0;

        virtual void log(LogLevel level, std::string_view message) = 0;

        void trace(std::string_view message) { log(LogLevel::Trace, message); }
        void debug(std::string_view message) { log(LogLevel::Debug, message); }
        void info(std::string_view message) { log(LogLevel::Info, message); }
        void warning(std::string_view message) { log(LogLevel::Warning, message); }
        void error(std::string_view message) { log(LogLevel::Error, message); }
        void fatal(std::string_view message) { log(LogLevel::Fatal, message); }
    };

    /**
     * @brief Core logging service providing isolated loggers and sink delegation.
     */
    class LoggingService
    {
    public:
        virtual ~LoggingService() = default;

        [[nodiscard]] virtual Logger* getLogger(std::string_view pluginId) = 0;

        [[nodiscard]] virtual LogLevel getGlobalLogLevel() const noexcept = 0;
        virtual void setGlobalLogLevel(LogLevel level) = 0;

        virtual void addSink(std::shared_ptr<LogSink> sink) = 0;
        virtual void removeSink(std::shared_ptr<LogSink> sink) = 0;
    };
} // namespace openeden::sdk
