#ifndef OPENMW_COMPONENTS_SETTINGS_CATEGORIES_BODIES_H
#define OPENMW_COMPONENTS_SETTINGS_CATEGORIES_BODIES_H

#include <components/settings/settingvalue.hpp>

namespace Settings
{
    // OpenEden AdvancedBodies (task 0069, stage d). Off by default: with `merged body = false` the stock parts are
    // drawn and no code of the merged body runs.
    struct BodiesCategory : WithIndex
    {
        using WithIndex::WithIndex;

        // Draw the merged body from the cache `openmw --bake-bodies` writes instead of the stock body parts, where the
        // cache is current for the actor's race and sex and nothing it wears replaces a merged slot (stage d).
        SettingValue<bool> mMergedBody{ mIndex, "Bodies", "merged body" };
    };
}

#endif
