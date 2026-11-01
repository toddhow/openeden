#ifndef CSV_DOC_LOGTAP_H
#define CSV_DOC_LOGTAP_H

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace CSVDoc
{
    /// Keeps a copy of every log line (what `Log(Debug::...)` writes, with its time and level prefix) for the
    /// Output Log panel. It uses the log's own listener hook (Debug::setLogListener, the one the game's in-game
    /// log window uses), so the log file and the console are not touched.
    ///
    /// Threads: any thread may log (the editor's worker threads do); the lines are kept under a mutex and the panel
    /// reads them on the main thread. The object lives until the process ends (deliberately never deleted: the
    /// listener can still be called while static objects are destroyed).
    class LogTap
    {
    public:
        /// Starts collecting. Calling it again does nothing.
        static LogTap& install();

        /// The tap if installed, else null.
        static LogTap* get();

        /// Appends the lines logged after \a cursor (a count of lines ever seen) to \a out and moves \a cursor.
        /// If lines were dropped before the panel saw them, the oldest still held come first.
        void linesSince(std::size_t& cursor, std::vector<std::string>& out);

        static constexpr std::size_t maxLines = 5000;

    private:
        LogTap() = default;

        std::mutex mMutex;
        std::deque<std::string> mLines;
        std::size_t mTotal = 0; // lines ever added
    };
}

#endif
