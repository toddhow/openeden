#include "profiler_service.hpp"

#include <algorithm>
#include <cstring>

namespace
{
    void copyStringToBuffer(char* dst, std::size_t dstSize, std::string_view src) noexcept
    {
        if (dstSize == 0)
            return;
        const std::size_t copyLen = std::min(dstSize - 1, src.size());
        if (copyLen > 0)
        {
            std::memcpy(dst, src.data(), copyLen);
        }
        dst[copyLen] = '\0';
    }

    bool stringViewMatches(const char* buf, std::string_view sv) noexcept
    {
        return std::string_view(buf) == sv;
    }
} // namespace

namespace openeden::extension
{
    ProfilerServiceImpl::ProfilerServiceImpl()
    {
        mStats.reserve(DefaultStatsCapacity);
    }

    void ProfilerServiceImpl::beginScope(std::string_view extensionId, std::string_view scopeName)
    {
        const auto now = std::chrono::steady_clock::now();
        const auto tid = std::this_thread::get_id();

        std::lock_guard<std::mutex> lock(mMutex);
        for (auto& entry : mActiveScopes)
        {
            if (!entry.inUse)
            {
                entry.threadId = tid;
                copyStringToBuffer(entry.extensionId, sizeof(entry.extensionId), extensionId);
                copyStringToBuffer(entry.scopeName, sizeof(entry.scopeName), scopeName);
                entry.startTime = now;
                entry.inUse = true;
                return;
            }
        }
        // If max active scopes pool is exhausted, gracefully ignore to avoid memory faults
    }

    void ProfilerServiceImpl::endScope(std::string_view extensionId, std::string_view scopeName)
    {
        const auto now = std::chrono::steady_clock::now();
        const auto tid = std::this_thread::get_id();
        double elapsedMs = 0.0;
        bool found = false;

        {
            std::lock_guard<std::mutex> lock(mMutex);
            // Search backwards to unwind nested scopes LIFO on this calling thread
            for (auto it = mActiveScopes.rbegin(); it != mActiveScopes.rend(); ++it)
            {
                if (it->inUse && it->threadId == tid && stringViewMatches(it->extensionId, extensionId)
                    && stringViewMatches(it->scopeName, scopeName))
                {
                    const auto duration = std::chrono::duration<double, std::milli>(now - it->startTime);
                    elapsedMs = duration.count();
                    it->inUse = false;
                    found = true;
                    break;
                }
            }

            if (found)
            {
                recordSampleLocked(extensionId, scopeName, elapsedMs);
            }
        }
    }

    void ProfilerServiceImpl::recordSample(std::string_view extensionId, std::string_view scopeName, double elapsedMs)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        recordSampleLocked(extensionId, scopeName, elapsedMs);
    }

    void ProfilerServiceImpl::recordSampleLocked(
        std::string_view extensionId, std::string_view scopeName, double elapsedMs)
    {
        for (auto& entry : mStats)
        {
            if (stringViewMatches(entry.extensionId, extensionId) && stringViewMatches(entry.scopeName, scopeName))
            {
                if (entry.stats.callCount == 0)
                {
                    entry.stats.callCount = 1;
                    entry.stats.totalTimeMs = elapsedMs;
                    entry.stats.minTimeMs = elapsedMs;
                    entry.stats.maxTimeMs = elapsedMs;
                    entry.stats.avgTimeMs = elapsedMs;
                }
                else
                {
                    entry.stats.callCount += 1;
                    entry.stats.totalTimeMs += elapsedMs;
                    entry.stats.minTimeMs = std::min(entry.stats.minTimeMs, elapsedMs);
                    entry.stats.maxTimeMs = std::max(entry.stats.maxTimeMs, elapsedMs);
                    entry.stats.avgTimeMs = entry.stats.totalTimeMs / static_cast<double>(entry.stats.callCount);
                }
                return;
            }
        }

        // Register new scope
        ScopeStatsEntry newEntry{};
        copyStringToBuffer(newEntry.extensionId, sizeof(newEntry.extensionId), extensionId);
        copyStringToBuffer(newEntry.scopeName, sizeof(newEntry.scopeName), scopeName);
        newEntry.stats.callCount = 1;
        newEntry.stats.totalTimeMs = elapsedMs;
        newEntry.stats.minTimeMs = elapsedMs;
        newEntry.stats.maxTimeMs = elapsedMs;
        newEntry.stats.avgTimeMs = elapsedMs;
        mStats.push_back(newEntry);
    }

    bool ProfilerServiceImpl::hasScope(std::string_view extensionId, std::string_view scopeName) const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        for (const auto& entry : mStats)
        {
            if (stringViewMatches(entry.extensionId, extensionId) && stringViewMatches(entry.scopeName, scopeName))
            {
                return entry.stats.callCount > 0;
            }
        }
        return false;
    }

    openeden::sdk::ProfileStats ProfilerServiceImpl::getScopeStats(
        std::string_view extensionId, std::string_view scopeName) const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        for (const auto& entry : mStats)
        {
            if (stringViewMatches(entry.extensionId, extensionId) && stringViewMatches(entry.scopeName, scopeName))
            {
                return entry.stats;
            }
        }
        return {};
    }

    void ProfilerServiceImpl::snapshot(ProfilerSnapshot& out) const
    {
        out.count = 0;
        out.dropped = 0;
        std::lock_guard<std::mutex> lock(mMutex);
        for (const ScopeStatsEntry& entry : mStats)
        {
            if (entry.stats.callCount == 0)
                continue;
            if (out.count >= ProfilerSnapshot::MaxRows)
            {
                ++out.dropped;
                continue;
            }
            static_assert(ProfilerSnapshot::NameLen == MaxNameLen);
            ProfilerSnapshot::Row& row = out.rows[out.count++];
            std::memcpy(row.extensionId, entry.extensionId, sizeof(row.extensionId));
            std::memcpy(row.scopeName, entry.scopeName, sizeof(row.scopeName));
            row.stats = entry.stats;
        }
    }

    void ProfilerServiceImpl::resetFrame()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        for (auto& entry : mStats)
        {
            entry.stats.callCount = 0;
            entry.stats.totalTimeMs = 0.0;
            entry.stats.minTimeMs = 0.0;
            entry.stats.maxTimeMs = 0.0;
            entry.stats.avgTimeMs = 0.0;
        }
    }
} // namespace openeden::extension
