#ifndef OPENEDEN_MISC_LOADTRACE_H
#define OPENEDEN_MISC_LOADTRACE_H

#include <atomic>
#include <cstdint>
#include <string>

namespace Misc::LoadTrace
{
    /*
     * Task 0060 (LOD S6), stage a. A tracer that times named parts of the frame and of the cell load, so that the
     * hitches of research note 0026 can be attributed to code. It is a diagnostic only: nothing in the game reads it.
     *
     * Off (the default): a Scope is one relaxed read of an atomic flag and does nothing else.
     * On ([Cells] load trace = true): each Scope records its name, begin and end (microseconds on the steady clock),
     * a number of the caller's choosing and a small thread number, into a list kept in memory. The list is written
     * as JSON lines by stop(), at game exit. Writing only at the end keeps the file's cost out of the frames.
     *
     * Names must be string literals (the list stores the pointer). Scopes can be used from any thread.
     */
    extern std::atomic<bool> sEnabled;

    inline bool enabled()
    {
        return sEnabled.load(std::memory_order_relaxed);
    }

    // Microseconds on the steady clock, only called when enabled.
    std::int64_t nowMicroseconds();

    // Turn the tracer on and name the file stop() writes. Main thread, before the first frame.
    void start(const std::string& path);
    // Write the file and turn the tracer off. Safe to call when it was never started.
    void stop();

    // Add one finished scope.
    void record(const char* name, std::int64_t beginUs, std::int64_t endUs, std::int64_t value);
    // The main thread calls this when a frame ends: its number and its wall time in microseconds.
    void frameEnd(std::uint64_t frame, std::int64_t beginUs, std::int64_t endUs);

    // Records the time between its construction and its destruction.
    class Scope
    {
    public:
        explicit Scope(const char* name, std::int64_t value = 0) noexcept
        {
            if (enabled())
            {
                mName = name;
                mValue = value;
                mBegin = nowMicroseconds();
            }
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        ~Scope() { end(); }
        // Finish early; the destructor then does nothing.
        void end() noexcept
        {
            if (mName != nullptr)
            {
                record(mName, mBegin, nowMicroseconds(), mValue);
                mName = nullptr;
            }
        }
        // For a number that is only known at the end of the scope (objects inserted, for example).
        void setValue(std::int64_t value) noexcept { mValue = value; }

    private:
        const char* mName = nullptr;
        std::int64_t mBegin = 0;
        std::int64_t mValue = 0;
    };
}

#endif
