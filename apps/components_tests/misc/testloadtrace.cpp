#include <components/misc/loadtrace.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    // Task 0060: the load tracer is a diagnostic, but it is switched on in the game's frame loop, so its contract
    // (off means nothing recorded, on means one line per scope, safe from many threads) is worth a test.
    // The tracer is one global object, so every test stops it before it ends.
    struct LoadTraceTest : testing::Test
    {
        std::filesystem::path mPath;

        void SetUp() override
        {
            const auto* info = testing::UnitTest::GetInstance()->current_test_info();
            mPath = std::filesystem::temp_directory_path()
                / (std::string("openeden_loadtrace_") + info->name() + ".jsonl");
            std::filesystem::remove(mPath);
        }

        void TearDown() override
        {
            Misc::LoadTrace::stop();
            std::filesystem::remove(mPath);
        }

        std::vector<std::string> readLines() const
        {
            std::ifstream in(mPath, std::ios::binary);
            std::vector<std::string> lines;
            std::string line;
            while (std::getline(in, line))
                lines.push_back(line);
            return lines;
        }
    };

    TEST_F(LoadTraceTest, isOffUntilStarted)
    {
        EXPECT_FALSE(Misc::LoadTrace::enabled());
        {
            Misc::LoadTrace::Scope scope("test.off", 1);
        }
        Misc::LoadTrace::frameEnd(0, 0, 1);
        // Stopping something never started writes nothing.
        Misc::LoadTrace::stop();
        EXPECT_FALSE(std::filesystem::exists(mPath));
    }

    TEST_F(LoadTraceTest, writesOneLinePerScopeAndFrame)
    {
        Misc::LoadTrace::start(mPath.string());
        EXPECT_TRUE(Misc::LoadTrace::enabled());
        {
            Misc::LoadTrace::Scope outer("test.outer", 7);
            {
                Misc::LoadTrace::Scope inner("test.inner");
            }
        }
        Misc::LoadTrace::frameEnd(42, 100, 250);
        Misc::LoadTrace::stop();
        EXPECT_FALSE(Misc::LoadTrace::enabled());

        const std::vector<std::string> lines = readLines();
        ASSERT_EQ(lines.size(), 3u);
        // The inner scope ends first, so it is written first.
        EXPECT_NE(lines[0].find("\"n\":\"test.inner\""), std::string::npos);
        EXPECT_NE(lines[1].find("\"n\":\"test.outer\""), std::string::npos);
        EXPECT_NE(lines[1].find("\"v\":7"), std::string::npos);
        EXPECT_NE(lines[1].find("\"t\":"), std::string::npos);
        EXPECT_EQ(lines[2], "{\"f\":42,\"b\":100,\"e\":250}");
    }

    TEST_F(LoadTraceTest, endFinishesEarlyAndOnlyOnce)
    {
        Misc::LoadTrace::start(mPath.string());
        {
            Misc::LoadTrace::Scope scope("test.early");
            scope.end();
            scope.end();
        }
        Misc::LoadTrace::stop();
        EXPECT_EQ(readLines().size(), 1u);
    }

    TEST_F(LoadTraceTest, setValueIsWritten)
    {
        Misc::LoadTrace::start(mPath.string());
        {
            Misc::LoadTrace::Scope scope("test.value");
            scope.setValue(123);
        }
        Misc::LoadTrace::stop();
        const std::vector<std::string> lines = readLines();
        ASSERT_EQ(lines.size(), 1u);
        EXPECT_NE(lines[0].find("\"v\":123"), std::string::npos);
    }

    TEST_F(LoadTraceTest, scopeTimesAreOrdered)
    {
        Misc::LoadTrace::start(mPath.string());
        const std::int64_t before = Misc::LoadTrace::nowMicroseconds();
        {
            Misc::LoadTrace::Scope scope("test.time");
        }
        const std::int64_t after = Misc::LoadTrace::nowMicroseconds();
        Misc::LoadTrace::stop();
        const std::vector<std::string> lines = readLines();
        ASSERT_EQ(lines.size(), 1u);
        const auto numberAfter = [](const std::string& line, const std::string& key) {
            const std::size_t at = line.find(key);
            EXPECT_NE(at, std::string::npos);
            return static_cast<std::int64_t>(std::stoll(line.substr(at + key.size())));
        };
        const std::int64_t begin = numberAfter(lines[0], "\"b\":");
        const std::int64_t end = numberAfter(lines[0], "\"e\":");
        EXPECT_LE(before, begin);
        EXPECT_LE(begin, end);
        EXPECT_LE(end, after);
    }

    TEST_F(LoadTraceTest, manyThreadsLoseNothingAndGetOwnNumbers)
    {
        Misc::LoadTrace::start(mPath.string());
        constexpr int threadCount = 4;
        constexpr int perThread = 500;
        std::vector<std::thread> threads;
        for (int i = 0; i < threadCount; ++i)
        {
            threads.emplace_back([] {
                for (int n = 0; n < perThread; ++n)
                {
                    Misc::LoadTrace::Scope scope("test.thread", n);
                }
            });
        }
        for (std::thread& thread : threads)
            thread.join();
        Misc::LoadTrace::stop();

        const std::vector<std::string> lines = readLines();
        EXPECT_EQ(lines.size(), static_cast<std::size_t>(threadCount * perThread));
        // Four different thread numbers (the test's own thread, number 0, recorded nothing here).
        std::vector<std::string> numbers;
        for (const std::string& line : lines)
        {
            const std::size_t at = line.find("\"t\":");
            ASSERT_NE(at, std::string::npos);
            const std::string number = line.substr(at + 4);
            bool known = false;
            for (const std::string& seen : numbers)
                known = known || seen == number;
            if (!known)
                numbers.push_back(number);
        }
        EXPECT_EQ(numbers.size(), static_cast<std::size_t>(threadCount));
    }
}
