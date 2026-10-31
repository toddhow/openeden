#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace openeden::sdk
{
    /**
     * @brief Aggregated profiling statistics for a named scope.
     */
    struct ProfileStats
    {
        std::uint64_t callCount = 0;
        double totalTimeMs = 0.0;
        double minTimeMs = 0.0;
        double maxTimeMs = 0.0;
        double avgTimeMs = 0.0;
    };

    /**
     * @brief Service measuring per-extension execution timing across named scopes.
     */
    class ProfilerService
    {
    public:
        virtual ~ProfilerService() = default;

        virtual void beginScope(std::string_view extensionId, std::string_view scopeName) = 0;
        virtual void endScope(std::string_view extensionId, std::string_view scopeName) = 0;
        virtual void recordSample(std::string_view extensionId, std::string_view scopeName, double elapsedMs) = 0;

        [[nodiscard]] virtual bool hasScope(std::string_view extensionId, std::string_view scopeName) const = 0;
        [[nodiscard]] virtual ProfileStats getScopeStats(
            std::string_view extensionId, std::string_view scopeName) const = 0;

        virtual void resetFrame() = 0;
    };

    /**
     * @brief RAII helper for profiling scopes. Automatically calls endScope on destruction.
     */
    class ProfileScope
    {
    public:
        ProfileScope(ProfilerService& profiler, std::string_view extensionId, std::string_view scopeName)
            : mProfiler(&profiler)
            , mExtensionId(extensionId)
            , mScopeName(scopeName)
        {
            mProfiler->beginScope(mExtensionId, mScopeName);
        }

        ~ProfileScope()
        {
            if (mProfiler)
            {
                mProfiler->endScope(mExtensionId, mScopeName);
            }
        }

        // Non-copyable
        ProfileScope(const ProfileScope&) = delete;
        ProfileScope& operator=(const ProfileScope&) = delete;

        // Movable
        ProfileScope(ProfileScope&& other) noexcept
            : mProfiler(other.mProfiler)
            , mExtensionId(other.mExtensionId)
            , mScopeName(other.mScopeName)
        {
            other.mProfiler = nullptr;
        }

        ProfileScope& operator=(ProfileScope&& other) noexcept
        {
            if (this != &other)
            {
                if (mProfiler)
                {
                    mProfiler->endScope(mExtensionId, mScopeName);
                }
                mProfiler = other.mProfiler;
                mExtensionId = other.mExtensionId;
                mScopeName = other.mScopeName;
                other.mProfiler = nullptr;
            }
            return *this;
        }

    private:
        ProfilerService* mProfiler = nullptr;
        std::string_view mExtensionId;
        std::string_view mScopeName;
    };
} // namespace openeden::sdk
