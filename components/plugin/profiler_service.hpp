#pragma once

#include <openeden/profiling.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

namespace openeden::plugin
{
    /**
     * @brief Engine implementation of the profiling service.
     *
     * Pre-allocates active scope slots and scope stats capacity to achieve
     * strictly zero dynamic heap allocations on the hot execution path.
     * Scope tracking is thread-aware to prevent data races across parallel worker threads.
     */
    class ProfilerServiceImpl : public openeden::sdk::ProfilerService
    {
    public:
        static constexpr std::size_t MaxActiveScopes = 512;
        static constexpr std::size_t DefaultStatsCapacity = 1024;
        static constexpr std::size_t MaxNameLen = 64;

        ProfilerServiceImpl();
        ~ProfilerServiceImpl() override = default;

        // Non-copyable
        ProfilerServiceImpl(const ProfilerServiceImpl&) = delete;
        ProfilerServiceImpl& operator=(const ProfilerServiceImpl&) = delete;

        // Movable
        ProfilerServiceImpl(ProfilerServiceImpl&&) noexcept = default;
        ProfilerServiceImpl& operator=(ProfilerServiceImpl&&) noexcept = default;

        void beginScope(std::string_view pluginId, std::string_view scopeName) override;
        void endScope(std::string_view pluginId, std::string_view scopeName) override;
        void recordSample(std::string_view pluginId, std::string_view scopeName, double elapsedMs) override;

        [[nodiscard]] bool hasScope(std::string_view pluginId, std::string_view scopeName) const override;
        [[nodiscard]] openeden::sdk::ProfileStats getScopeStats(
            std::string_view pluginId, std::string_view scopeName) const override;

        void resetFrame() override;

    private:
        struct ActiveScopeEntry
        {
            std::thread::id threadId{};
            char pluginId[MaxNameLen]{};
            char scopeName[MaxNameLen]{};
            std::chrono::steady_clock::time_point startTime{};
            bool inUse = false;
        };

        struct ScopeStatsEntry
        {
            char pluginId[MaxNameLen]{};
            char scopeName[MaxNameLen]{};
            openeden::sdk::ProfileStats stats{};
        };

        void recordSampleLocked(std::string_view pluginId, std::string_view scopeName, double elapsedMs);

        mutable std::mutex mMutex;
        std::array<ActiveScopeEntry, MaxActiveScopes> mActiveScopes{};
        std::vector<ScopeStatsEntry> mStats;
    };
} // namespace openeden::plugin
