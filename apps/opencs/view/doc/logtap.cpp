#include "logtap.hpp"

#include <string_view>

#include <components/debug/debugging.hpp>

namespace
{
    CSVDoc::LogTap* sTap = nullptr;
}

CSVDoc::LogTap& CSVDoc::LogTap::install()
{
    if (sTap == nullptr)
    {
        sTap = new LogTap; // never deleted, see the header
        Debug::setLogListener([tap = sTap](Debug::Level, std::string_view prefix, std::string_view message) {
            std::string line;
            line.reserve(prefix.size() + message.size());
            line.append(prefix).append(message);
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                line.pop_back();

            std::lock_guard<std::mutex> lock(tap->mMutex);
            tap->mLines.push_back(std::move(line));
            ++tap->mTotal;
            if (tap->mLines.size() > maxLines)
                tap->mLines.pop_front();
        });
    }
    return *sTap;
}

CSVDoc::LogTap* CSVDoc::LogTap::get()
{
    return sTap;
}

void CSVDoc::LogTap::linesSince(std::size_t& cursor, std::vector<std::string>& out)
{
    std::lock_guard<std::mutex> lock(mMutex);
    const std::size_t firstHeld = mTotal - mLines.size();
    if (cursor < firstHeld)
        cursor = firstHeld; // the lines in between were dropped
    for (std::size_t i = cursor - firstHeld; i < mLines.size(); ++i)
        out.push_back(mLines[i]);
    cursor = mTotal;
}
