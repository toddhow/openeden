#ifndef OPENEDEN_GAME_SKYRIMSE_LOCATION_CONDITIONS_H
#define OPENEDEN_GAME_SKYRIMSE_LOCATION_CONDITIONS_H

// Conditions that ask about a LOCATION (task 0211, radiant quests). A location alias with no fill subrecord is "find
// matching location": its conditions run on each candidate location ("run on subject"). The functions that occur in
// those condition lists in the five masters (SkyrimRadiantSurvey): LocationHasKeyword (562) 265, LocationHasRefType
// (563) 91, GetIsEditorLocation (565) 72, HasSameEditorLocAsRefAlias (181) 88, GetInCurrentLocAlias (360) 7,
// GetEventData (576) 2. Task 0226 implements 181 for a candidate LOCATION: HasSameEditorLocAsRefAlias(alias, keyword)
// is true when the candidate and the editor location of the reference in `alias` lie in the same location that carries
// `keyword` (each one's own nearest ancestor-or-self with the keyword, and the two are the same location). xEdit names
// the function and its two parameters (an alias and a keyword; the data agrees: 88 of 88 uses carry an alias id and a
// keyword form); the meaning is RECALLED-VERIFY (the Creation Kit wiki is unreachable). On a reference subject it stays
// false (fail closed). 562 is in the global table (it reads the world's location subject); 563 and 565 are added for
// the evaluators that need them through the extra-function table, with 576, so every other list keeps failing closed on
// them.
//
// RECALLED-VERIFY (xEdit names the functions, the Creation Kit wiki is unreachable): LocationHasRefType(type) is true
// when the location lists a special reference of that location reference type (LCSR); GetIsEditorLocation(location) is
// true when the subject is that location or inside it.

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
        // ---- task 0226 (non-pure: a source that does not answer them makes HasSameEditorLocAsRefAlias false) --------
        /// The reference an alias of the quest being evaluated holds now (the story filler's plan first); kNullFormId:
        /// none.
        [[nodiscard]] virtual FormId aliasReference(std::uint32_t /*alias*/) const { return kNullFormId; }
        /// The location itself or its nearest ancestor with the keyword; kNullFormId: none.
        [[nodiscard]] virtual FormId locationAncestorWithKeyword(FormId /*location*/, FormId /*keyword*/) const
        {
            return kNullFormId;
        }
        /// The editor location of a placed reference; kNullFormId: not known.
        [[nodiscard]] virtual FormId editorLocationOfReference(FormId /*reference*/) const { return kNullFormId; }
        /// Task 0230: the editor location of the subject of the condition (a placed reference, the speaker, an actor
        /// known by its base); kNullFormId: not known.
        [[nodiscard]] virtual FormId editorLocationOfSubject(conditions::Subject /*subject*/) const
        {
            return kNullFormId;
        }
    };

    /// 563 and 565 only (a test pins this table's size; fn_181 is in `aliasLocationExtraFunctions`).
    [[nodiscard]] const std::vector<conditions::ExtraFunction>& locationExtraFunctions();
    /// Task 0226: 181 (HasSameEditorLocAsRefAlias), task 0230: 567 (GetIsEditorLocAlias); the quest evaluator adds them
    /// to the others.
    [[nodiscard]] const std::vector<conditions::ExtraFunction>& aliasLocationExtraFunctions();
    /// 576 (the story event) plus 563 and 565: what the quest evaluator of the pack uses.
    [[nodiscard]] const std::vector<conditions::ExtraFunction>& storyAndLocationExtraFunctions();
}

#endif
