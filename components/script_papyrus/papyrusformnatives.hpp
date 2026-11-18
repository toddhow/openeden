#ifndef OPENEDEN_COMPONENTS_SCRIPT_PAPYRUS_PAPYRUSFORMNATIVES_H
#define OPENEDEN_COMPONENTS_SCRIPT_PAPYRUS_PAPYRUSFORMNATIVES_H

// Task 0321: natives that Skyrim SE and Fallout 4 share, answered once for every game pack.
//
// In plain words: both games' scripts call Actor.GetLevel, FormList.GetAt and a few hundred more functions that mean
// the same thing in each. The game pack owns the records, so the answer comes from it, but the native itself (what
// the arguments mean, what to return for a missing item, the degrees) is written here once. Two groups:
//   - QUERY natives read the game's records through the optional PapyrusFormQueries capability of the world given to
//     the constructor (sdk/include/openeden/script/papyrus.hpp). A world without it gets none of them registered
//     (they stay tolerant) and the report says so. The natives are (RECALLED-VERIFY meanings, Creation Kit wiki):
//       Actor.GetLevel, Actor.GetRace, ActorBase.GetRace, FormList.GetSize, FormList.GetAt, FormList.HasForm (the
//       members of the list itself, nested lists are not searched), ObjectReference.GetAngleX, GetAngleY, GetAngleZ
//       (degrees).
//   - COSMETIC natives do nothing on purpose, because a sound, a shader, a particle effect, a camera shake or an
//     animation-graph variable has no counterpart in a neutral world (Sound.Play returns the instance id 0). They are
//     registered so that a script's call is answered and counted as done, not as missing.
// Threading: main thread only, like every native. Ownership: the object owns its state; the world is not owned and
// must outlive every call into the runtime that can reach a handler.

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include <components/script_papyrus/papyrusnatives.hpp>
#include <openeden/script/papyrus.hpp>

namespace ScriptPapyrus
{
    class FormNatives
    {
    public:
        explicit FormNatives(openeden::script::PapyrusWorld& world);
        ~FormNatives();
        FormNatives(const FormNatives&) = delete;
        FormNatives& operator=(const FormNatives&) = delete;

        /// Registers what the loaded classes declare (and all of it for a class not loaded yet). A native the game
        /// pack registered first stays the game pack's (reported in failures, like PureNatives). The query natives
        /// are skipped, and `queriesMissing` is set, when the world has no PapyrusFormQueries.
        PureNativesReport registerAll(openeden::script::PapyrusContract& contract);

        /// "class.function" in lower case, sorted.
        static std::span<const std::string_view> queryNames() noexcept;
        static std::span<const std::string_view> cosmeticNames() noexcept;

        bool queriesMissing() const noexcept;
        /// Query calls the world refused (a zero value was returned) since construction.
        std::uint64_t unanswered() const noexcept;

        struct State;

    private:
        std::shared_ptr<State> mState;
    };
}

#endif
