#include "neutralplayerplace.hpp"

namespace MWPhysics
{
    namespace
    {
        // the fields that name the place: an interior by its cell, an exterior by its world and grid square
        bool samePlace(const openeden::sdk::game::PlayerPlace& a, const openeden::sdk::game::PlayerPlace& b)
        {
            if (a.interior != b.interior)
                return false;
            if (a.interior)
                return a.cell == b.cell;
            return a.world == b.world && a.gridX == b.gridX && a.gridY == b.gridY;
        }
    }

    bool PlayerPlaceFilter::valid(const openeden::sdk::game::PlayerPlace& place) const
    {
        return place.interior ? place.cell != openeden::sdk::game::kNoWorldId
                              : place.world != openeden::sdk::game::kNoWorldId;
    }

    bool PlayerPlaceFilter::accept(const openeden::sdk::game::PlayerPlace& place)
    {
        if (!valid(place))
            return false;
        if (mHave && samePlace(mLast, place))
            return false;
        mLast = place;
        mHave = true;
        return true;
    }
}
