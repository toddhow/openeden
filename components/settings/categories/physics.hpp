#ifndef OPENMW_COMPONENTS_SETTINGS_CATEGORIES_PHYSICS_H
#define OPENMW_COMPONENTS_SETTINGS_CATEGORIES_PHYSICS_H

#include <components/settings/sanitizerimpl.hpp>
#include <components/settings/settingvalue.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace Settings
{
    struct PhysicsCategory : WithIndex
    {
        using WithIndex::WithIndex;

        SettingValue<int> mAsyncNumThreads{ mIndex, "Physics", "async num threads", makeMaxSanitizerInt(0) };
        SettingValue<int> mLineofsightKeepInactiveCache{ mIndex, "Physics", "lineofsight keep inactive cache",
            makeMaxSanitizerInt(-1) };
        // Task 0035 stage b: allow the cmRagdoll debug console command. Off by default; nothing else uses it.
        SettingValue<bool> mCreativeMotionRagdolls{ mIndex, "Physics", "creativemotion ragdolls" };
    };
}

#endif
