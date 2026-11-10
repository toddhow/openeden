// Task 0017 stage 5 (W3 leftover): the profiler's per-frame snapshot and the text the debug window shows.
//
// In plain words: ProfilerServiceImpl::snapshot copies the scopes timed since the last reset into a fixed table, before
// the engine resets them at the end of the frame; formatProfilerReport turns that table into the text of the
// "Extensions" tab. Both must keep to their fixed sizes.
#include <components/extension/profiler_service.hpp>
#include <components/extension/profiler_snapshot.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string>

namespace
{
    using openeden::extension::formatProfilerReport;
    using openeden::extension::ProfilerServiceImpl;
    using openeden::extension::ProfilerSnapshot;

    std::string format(const ProfilerSnapshot& snapshot, bool escape = false, std::size_t size = 4096)
    {
        std::string buffer(size, '\x7f');
        const std::size_t length = formatProfilerReport(snapshot, buffer, escape);
        EXPECT_LT(length, size);
        EXPECT_EQ(buffer[length], '\0');
        return buffer.substr(0, length);
    }

    TEST(ProfilerSnapshotTest, StartsEmptyAndAnEmptyServiceGivesNoRows)
    {
        ProfilerServiceImpl service;
        ProfilerSnapshot snapshot;
        snapshot.count = 7;
        snapshot.dropped = 9;
        service.snapshot(snapshot);
        EXPECT_EQ(snapshot.count, 0u);
        EXPECT_EQ(snapshot.dropped, 0u);
    }

    TEST(ProfilerSnapshotTest, CopiesTheTimedScopesInTheOrderTheyWereFirstUsed)
    {
        ProfilerServiceImpl service;
        service.recordSample("alpha", "update", 1.5);
        service.recordSample("beta", "draw", 2.0);
        service.recordSample("alpha", "update", 0.5);
        service.recordSample("alpha", "ai", 3.0);

        ProfilerSnapshot snapshot;
        service.snapshot(snapshot);
        ASSERT_EQ(snapshot.count, 3u);
        EXPECT_STREQ(snapshot.rows[0].extensionId, "alpha");
        EXPECT_STREQ(snapshot.rows[0].scopeName, "update");
        EXPECT_EQ(snapshot.rows[0].stats.callCount, 2u);
        EXPECT_DOUBLE_EQ(snapshot.rows[0].stats.totalTimeMs, 2.0);
        EXPECT_DOUBLE_EQ(snapshot.rows[0].stats.maxTimeMs, 1.5);
        EXPECT_STREQ(snapshot.rows[1].extensionId, "beta");
        EXPECT_STREQ(snapshot.rows[2].scopeName, "ai");
        EXPECT_EQ(snapshot.dropped, 0u);
    }

    TEST(ProfilerSnapshotTest, ScopesResetByTheFrameEndAreNotListed)
    {
        ProfilerServiceImpl service;
        service.recordSample("alpha", "update", 1.0);
        service.recordSample("alpha", "old", 1.0);
        service.resetFrame();
        service.recordSample("alpha", "update", 4.0);

        ProfilerSnapshot snapshot;
        service.snapshot(snapshot);
        ASSERT_EQ(snapshot.count, 1u);
        EXPECT_STREQ(snapshot.rows[0].scopeName, "update");
        EXPECT_DOUBLE_EQ(snapshot.rows[0].stats.totalTimeMs, 4.0);
    }

    TEST(ProfilerSnapshotTest, RowsPastTheCapAreCountedNotStored)
    {
        ProfilerServiceImpl service;
        const std::size_t total = ProfilerSnapshot::MaxRows + 25;
        for (std::size_t i = 0; i < total; ++i)
            service.recordSample("alpha", "scope" + std::to_string(i), 1.0);

        ProfilerSnapshot snapshot;
        service.snapshot(snapshot);
        EXPECT_EQ(snapshot.count, ProfilerSnapshot::MaxRows);
        EXPECT_EQ(snapshot.dropped, 25u);
        EXPECT_STREQ(snapshot.rows[0].scopeName, "scope0");
    }

    TEST(ProfilerSnapshotTest, ASecondSnapshotReplacesTheFirst)
    {
        ProfilerServiceImpl service;
        service.recordSample("alpha", "update", 1.0);
        ProfilerSnapshot snapshot;
        service.snapshot(snapshot);
        ASSERT_EQ(snapshot.count, 1u);
        service.resetFrame();
        service.snapshot(snapshot);
        EXPECT_EQ(snapshot.count, 0u);
    }

    TEST(ProfilerSnapshotTest, LongNamesAreCutToTheProfilersNameLengthAndEndWithZero)
    {
        ProfilerServiceImpl service;
        service.recordSample(std::string(200, 'e'), std::string(200, 's'), 1.0);
        ProfilerSnapshot snapshot;
        service.snapshot(snapshot);
        ASSERT_EQ(snapshot.count, 1u);
        EXPECT_EQ(std::string(snapshot.rows[0].extensionId).size(), ProfilerSnapshot::NameLen - 1);
        EXPECT_EQ(std::string(snapshot.rows[0].scopeName).size(), ProfilerSnapshot::NameLen - 1);
    }

