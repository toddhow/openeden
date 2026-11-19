#ifndef OPENEDEN_VIEW_LIGHTDATA_H
#define OPENEDEN_VIEW_LIGHTDATA_H

#include <cstddef>

#include <vsg/core/Data.h>

namespace View
{
    /*
     * The layout of the light buffer (VIEW_LIGHTS_BINDING), an array of 32 byte PointLight structs of two vec4 each:
     *   [0, maxLights)                       the lights: { colour rgb, radius } and { eye position xyz, w }
     *   [maxLights, maxLights + spotStructs)  (task 0355 stage g, only when maxLights >= 2) one vec4 a light, two to a
     *                                         struct: the spot light's axis in eye space xyz and the cos of its half
     *                                         angle, or 0 for a light that is not a spot
     * The first light's position.w is the number of lights; the SECOND light's position.w is the marker of the spot
     * region: the value of maxLights (0 or absent: no region, a buffer made by anything else). Light i's spot vec4 is
     * element maxLights + i / 2, member colourIntensity for even i and position for odd i
     * (files/shaders/lib/light/pointlights.*). Change both together.
     */
    constexpr std::size_t kMinLightsForSpotRegion = 2;

    /// The number of 32 byte structs a buffer for `maxLights` lights holds.
    constexpr std::size_t lightStructCount(std::size_t maxLights)
    {
        return maxLights >= kMinLightsForSpotRegion ? maxLights + (maxLights + 1) / 2 : maxLights;
    }

    /// The vec4 count of that buffer.
    constexpr std::size_t lightVec4Count(std::size_t maxLights)
    {
        return 2 * lightStructCount(maxLights);
    }

    /// How many lights a light buffer was made for: the marker CollectLights writes when it has the spot region, else
    /// half its vec4 count (a buffer of any other origin); at least 1.
    std::size_t maxLightsOf(const vsg::Data& lightData);
}

#endif
