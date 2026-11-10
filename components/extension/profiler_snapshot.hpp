#pragma once

#include <openeden/profiling.hpp>

#include <array>
#include <cstddef>
#include <span>

namespace openeden::extension
{
    /**
     * @brief The profiler's scopes of one frame, copied out before the end-of-frame reset (task 0017 stage 5).
     *
     * Fixed size: ProfilerServiceImpl::snapshot() fills it without allocating, and scopes past MaxRows are counted in
     * `dropped`. Names are cut to the profiler's own name length. Owned by the engine's extension host and filled on
     * the main thread in the Ui phase.
     */
    struct ProfilerSnapshot
    {
        static constexpr std::size_t MaxRows = 128;
        static constexpr std::size_t NameLen = 64;

        struct Row
        {
            char extensionId[NameLen] = {};
            char scopeName[NameLen] = {};
            openeden::sdk::ProfileStats stats;
        };

        std::array<Row, MaxRows> rows{};
        std::size_t count = 0;
        std::size_t dropped = 0;
    };

    /**
     * @brief Writes the snapshot as text for the debug window: one block per extension, in the order the extensions
     * first appear, one line per scope with its calls, total and slowest call in milliseconds. Writes at most
     * out.size() - 1 characters and a terminating zero, ends a cut text with "...", and returns the length (not
     * counting the zero). With `escapeMarkup`, every '#' is doubled for MyGUI. Allocates nothing.
     */
    std::size_t formatProfilerReport(const ProfilerSnapshot& snapshot, std::span<char> out, bool escapeMarkup);
}
