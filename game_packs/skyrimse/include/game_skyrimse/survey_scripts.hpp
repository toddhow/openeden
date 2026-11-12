#ifndef OPENEDEN_GAME_SKYRIMSE_SURVEY_SCRIPTS_H
#define OPENEDEN_GAME_SKYRIMSE_SURVEY_SCRIPTS_H

// The Skyrim SE data survey, part 2: Papyrus events (task 0116). The native functions called by the scripts are ranked
// by the runtime itself (ScriptPapyrus::PapyrusRuntime::nativeCallSites, task 0093); this part counts the events that
// scripts declare handlers for. Pure: it reads parsed .pex files and keeps numbers.
//
// An event handler is a script function (not native, not a property accessor) whose name is "On" followed by an
// upper-case letter (Papyrus convention; the file has no event flag). An event is "declared" when some loaded class
// declares a native function of that name (for example the base classes' OnInit, OnActivate, OnHit), otherwise it is a
// custom event of the scripts.

#include <components/script_papyrus/pexfile.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace game_skyrimse::survey
{
    struct EventRow
    {
        std::string mName; ///< lower case
        std::uint64_t mHandlers = 0; ///< functions, states included
        std::uint64_t mScripts = 0; ///< distinct script objects with at least one handler
        std::uint64_t mInStates = 0; ///< handlers inside a named state
        bool mDeclaredNative = false;
    };

    class EventSurvey
    {
    public:
        void addScript(const ScriptPapyrus::Pex::File& file);

        std::uint64_t mObjects = 0;
        std::uint64_t mObjectsWithHandlers = 0;
        std::uint64_t mFunctions = 0;
        std::uint64_t mNativeFunctions = 0;

        /// Most scripts first, ties by name.
        [[nodiscard]] std::vector<EventRow> events() const;
        [[nodiscard]] std::size_t declaredNativeEventCount() const { return mDeclared.size(); }

    private:
        struct Acc
        {
            std::uint64_t mHandlers = 0, mScripts = 0, mInStates = 0;
        };
        std::map<std::string, Acc> mHandled;
        std::set<std::string> mDeclared;
    };
}

#endif
