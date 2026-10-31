#include <components/extension/host_impl.hpp>
#include <components/extension/logging_service.hpp>
#include <openeden/host.hpp>
#include <openeden/logging.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using openeden::extension::HostImpl;
    using openeden::extension::LoggingServiceImpl;
    using openeden::sdk::Logger;
    using openeden::sdk::LoggingService;
    using openeden::sdk::LogLevel;
    using openeden::sdk::LogSink;

    // In-memory test sink for capturing logged messages
    class TestLogSink : public LogSink
    {
    public:
        struct LogEntry
        {
            std::string extensionId;
            LogLevel level = LogLevel::Info;
            std::string formattedMessage;
        };

        void log(std::string_view extensionId, LogLevel level, std::string_view formattedMessage) override
        {
            mEntries.push_back({ std::string(extensionId), level, std::string(formattedMessage) });
        }

        [[nodiscard]] const std::vector<LogEntry>& entries() const noexcept { return mEntries; }
        [[nodiscard]] std::size_t size() const noexcept { return mEntries.size(); }
        void clear() noexcept { mEntries.clear(); }

    private:
        std::vector<LogEntry> mEntries;
    };

    // Deliberately faulty sink that throws an exception during log dispatch
    class ThrowingLogSink : public LogSink
    {
    public:
        void log(std::string_view /*extensionId*/, LogLevel /*level*/, std::string_view /*formattedMessage*/) override
        {
            throw std::runtime_error("Simulated log sink disk write failure");
        }
    };

    TEST(LoggingServiceTest, PerExtensionLoggerCreationAndIsolation)
    {
        LoggingServiceImpl service;

        Logger* loggerMotion = service.getLogger("creativemotion");
        Logger* loggerPhysics = service.getLogger("jolt_physics");

        ASSERT_NE(loggerMotion, nullptr);
        ASSERT_NE(loggerPhysics, nullptr);
        EXPECT_NE(loggerMotion, loggerPhysics);

        EXPECT_EQ(loggerMotion->getExtensionId(), "creativemotion");
        EXPECT_EQ(loggerPhysics->getExtensionId(), "jolt_physics");

        // Repeated requests for the same extension ID return the exact same cached instance
        Logger* cachedMotion = service.getLogger("creativemotion");
        EXPECT_EQ(loggerMotion, cachedMotion);
    }

    TEST(LoggingServiceTest, MessagePrefixingWithExtensionId)
    {
        LoggingServiceImpl service;
        auto sink = std::make_shared<TestLogSink>();
        service.addSink(sink);

        Logger* loggerMotion = service.getLogger("creativemotion");
        Logger* loggerPhysics = service.getLogger("jolt_physics");

        loggerMotion->info("Motion subsystem initialized");
        loggerPhysics->warning("Collision broadphase overflow");

        ASSERT_EQ(sink->size(), 2u);

        const auto& entry0 = sink->entries()[0];
        EXPECT_EQ(entry0.extensionId, "creativemotion");
        EXPECT_EQ(entry0.level, LogLevel::Info);
        EXPECT_EQ(entry0.formattedMessage, "[creativemotion] Motion subsystem initialized");

        const auto& entry1 = sink->entries()[1];
        EXPECT_EQ(entry1.extensionId, "jolt_physics");
        EXPECT_EQ(entry1.level, LogLevel::Warning);
        EXPECT_EQ(entry1.formattedMessage, "[jolt_physics] Collision broadphase overflow");
    }

    TEST(LoggingServiceTest, PerLoggerLevelFiltering)
    {
        LoggingServiceImpl service;
        auto sink = std::make_shared<TestLogSink>();
        service.addSink(sink);

        Logger* logger = service.getLogger("test_extension");

        // Filter out anything below Warning
        logger->setLogLevel(LogLevel::Warning);
        EXPECT_EQ(logger->getLogLevel(), LogLevel::Warning);

        logger->trace("Trace message (should be suppressed)");
        logger->debug("Debug message (should be suppressed)");
        logger->info("Info message (should be suppressed)");

        EXPECT_EQ(sink->size(), 0u);

        logger->warning("Warning message (should pass)");
        logger->error("Error message (should pass)");
        logger->fatal("Fatal message (should pass)");

        ASSERT_EQ(sink->size(), 3u);
        EXPECT_EQ(sink->entries()[0].level, LogLevel::Warning);
        EXPECT_EQ(sink->entries()[1].level, LogLevel::Error);
        EXPECT_EQ(sink->entries()[2].level, LogLevel::Fatal);

        // Lower threshold to Debug
        sink->clear();
        logger->setLogLevel(LogLevel::Debug);

        logger->trace("Trace suppressed");
        logger->debug("Debug passes");
        logger->info("Info passes");

        ASSERT_EQ(sink->size(), 2u);
        EXPECT_EQ(sink->entries()[0].level, LogLevel::Debug);
        EXPECT_EQ(sink->entries()[1].level, LogLevel::Info);
    }

    TEST(LoggingServiceTest, GlobalLogLevelFiltering)
    {
        LoggingServiceImpl service;
        auto sink = std::make_shared<TestLogSink>();
        service.addSink(sink);

        service.setGlobalLogLevel(LogLevel::Error);
        EXPECT_EQ(service.getGlobalLogLevel(), LogLevel::Error);

        Logger* logger = service.getLogger("unconfigured_extension");

        logger->info("Info message under high global threshold");
        logger->warning("Warning message under high global threshold");
        EXPECT_EQ(sink->size(), 0u);

        logger->error("Error message passing global threshold");
        logger->fatal("Fatal message passing global threshold");

        ASSERT_EQ(sink->size(), 2u);
        EXPECT_EQ(sink->entries()[0].level, LogLevel::Error);
        EXPECT_EQ(sink->entries()[1].level, LogLevel::Fatal);
    }

    TEST(LoggingServiceTest, AllConvenienceLogLevelMethods)
    {
        LoggingServiceImpl service;
        service.setGlobalLogLevel(LogLevel::Trace);
        auto sink = std::make_shared<TestLogSink>();
        service.addSink(sink);

        Logger* logger = service.getLogger("convenience_extension");
        logger->setLogLevel(LogLevel::Trace);

        logger->trace("T");
        logger->debug("D");
        logger->info("I");
        logger->warning("W");
        logger->error("E");
        logger->fatal("F");
        logger->log(LogLevel::Info, "ExplicitLog");

        ASSERT_EQ(sink->size(), 7u);
        EXPECT_EQ(sink->entries()[0].level, LogLevel::Trace);
        EXPECT_EQ(sink->entries()[1].level, LogLevel::Debug);
        EXPECT_EQ(sink->entries()[2].level, LogLevel::Info);
        EXPECT_EQ(sink->entries()[3].level, LogLevel::Warning);
        EXPECT_EQ(sink->entries()[4].level, LogLevel::Error);
        EXPECT_EQ(sink->entries()[5].level, LogLevel::Fatal);
        EXPECT_EQ(sink->entries()[6].level, LogLevel::Info);
        EXPECT_EQ(sink->entries()[6].formattedMessage, "[convenience_extension] ExplicitLog");
    }

    TEST(LoggingServiceTest, MultipleSinkDelegationAndRemoval)
    {
        LoggingServiceImpl service;
        auto sinkA = std::make_shared<TestLogSink>();
        auto sinkB = std::make_shared<TestLogSink>();

        service.addSink(sinkA);
        service.addSink(sinkB);

        Logger* logger = service.getLogger("multi_sink_extension");
        logger->info("Broadcast message");

        EXPECT_EQ(sinkA->size(), 1u);
        EXPECT_EQ(sinkB->size(), 1u);

        service.removeSink(sinkA);

        logger->info("Message for B only");
        EXPECT_EQ(sinkA->size(), 1u);
        EXPECT_EQ(sinkB->size(), 2u);
    }

    TEST(LoggingServiceTest, ExceptionContainmentWhenSinkThrows)
    {
        LoggingServiceImpl service;
        auto throwingSink = std::make_shared<ThrowingLogSink>();
        auto healthySink = std::make_shared<TestLogSink>();

        // Register throwing sink first, then healthy sink
        service.addSink(throwingSink);
        service.addSink(healthySink);

        Logger* logger = service.getLogger("fault_tolerant_extension");

        // Logging must NEVER propagate exceptions to caller
        EXPECT_NO_THROW(logger->error("Testing sink fault isolation"));

        // Healthy sink must still receive the entry despite the previous sink throwing
        ASSERT_EQ(healthySink->size(), 1u);
        EXPECT_EQ(
            healthySink->entries()[0].formattedMessage, "[fault_tolerant_extension] Testing sink fault isolation");
    }

    TEST(LoggingServiceTest, HostImplWiresLoggingService)
    {
        HostImpl host("TestHost");
        LoggingService* loggingService = host.getLoggingService();

        ASSERT_NE(loggingService, nullptr);

        Logger* logger = loggingService->getLogger("host_test_extension");
        ASSERT_NE(logger, nullptr);
        EXPECT_EQ(logger->getExtensionId(), "host_test_extension");
    }
}
