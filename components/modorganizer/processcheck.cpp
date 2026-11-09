#include "processcheck.hpp"

#include <algorithm>
#include <cctype>

#ifdef _WIN32
#include <windows.h>

#include <tlhelp32.h>
#else
#include <filesystem>
#include <fstream>
#include <unistd.h>
#endif

namespace ModOrganizer
{
    namespace
    {
        std::string normalizeName(std::string_view name)
        {
            std::string result(name);
            std::transform(result.begin(), result.end(), result.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (result.size() > 4 && result.compare(result.size() - 4, 4, ".exe") == 0)
                result.resize(result.size() - 4);
            return result;
        }
    }

#ifdef _WIN32
    bool isProcessRunning(std::string_view executableName)
    {
        const std::string wanted = normalizeName(executableName);
        if (wanted.empty())
            return false;
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return false;
        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(entry);
        bool found = false;
        for (BOOL ok = Process32FirstW(snapshot, &entry); ok && !found; ok = Process32NextW(snapshot, &entry))
        {
            std::string name;
            for (const wchar_t* c = entry.szExeFile; *c != L'\0'; ++c)
                name.push_back(*c < 128 ? static_cast<char>(*c) : '?');
            found = normalizeName(name) == wanted;
        }
        CloseHandle(snapshot);
        return found;
    }

    std::string currentProcessName()
    {
        wchar_t buffer[MAX_PATH * 2];
        const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
        std::string name;
        std::size_t start = 0;
        for (DWORD i = 0; i < length; ++i)
            if (buffer[i] == L'\\' || buffer[i] == L'/')
                start = i + 1;
        for (DWORD i = static_cast<DWORD>(start); i < length; ++i)
            name.push_back(buffer[i] < 128 ? static_cast<char>(buffer[i]) : '?');
        return normalizeName(name);
    }
#else
    bool isProcessRunning(std::string_view executableName)
    {
        // /proc/<pid>/comm holds at most 15 characters of the executable name.
        std::string wanted = normalizeName(executableName);
        if (wanted.empty())
            return false;
        wanted.resize(std::min<std::size_t>(wanted.size(), 15));
        std::error_code ec;
        for (std::filesystem::directory_iterator it("/proc", ec), end; !ec && it != end; it.increment(ec))
        {
            const std::string name = it->path().filename().string();
            if (name.empty() || !std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isdigit(c); }))
                continue;
            std::ifstream comm(it->path() / "comm");
            std::string line;
            if (std::getline(comm, line) && normalizeName(line) == wanted)
                return true;
        }
        return false;
    }

    std::string currentProcessName()
    {
        std::error_code ec;
        const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
        std::string name = normalizeName(self.filename().string());
        name.resize(std::min<std::size_t>(name.size(), 15));
        return name;
    }
#endif
}
