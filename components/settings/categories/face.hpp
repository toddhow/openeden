#ifndef OPENMW_COMPONENTS_SETTINGS_CATEGORIES_FACE_H
#define OPENMW_COMPONENTS_SETTINGS_CATEGORIES_FACE_H

#include <components/settings/settingvalue.hpp>

namespace Settings
{
    // OpenEden facial speech animation (task 0071, engine wiring). Off by default: with `speech curves = false` the
    // face code of this feature is not entered and the mouth follows the voice loudness as in OpenMW.
    struct FaceCategory : WithIndex
    {
        using WithIndex::WithIndex;

        // Drive the talk and blink of a stock head from the facial curves file that sits beside a voice line (the
        // voice path with its extension replaced by `.facecurves` or `.facecurves.json`), at the sound's playback
        // position. A line without a usable curves file keeps the loudness drive.
        SettingValue<bool> mSpeechCurves{ mIndex, "Face", "speech curves" };
    };
}

#endif
