#include "locomotion.hpp"

namespace HavokPack::Gait
{
    Weights weights(float, const Params&)
    {
        return {};
    }

    float smoothSpeed(float current, float, float, float)
    {
        return current;
    }

    float advancePhase(float phase, float, float)
    {
        return phase;
    }

    float clipTime(float, float, float)
    {
        return 0.f;
    }

    Transform blend(const Transform& a, const Transform&, float)
    {
        return a;
    }
}
