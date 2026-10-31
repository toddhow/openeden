#include "lighting.hpp"

namespace
{
    // The values of the OSG editor's LightingDay, LightingNight and LightingBright.
    constexpr float sDayAmbient = 0.7f;
    constexpr float sNightAmbient = 0.2f;
    constexpr float sNightDiffuse = 0.2f;
}

CSVRender::LightingValues CSVRender::lightingValues(
    LightingMode mode, const std::optional<vsg::vec4>& cellAmbient, bool isExterior)
{
    const vsg::vec4 white(1.f, 1.f, 1.f, 1.f);
    switch (mode)
    {
        case LightingMode::Night:
            return { cellAmbient.value_or(vsg::vec4(sNightAmbient, sNightAmbient, sNightAmbient, 1.f)),
                vsg::vec4(sNightDiffuse, sNightDiffuse, sNightDiffuse, 1.f), isExterior ? 1u : 0u };
        case LightingMode::Bright:
            return { white, white, 0 };
        case LightingMode::Day:
            break;
    }
    return { cellAmbient.value_or(vsg::vec4(sDayAmbient, sDayAmbient, sDayAmbient, 1.f)), white, 0 };
}
