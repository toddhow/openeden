#include "loadtrace.hpp"

#include <components/debug/debuglog.hpp>
#include <components/files/conversion.hpp>

#include <chrono>
#include <fstream>
#include <mutex>
#include <vector>

namespace Misc::LoadTrace
{
    std::atomic<bool> sEnabled{ false };

    namespace
    {
        // One finished scope, or (name == nullptr) one finished frame whose number is in mValue.
        struct Event
        {
            const char* mName;
            std::int64_t mBegin;
            std::int64_t mEnd;
            std::int64_t mValue;
            std::uint32_t mThread;
        };

        // The most scopes kept. At about 20 per ordinary frame and a few thousand in a cell load, a long flight
        // stays far under this; when it is reached the rest are dropped and counted.
        constexpr std::size_t maxEvents = 4'000'000;

        std::mutex sMutex;
        std::vector<Event> sEvents;
        std::string sPath;
        std::size_t sDropped = 0;
        std::atomic<std::uint32_t> sNextThread{ 0 };

        std::uint32_t threadNumber()
        {
            // Numbers are given in the order threads first record something; the main thread records first.
            thread_local const std::uint32_t number = sNextThread.fetch_add(1);
            return number;
        }

        void push(const Event& event)
        {
            std::lock_guard lock(sMutex);
            if (sEvents.size() >= maxEvents)
            {
                ++sDropped;
                return;
            }
            sEvents.push_back(event);
        }
    }

    std::int64_t nowMicroseconds()
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    void start(const std::string& path)
    {
        {
            std::lock_guard lock(sMutex);
            sPath = path;
            sEvents.clear();
            sEvents.reserve(2'000'000);
            sDropped = 0;
        }
        threadNumber(); // the calling (main) thread is number 0
        sEnabled.store(true);
    }

    void record(const char* name, std::int64_t beginUs, std::int64_t endUs, std::int64_t value)
    {
        if (!enabled())
            return;
        push(Event{ name, beginUs, endUs, value, threadNumber() });
    }

    void frameEnd(std::uint64_t frame, std::int64_t beginUs, std::int64_t endUs)
    {
        if (!enabled())
            return;
        push(Event{ nullptr, beginUs, endUs, static_cast<std::int64_t>(frame), threadNumber() });
    }

    void stop()
    {
        if (!sEnabled.exchange(false))
            return;
        std::lock_guard lock(sMutex);
        std::ofstream out(Files::pathFromUnicodeString(sPath), std::ios::binary);
        if (!out)
        {
            Log(Debug::Error) << "Load trace: cannot write " << sPath;
            return;
        }
        // One JSON object per line: {"f":frame,"b":begin_us,"e":end_us} for a frame, and
        // {"n":"name","b":begin_us,"e":end_us,"v":value,"t":thread} for a scope.
        for (const Event& event : sEvents)
        {
            if (event.mName == nullptr)
                out << "{\"f\":" << event.mValue << ",\"b\":" << event.mBegin << ",\"e\":" << event.mEnd << "}\n";
            else
                out << "{\"n\":\"" << event.mName << "\",\"b\":" << event.mBegin << ",\"e\":" << event.mEnd
                    << ",\"v\":" << event.mValue << ",\"t\":" << event.mThread << "}\n";
        }
        Log(Debug::Info) << "Load trace: " << sEvents.size() << " events written to " << sPath << " (" << sDropped
                         << " dropped)";
        sEvents.clear();
    }
}
