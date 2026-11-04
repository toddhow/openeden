#include "processmemory.hpp"

#include <cctype>
#include <charconv>
#include <fstream>
#include <sstream>

#ifdef _WIN32
// PSAPI_VERSION 2 makes GetProcessMemoryInfo come from kernel32, so no extra library is linked.
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2
#endif
#include <windows.h>

#include <psapi.h>
#else
#include <unistd.h>
#endif

namespace Render
{
    std::uint64_t parseStatmResidentBytes(const std::string& text, std::uint64_t pageSize)
    {
        std::istringstream in(text);
        std::string first;
        std::string second;
        if (!(in >> first >> second))
            return 0;
        for (char c : first + second)
            if (!std::isdigit(static_cast<unsigned char>(c)))
                return 0;
        std::uint64_t pages = 0;
        const auto [end, error] = std::from_chars(second.data(), second.data() + second.size(), pages);
        if (error != std::errc() || end != second.data() + second.size())
            return 0;
        return pages * pageSize;
    }

    std::uint64_t processWorkingSetBytes()
    {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS counters{};
        counters.cb = sizeof(counters);
        if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
            return 0;
        return counters.WorkingSetSize;
#else
        std::ifstream file("/proc/self/statm");
        std::string text;
        std::getline(file, text);
        const long page = sysconf(_SC_PAGESIZE);
        return parseStatmResidentBytes(text, page > 0 ? static_cast<std::uint64_t>(page) : 4096);
#endif
    }
}

namespace Render
{
    std::uint64_t physicalMemoryBytes()
    {
#ifdef _WIN32
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (!GlobalMemoryStatusEx(&status))
            return 0;
        return status.ullTotalPhys;
#else
        const long pages = sysconf(_SC_PHYS_PAGES);
        const long page = sysconf(_SC_PAGESIZE);
        if (pages <= 0 || page <= 0)
            return 0;
        return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(page);
#endif
    }
}