    TEST(ProfilerReportTest, AnEmptySnapshotSaysSo)
    {
        const ProfilerSnapshot snapshot;
        const std::string text = format(snapshot);
        EXPECT_NE(text.find("No extension scope"), std::string::npos) << text;
    }

    TEST(ProfilerReportTest, GroupsScopesByExtensionInFirstUseOrder)
    {
        ProfilerServiceImpl service;
        service.recordSample("alpha", "update", 1.25);
        service.recordSample("beta", "draw", 2.0);
        service.recordSample("alpha", "ai", 3.5);
        service.recordSample("alpha", "ai", 0.5);
        ProfilerSnapshot snapshot;
        service.snapshot(snapshot);

        const std::string text = format(snapshot);
        const std::size_t alpha = text.find("[alpha]");
        const std::size_t beta = text.find("[beta]");
        ASSERT_NE(alpha, std::string::npos) << text;
        ASSERT_NE(beta, std::string::npos) << text;
        EXPECT_LT(alpha, beta);
        // "ai" was used after "draw" but belongs to alpha's block
        const std::size_t ai = text.find("ai:");
        const std::size_t draw = text.find("draw:");
        ASSERT_NE(ai, std::string::npos) << text;
        ASSERT_NE(draw, std::string::npos) << text;
        EXPECT_GT(ai, alpha);
        EXPECT_LT(ai, beta);
        EXPECT_GT(draw, beta);
        // "[alpha]" appears once
        EXPECT_EQ(text.find("[alpha]", alpha + 1), std::string::npos);
    }

    TEST(ProfilerReportTest, ALineShowsCallsTotalAndSlowestInMilliseconds)
    {
        ProfilerServiceImpl service;
        service.recordSample("alpha", "ai", 3.5);
        service.recordSample("alpha", "ai", 0.5);
        ProfilerSnapshot snapshot;
        service.snapshot(snapshot);

        const std::string text = format(snapshot);
        EXPECT_NE(text.find("ai: 2 calls, 4.000 ms total, 3.500 ms max"), std::string::npos) << text;
    }

    TEST(ProfilerReportTest, DroppedScopesAreMentioned)
    {
        ProfilerSnapshot snapshot;
        snapshot.dropped = 12;
        const std::string text = format(snapshot);
        EXPECT_NE(text.find("12 more scopes not shown"), std::string::npos) << text;
    }

    TEST(ProfilerReportTest, MarkupIsEscapedOnlyWhenAsked)
    {
        ProfilerServiceImpl service;
        service.recordSample("a#b", "c#d", 1.0);
        ProfilerSnapshot snapshot;
        service.snapshot(snapshot);

        const std::string plain = format(snapshot, false);
        EXPECT_NE(plain.find("[a#b]"), std::string::npos) << plain;
        EXPECT_EQ(plain.find("##"), std::string::npos) << plain;

        const std::string escaped = format(snapshot, true);
        EXPECT_NE(escaped.find("[a##b]"), std::string::npos) << escaped;
        EXPECT_NE(escaped.find("c##d:"), std::string::npos) << escaped;
    }

    TEST(ProfilerReportTest, ATooSmallBufferIsCutWithDotsAndStaysTerminated)
    {
        ProfilerServiceImpl service;
        for (int i = 0; i < 20; ++i)
            service.recordSample("alpha", "scope" + std::to_string(i), 1.0);
        ProfilerSnapshot snapshot;
        service.snapshot(snapshot);

        for (const std::size_t size :
            { std::size_t(1), std::size_t(2), std::size_t(5), std::size_t(40), std::size_t(100) })
        {
            std::string buffer(size, '\x7f');
            const std::size_t length = formatProfilerReport(snapshot, buffer, false);
            EXPECT_LT(length, size) << size;
            EXPECT_EQ(buffer[length], '\0') << size;
            if (size >= 5)
            {
                EXPECT_EQ(buffer.substr(length - 3, 3), "...") << size;
            }
        }
    }

    TEST(ProfilerReportTest, AnEmptyBufferIsLeftAlone)
    {
        const ProfilerSnapshot snapshot;
        EXPECT_EQ(formatProfilerReport(snapshot, std::span<char>{}, false), 0u);
    }

    TEST(ProfilerReportTest, TheFullTableFitsTheHostsBuffer)
    {
        ProfilerServiceImpl service;
        for (std::size_t i = 0; i < ProfilerSnapshot::MaxRows; ++i)
            service.recordSample("extension_" + std::to_string(i % 8), std::string(40, 's') + std::to_string(i), 1.0);
        ProfilerSnapshot snapshot;
        service.snapshot(snapshot);

        std::string buffer(16384, '\0');
        const std::size_t length = formatProfilerReport(snapshot, buffer, true);
        EXPECT_LT(length, buffer.size());
        EXPECT_EQ(buffer.substr(length - 3, 3) == "...", false) << "the host's 16 KiB buffer holds a full table";
    }
}
