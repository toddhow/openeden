#ifndef OPENMW_COMPONENTS_SETTINGS_CATEGORIES_VIDEOMEMORY_H
#define OPENMW_COMPONENTS_SETTINGS_CATEGORIES_VIDEOMEMORY_H

#include <components/settings/sanitizerimpl.hpp>
#include <components/settings/settingvalue.hpp>

#include <string>

namespace Settings
{
    // OpenEden (task 0080): the video memory scale-back. All off by default: with `enabled = false` no code of the
    // feature runs and no memory budget is read. The values are read at start.
    struct VideoMemoryCategory : WithIndex
    {
        using WithIndex::WithIndex;

        SettingValue<bool> mEnabled{ mIndex, "VideoMemory", "enabled" };
        // `auto` or a number of MiB (simulates a card of that size).
        SettingValue<std::string> mBudget{ mIndex, "VideoMemory", "budget" };
        SettingValue<float> mBudgetFraction{ mIndex, "VideoMemory", "budget fraction",
            makeClampSanitizerFloat(0.1f, 1.0f) };
        SettingValue<float> mHighWater{ mIndex, "VideoMemory", "high water", makeClampSanitizerFloat(0.3f, 1.0f) };
        SettingValue<float> mLowWater{ mIndex, "VideoMemory", "low water", makeClampSanitizerFloat(0.0f, 1.0f) };
        // `driver` or `ledger`.
        SettingValue<std::string> mUsageSource{ mIndex, "VideoMemory", "usage source" };
        // A comma list of system names, first reduced first.
        SettingValue<std::string> mOrder{ mIndex, "VideoMemory", "order" };
        // `automatic` or `manual`.
        SettingValue<std::string> mRestore{ mIndex, "VideoMemory", "restore" };
        SettingValue<float> mDwell{ mIndex, "VideoMemory", "dwell", makeClampSanitizerFloat(0, 3600) };
        SettingValue<float> mRestoreDwell{ mIndex, "VideoMemory", "restore dwell", makeClampSanitizerFloat(0, 3600) };
        SettingValue<float> mSampleInterval{ mIndex, "VideoMemory", "sample interval",
            makeClampSanitizerFloat(0.05f, 60) };
        SettingValue<bool> mReport{ mIndex, "VideoMemory", "report" };
    };
}

#endif
