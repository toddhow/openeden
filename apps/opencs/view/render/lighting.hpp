#ifndef OPENCS_VIEW_LIGHTING_H
#define OPENCS_VIEW_LIGHTING_H

#include <optional>

#include <vsg/maths/vec4.h>

namespace CSVRender
{
    /// The lighting modes of the scene toolbar.
    enum class LightingMode
    {
        Day,
        Night,
        Bright
    };

    struct LightingValues
    {
        vsg::vec4 mAmbient;
        vsg::vec4 mDiffuse;
        /// Which child a NiSwitchNode named NightDaySwitch shows.
        unsigned int mNightDayIndex = 0;
    };

    /// \param cellAmbient the cell's own ambient colour, if it has one. Day and night use it; bright ignores it.
    LightingValues lightingValues(LightingMode mode, const std::optional<vsg::vec4>& cellAmbient, bool isExterior);
}

#endif
