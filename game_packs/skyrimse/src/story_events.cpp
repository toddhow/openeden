#include "game_skyrimse/story_events.hpp"

namespace game_skyrimse
{
    namespace
    {
        bool usable(FormId id)
        {
            return id != kNullFormId && id != kUnresolvedFormId;
        }
    }

    StoryEvent makeKillEvent(FormId victim, FormId killer, FormId location)
    {
        StoryEvent event;
        event.mType = "KILL";
        event.mRef1 = victim;
        event.mRef2 = killer;
        event.mLocation1 = location;
        return event;
    }

    FormId locationOfPlace(const PlaceQuery& place, const LocationIndex& locations)
    {
        if (usable(place.mCellLocation))
            return place.mCellLocation;
        if (place.mInterior || !usable(place.mWorld))
            return kNullFormId;
        return locations.locationOfGrid(place.mWorld, place.mX, place.mY);
    }

    std::optional<LocationChange::Change> LocationChange::arrive(FormId location)
    {
        if (!usable(location))
            location = kNullFormId;
        if (location == mCurrent)
            return std::nullopt;
        const Change change{ mCurrent, location };
        mCurrent = location;
        if (location == kNullFormId)
            return std::nullopt; // nowhere is remembered, nothing is raised
        return change;
    }
}
