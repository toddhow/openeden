#ifndef OPENMW_MWWORLD_NEUTRALFOGCURVE_H
#define OPENMW_MWWORLD_NEUTRALFOGCURVE_H

namespace MWWorld
{
    /// Task 0355 (follow-up of stage d): the fog power and maximum of a game pack's interior (share = pow(linear, power)
    /// * max). Both 1 is the plain linear fog and is never stored as a curve. See neutralcellatmosphere.hpp.
    struct NeutralFogCurve
    {
        float power = 1.f;
        float max = 1.f;

        friend bool operator==(const NeutralFogCurve&, const NeutralFogCurve&) = default;
    };
}

#endif
