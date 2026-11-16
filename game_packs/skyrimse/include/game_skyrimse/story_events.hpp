#ifndef OPENEDEN_GAME_SKYRIMSE_STORY_EVENTS_H
#define OPENEDEN_GAME_SKYRIMSE_STORY_EVENTS_H

// Story events the world can raise (task 0211, milestone M-S6). Pure: the event a death makes, the location a place
// belongs to, and the rule for "the player changed location". The pack's driver feeds them; the story manager (story.hpp)
// answers them.
//
// KILL (VERIFIED on the 8 KILL quests of the five masters that name their members: the alias of a victim reads 'R1' in
// all of them, the alias of the killer or attacker reads 'R2' in all of them, and WIKill06's location alias reads 'L1'):
//  - R1 the dead actor, R2 the killer (the player is the reference 0x14), L1 the location the actor died in.
// CLOC ("change location"): L1 the location left, L2 the location entered; an exterior cell's location is its XLCN, else
// the location that lists its grid square (LocationIndex::locationOfGrid; only 1,907 of 75,776 exterior CELL records have
// an XLCN, 403 of 831 LCTN list exterior cells).

#include <game_skyrimse/location.hpp>
#include <game_skyrimse/story.hpp>

#include <cstdint>
#include <functional>
#include <optional>

namespace game_skyrimse
{
    /// The KILL event of a death. `killer` 0 (nobody found) leaves R2 empty.
    [[nodiscard]] StoryEvent makeKillEvent(FormId victim, FormId killer, FormId location);

    /// Which location a place in the world is in, from the data: an interior cell's XLCN, an exterior cell's XLCN, else
    /// the location that lists the grid square. kNullFormId for nowhere.
    struct PlaceQuery
    {
        bool mInterior = false;
        FormId mWorld = kNullFormId; ///< exterior: the worldspace
        std::int32_t mX = 0;
        std::int32_t mY = 0; ///< exterior: the grid square
        FormId mCellLocation = kNullFormId; ///< the XLCN of the cell the caller found (interior or exterior), or 0
    };
    [[nodiscard]] FormId locationOfPlace(const PlaceQuery& place, const LocationIndex& locations);

    /// "The player changed location": remembers the last location and says when a new one is entered. A move to nowhere
    /// (0) is remembered (so that coming back raises the event) and raises nothing.
    class LocationChange
    {
    public:
        struct Change
        {
            FormId mFrom = kNullFormId;
            FormId mTo = kNullFormId;
        };
        [[nodiscard]] std::optional<Change> arrive(FormId location);
        [[nodiscard]] FormId current() const { return mCurrent; }
        void reset() { mCurrent = kNullFormId; }

    private:
        FormId mCurrent = kNullFormId;
    };
}

#endif
