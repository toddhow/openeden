#ifndef OPENEDEN_GAME_SKYRIMSE_LOCATION_CONDITIONS_H
#define OPENEDEN_GAME_SKYRIMSE_LOCATION_CONDITIONS_H

// Conditions that ask about a LOCATION (task 0211, radiant quests). A location alias with no fill subrecord is "find
// matching location": its conditions run on each candidate location ("run on subject"). The functions that occur in those
// condition lists in the five masters (SkyrimRadiantSurvey): LocationHasKeyword (562) 265, LocationHasRefType (563) 91,
// GetIsEditorLocation (565) 72, HasSameEditorLocAsRefAlias (181) 88, GetInCurrentLocAlias (360) 7, GetEventData (576) 2.
// 562 is in the global table (it reads the world's location subject); 563 and 565 are added for the evaluators that
// need them through the extra-function table, with 576, so every other list keeps failing closed on them.
//
// RECALLED-VERIFY (xEdit names the functions, the Creation Kit wiki is unreachable): LocationHasRefType(type) is true when
// the location lists a special reference of that location reference type (LCSR); GetIsEditorLocation(location) is true
// when the subject is that location or inside it.

#include <game_skyrimse/conditions.hpp>
#include <game_skyrimse/records.hpp>

#include <vector>

namespace game_skyrimse
{
    /// Implemented by the condition world that can evaluate a list for a candidate location.
    class LocationSubjectSource
    {
    public:
        virtual ~LocationSubjectSource() = default;
        /// The candidate location the list is evaluated for; kNullFormId when the evaluation is not for a location.
        [[nodiscard]] virtual FormId locationSubject() const = 0;
        /// A stored (plugin-local) form id of the condition being evaluated, as a load-order id.
        [[nodiscard]] virtual FormId resolveLocationForm(std::uint32_t stored) const = 0;
        [[nodiscard]] virtual bool locationHasRefType(FormId location, FormId refType) const = 0;
        [[nodiscard]] virtual bool locationWithin(FormId location, FormId ancestor) const = 0;
    };

    /// 563 and 565 only.
    [[nodiscard]] const std::vector<conditions::ExtraFunction>& locationExtraFunctions();
    /// 576 (the story event) plus 563 and 565: what the quest evaluator of the pack uses.
    [[nodiscard]] const std::vector<conditions::ExtraFunction>& storyAndLocationExtraFunctions();
}

#endif
