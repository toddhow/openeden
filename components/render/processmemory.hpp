#ifndef OPENEDEN_RENDER_PROCESSMEMORY_H
#define OPENEDEN_RENDER_PROCESSMEMORY_H

#include <cstdint>
#include <string>

namespace Render
{
    // Task 0054 (S0). Bytes of physical memory this process holds right now (the working set on Windows, the resident
    // set on Linux), or 0 where the system cannot say.
    std::uint64_t processWorkingSetBytes();

    // Linux's /proc/self/statm is "size resident shared ..." in pages. Gives resident * pageSize, or 0 for text that
    // does not start with two numbers. Public so that it can be tested on any system.
    std::uint64_t parseStatmResidentBytes(const std::string& text, std::uint64_t pageSize);
}

#endif
