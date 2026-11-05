#ifndef OPENMW_MWRENDER_PARTREADERRORLOG_H
#define OPENMW_MWRENDER_PARTREADERRORLOG_H

#include <cstddef>
#include <exception>
#include <mutex>
#include <set>
#include <string>
#include <string_view>

namespace MWRender
{
    // Task 0074h: a model that cannot be read is retried at every equipment change (Npc::insertBoundedPart,
    // WieldingCreature::addItemPart and the ammunition, holstered and quiver reads skip the part and the update goes
    // on), and each retry would log the same error again. This remembers which model paths have been reported, so
    // that each unreadable file is logged once per session; a repeat is a Verbose line.
    //
    // Keyed by the VFS path as the caller read it (lower case, forward slashes). Thread-safe: the part reads run on
    // the update workers (Npc::update's callers) and on the main thread, so every call takes the mutex. It holds the
    // paths of unreadable files only, at most `sCapacity` of them; beyond that a path counts as already reported.
    class ReadErrorLog
    {
    public:
        static constexpr std::size_t sCapacity = 1024;

        // True the first time `model` is reported, false afterwards.
        bool firstFailure(std::string_view model);
        // Forgets everything (tests).
        void clear();
        std::size_t size() const;

        // The session's log, used by the game.
        static ReadErrorLog& instance();

    private:
        mutable std::mutex mMutex;
        std::set<std::string, std::less<>> mReported;
    };

    // Logs `prefix` + the error's text at Error level the first time `model` fails in this session, and a short
    // Verbose line saying it is still failing afterwards. Any thread.
    void logPartReadError(std::string_view prefix, const std::string& model, const std::exception& error);
}

#endif
