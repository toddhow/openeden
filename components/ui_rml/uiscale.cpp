#include "uiscale.hpp"

#include <algorithm>
#include <cmath>

namespace UiRml
{
    float dpRatioFor(float factor)
    {
        if (!std::isfinite(factor) || !(factor > 0.f))
            return 1.f;
        return std::clamp(factor, kMinGuiScale, kMaxGuiScale);
    }
}
