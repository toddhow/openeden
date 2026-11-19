#include "lightdata.hpp"

#include <algorithm>
#include <cmath>

#include <vsg/core/Array.h>

namespace View
{
    std::size_t maxLightsOf(const vsg::Data& lightData)
    {
        const std::size_t plain = std::max<std::size_t>(1, lightData.valueCount() / 2);
        const auto* array = dynamic_cast<const vsg::vec4Array*>(&lightData);
        if (array != nullptr && array->size() >= 4)
        {
            const float marker = array->at(3).w;
            if (std::isfinite(marker) && marker >= static_cast<float>(kMinLightsForSpotRegion) && marker < 4096.f)
            {
                const std::size_t m = static_cast<std::size_t>(marker);
                if (lightVec4Count(m) == array->size())
                    return m;
            }
        }
        return plain;
    }
}
