// Tests for task 0074h (the retry-log-once rule): MWRender::ReadErrorLog remembers which model paths have been
// reported, so that an unreadable file is logged once per session however often the equipment change retries it.
// Written from the task's specification before the implementation and run red against a stub that says "first time"
// every time.

#include "apps/openmw/mwrender/partreaderrorlog.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using MWRender::ReadErrorLog;

    TEST(ReadErrorLogTest, the_first_report_of_a_path_is_true_and_later_ones_false)
    {
        ReadErrorLog log;

        EXPECT_TRUE(log.firstFailure("meshes/a/broken.nif"));
        EXPECT_FALSE(log.firstFailure("meshes/a/broken.nif"));
        EXPECT_FALSE(log.firstFailure("meshes/a/broken.nif"));
        EXPECT_EQ(log.size(), 1u);
    }

    TEST(ReadErrorLogTest, different_paths_are_independent)
    {
        ReadErrorLog log;

        EXPECT_TRUE(log.firstFailure("meshes/a/one.nif"));
        EXPECT_TRUE(log.firstFailure("meshes/a/two.nif"));
        EXPECT_FALSE(log.firstFailure("meshes/a/one.nif"));
        EXPECT_FALSE(log.firstFailure("meshes/a/two.nif"));
        EXPECT_EQ(log.size(), 2u);
    }

    // A record may spell the same file with another case or backslashes.
    TEST(ReadErrorLogTest, spellings_of_one_path_count_as_one)
    {
        ReadErrorLog log;

        EXPECT_TRUE(log.firstFailure("meshes/a/broken.nif"));
        EXPECT_FALSE(log.firstFailure("Meshes\\A\\Broken.NIF"));
        EXPECT_EQ(log.size(), 1u);
    }

    TEST(ReadErrorLogTest, clear_forgets_every_path)
    {
        ReadErrorLog log;
        ASSERT_TRUE(log.firstFailure("meshes/a/one.nif"));

        log.clear();

        EXPECT_EQ(log.size(), 0u);
        EXPECT_TRUE(log.firstFailure("meshes/a/one.nif"));
    }

    TEST(ReadErrorLogTest, a_full_log_counts_a_new_path_as_already_reported)
    {
        ReadErrorLog log;
        for (std::size_t i = 0; i < ReadErrorLog::sCapacity; ++i)
            ASSERT_TRUE(log.firstFailure("meshes/f" + std::to_string(i) + ".nif"));

        EXPECT_FALSE(log.firstFailure("meshes/one_too_many.nif"));
        EXPECT_EQ(log.size(), ReadErrorLog::sCapacity);
    }

    // The part reads run on the update workers and the main thread: many threads reporting the same paths still give
    // each path exactly one true.
    TEST(ReadErrorLogTest, many_threads_reporting_the_same_paths_get_exactly_one_true_each)
    {
        ReadErrorLog log;
        constexpr int paths = 50;
        constexpr int threads = 8;
        std::atomic<int> firsts{ 0 };
        std::vector<std::thread> workers;
        for (int t = 0; t < threads; ++t)
        {
            workers.emplace_back([&] {
                for (int round = 0; round < 3; ++round)
                    for (int i = 0; i < paths; ++i)
                        if (log.firstFailure("meshes/shared" + std::to_string(i) + ".nif"))
                            ++firsts;
            });
        }
        for (std::thread& worker : workers)
            worker.join();

        EXPECT_EQ(firsts.load(), paths);
        EXPECT_EQ(log.size(), static_cast<std::size_t>(paths));
    }

    // The log the game uses is one object.
    TEST(ReadErrorLogTest, the_session_log_is_a_single_object_that_remembers)
    {
        ReadErrorLog& log = ReadErrorLog::instance();
        log.clear();

        EXPECT_TRUE(log.firstFailure("meshes/session/x.nif"));
        EXPECT_FALSE(ReadErrorLog::instance().firstFailure("meshes/session/x.nif"));
        log.clear();
    }
}
