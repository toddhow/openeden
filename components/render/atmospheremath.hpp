#ifndef OPENEDEN_RENDER_ATMOSPHEREMATH_H
#define OPENEDEN_RENDER_ATMOSPHEREMATH_H

// OpenEden (task 0034): the maths of the atmosphere (R6, "visual weather state", stages a to c). No game types, and no
// shader twin: the model runs on the CPU once per frame and its result is handed to the renderer as the colours it
// always took (sky, fog, ambient, sun, and the cloud tint).
//
// The idea in plain words: sunlight crossing air loses blue light first, because blue scatters more than red
// (Rayleigh scattering). A high sun therefore stays white and a low sun turns orange and dim, and the sky it lights is
// blue overhead and warm near the horizon. Given only how high the sun is, this file computes those colours.
//
// Sources, reimplemented from the published formulas (no code taken):
//  - the optical depth of the air for each colour (Rayleigh) and of haze (a power law in the wavelength): Preetham,
//    Shirley and Smits, "A Practical Analytic Model for Daylight" (1999);
//  - the length of the air path at a given sun height: Kasten and Young, "Revised optical air mass tables and
//    approximation formula" (1989).
//
// The model describes a clear day. Morrowind's weather is much more than that, so the colours that the weather manager
// already computed (the "vanilla" colours) stay the starting point and the model is blended in (applyLook): by how
// clear the air is (the weather's own Glare_View value), by how high the sun is (nothing at night), and by the look.

#include <algorithm>
#include <cmath>
#include <string_view>

#include <vsg/maths/vec3.h>

#include <components/render/hdrmath.hpp>

namespace Render::Atmosphere
{
    constexpr float pi = 3.14159265358979f;

    // The colours the weather manager hands to the renderer, in the display-encoded space Morrowind's weather colours
    // are in. `horizon` is the fog colour (the sky's lower edge); `cloudTint` multiplies the cloud texture (white = as
    // before).
    struct Colours
    {
        vsg::vec3 sky{ 0.f, 0.f, 0.f };
        vsg::vec3 horizon{ 0.f, 0.f, 0.f };
        vsg::vec3 ambient{ 0.f, 0.f, 0.f };
        vsg::vec3 sun{ 0.f, 0.f, 0.f };
        vsg::vec3 cloudTint{ 1.f, 1.f, 1.f };
    };

    enum class Look
    {
        Vanilla,
        Physical
    };

    // The setting `atmosphere`, which needs the linear HDR path (its lighting is the one the colours were made for).
    inline bool useAtmosphere(bool setting, bool hdr)
    {
        return setting && hdr;
    }

    // `atmosphere look`: anything unknown is the gentle one.
    inline Look parseLook(std::string_view name)
    {
        return name == "physical" ? Look::Physical : Look::Vanilla;
    }

    // The sun's height above the horizon. `sunPosition` is the manager's sun position (RenderManager::setSunDirection);
    // the manager mirrors the sun at night (its z stays positive), so at night the angle is negated.
    inline float sunElevation(const vsg::vec3& sunPosition, bool night)
    {
        const float length
            = std::sqrt(sunPosition.x * sunPosition.x + sunPosition.y * sunPosition.y + sunPosition.z * sunPosition.z);
        if (length <= 0.f)
            return 0.f;
        const float elevation = std::asin(std::clamp(sunPosition.z / length, -1.f, 1.f));
        return night ? -elevation : elevation;
    }

    // Kasten and Young: the length of the air path in units of the vertical one; 1 at the zenith, about 38 at the
    // horizon. Below the horizon it stays at the horizon's value.
    inline float airMass(float elevation)
    {
        const float e = std::max(elevation, 0.f);
        const float degrees = e * 180.f / pi;
        return 1.f / (std::sin(e) + 0.50572f * std::pow(degrees + 6.07995f, -1.6364f));
    }

    // The wavelengths of the red, green and blue channels, in micrometres.
    constexpr vsg::vec3 wavelengths{ 0.65f, 0.55f, 0.45f };

    // The Rayleigh optical depth of the whole air column, looking straight up (Preetham et al., equation for tauR).
    inline float rayleighDepth(float wavelength)
    {
        const float l2 = wavelength * wavelength;
        return 0.008569f / (l2 * l2) * (1.f + 0.0113f / l2 + 0.00013f / (l2 * l2));
    }

    // The haze optical depth, looking straight up: beta * wavelength^-1.3 (Angstrom's law), with Preetham's
    // beta = 0.04608 T - 0.04586 for the turbidity T (1 is a very clear day).
    inline float hazeDepth(float wavelength, float turbidity)
    {
        const float beta = std::max(0.04608f * turbidity - 0.04586f, 0.f);
        return beta * std::pow(wavelength, -1.3f);
    }

    // The share of the sun's light that gets through the air, per colour channel, in linear light.
    inline vsg::vec3 sunTransmittance(float elevation, float turbidity)
    {
        const float m = airMass(elevation);
        const auto channel = [&](float wavelength) {
            return std::exp(-(rayleighDepth(wavelength) + hazeDepth(wavelength, turbidity)) * m);
        };
        return vsg::vec3(channel(wavelengths.x), channel(wavelengths.y), channel(wavelengths.z));
    }

    // 0 at night, 1 by day: the weight of the model against the vanilla colours. Below 4 degrees under the horizon the
    // vanilla night colours are used exactly; from 6 degrees above, the model has its full say.
    constexpr float nightEdge = -4.f * pi / 180.f;
    constexpr float dayEdge = 6.f * pi / 180.f;
    inline float dayWeight(float elevation)
    {
        const float t = std::clamp((elevation - nightEdge) / (dayEdge - nightEdge), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    }

    namespace Detail
    {
        inline float luma(const vsg::vec3& c)
        {
            return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
        }

        inline vsg::vec3 mix(const vsg::vec3& a, const vsg::vec3& b, float t)
        {
            return a + (b - a) * t;
        }

        inline vsg::vec3 power(const vsg::vec3& v, float p)
        {
            return vsg::vec3(std::pow(v.x, p), std::pow(v.y, p), std::pow(v.z, p));
        }

        inline vsg::vec3 clampColour(const vsg::vec3& c, float maximum)
        {
            return vsg::vec3(
                std::clamp(c.x, 0.f, maximum), std::clamp(c.y, 0.f, maximum), std::clamp(c.z, 0.f, maximum));
        }

        inline float smoothStep(float edge0, float edge1, float x)
        {
            const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
            return t * t * (3.f - 2.f * t);
        }

        // `colour` scaled to the brightness `wanted`. If that would push a channel past 1 (a red sun scaled up to the
        // brightness of a white one), the colour is made greyer instead of being clipped, so the brightness is kept.
        inline vsg::vec3 matchBrightness(const vsg::vec3& colour, float wanted)
        {
            const float own = luma(colour);
            if (own <= 1e-6f)
                return colour;
            const vsg::vec3 scaled = colour * (wanted / own);
            const float brightest = std::max({ scaled.x, scaled.y, scaled.z });
            if (brightest <= 1.f || brightest <= wanted)
                return scaled;
            const float keep = (1.f - wanted) / (brightest - wanted);
            return mix(vsg::vec3(wanted, wanted, wanted), scaled, keep);
        }

        // The brightest any model colour gets, in encoded values (vanilla's sun is at most 1).
        constexpr float maximumColour = 1.2f;

        // The turbidity of a clear day, and how much hazier the haziest weather is.
        constexpr float clearTurbidity = 2.f;
        constexpr float hazeTurbidity = 6.f;

        // The part of the zenith sky colour that is white (multiple scattering and the ozone layer, which the single
        // scattering model leaves out, make the real sky paler than pure Rayleigh blue). Tuned by eye.
        constexpr float skyWhitening = 0.05f;
        // The brightness of the sky overhead at noon, linear light. Tuned so that a clear noon sky is about as bright
        // as Morrowind's.
        constexpr float skyBrightness = 0.25f;
        // How much of the zenith colour a low sun filters out (the light that makes the zenith has crossed only part
        // of the air): the transmittance to this power tints the sky.
        constexpr float skyFilterPower = 0.1f;
        // The sky's brightness at the horizon and overhead, as a function of sqrt(sin(elevation)): a stand-in for the
        // multiple scattering that keeps a sunset sky from going black.
        constexpr float skyDimAtHorizon = 0.3f;
        // The horizon is brighter than the zenith by this factor.
        constexpr float horizonBoost = 1.4f;
        // Up to this height the horizon takes the sun's own colour (the glow around a low sun).
        constexpr float glowEdge = 30.f * pi / 180.f;
        // The ambient light is this share of the zenith's colour and the rest the horizon's.
        constexpr float ambientSkyShare = 0.55f;
        // A cloud lit by a low sun takes the sun's colour to this power (a cloud is not a filter, it scatters).
        constexpr float cloudTintPower = 0.25f;
        // The darkest a cloud tint gets with the sun on the horizon.
        constexpr float cloudDimAtHorizon = 0.55f;
        // The sky of the haziest air: this fraction of a clear sky's brightness.
        constexpr float hazeDim = 0.7f;
    }

    // The colours of a clear atmosphere at a sun height, thinned by `clearness` (1 clear to 0 hazy: the sky loses its
    // colour and some of its light). Encoded values.
    inline Colours model(float elevation, float clearness)
    {
        using namespace Detail;
        const float c = std::clamp(clearness, 0.f, 1.f);
        const float turbidity = clearTurbidity + (hazeTurbidity - clearTurbidity) * (1.f - c);
        const float up = std::sqrt(std::max(std::sin(std::max(elevation, 0.f)), 0.f));
        const vsg::vec3 white(1.f, 1.f, 1.f);

        const vsg::vec3 transmittance = sunTransmittance(elevation, turbidity);
        const vsg::vec3 zenithTransmittance = sunTransmittance(pi / 2.f, clearTurbidity);

        // The sun's own light, scaled so that a clear sun at the zenith has a brightness of 1.
        const vsg::vec3 sunLinear = transmittance / luma(zenithTransmittance);

        // The sky overhead: Rayleigh's colour (the optical depths, brightest in blue), a little whitened, filtered by
        // the sun's light and as bright as the sun is high.
        const vsg::vec3 rayleigh(
            rayleighDepth(wavelengths.x), rayleighDepth(wavelengths.y), rayleighDepth(wavelengths.z));
        const vsg::vec3 rayleighColour = rayleigh / std::max({ rayleigh.x, rayleigh.y, rayleigh.z });
        const vsg::vec3 filter = power(transmittance, skyFilterPower);
        const vsg::vec3 whitened = mix(rayleighColour, white, skyWhitening);
        vsg::vec3 skyLinear(whitened.x * filter.x, whitened.y * filter.y, whitened.z * filter.z);
        skyLinear = skyLinear * (skyBrightness / luma(skyLinear)) * (skyDimAtHorizon + (1.f - skyDimAtHorizon) * up);

        // Haze: a grey of the same brightness, and a little darker.
        const float dim = hazeDim + (1.f - hazeDim) * c;
        skyLinear = mix(white * luma(skyLinear), skyLinear, c) * dim;

        // The horizon: paler than the zenith, and warm (the sun's colour) while the sun is low.
        const vsg::vec3 paleHorizon = mix(skyLinear, white * luma(skyLinear), 0.5f);
        const vsg::vec3 warmDirection = transmittance / std::max(transmittance.x, 1e-6f);
        const vsg::vec3 warm = warmDirection * (luma(skyLinear) / std::max(luma(warmDirection), 1e-6f));
        const float glow = 1.f - smoothStep(0.f, glowEdge, elevation);
        const vsg::vec3 horizonLinear = mix(paleHorizon, warm, glow) * horizonBoost;

        const vsg::vec3 ambientLinear = skyLinear * ambientSkyShare + horizonLinear * (1.f - ambientSkyShare);

        // A cloud's tint: the sun's colour, softened, and dimmer as the sun gets low.
        const vsg::vec3 cloudLinear = power(transmittance / std::max(transmittance.x, 1e-6f), cloudTintPower)
            * (cloudDimAtHorizon + (1.f - cloudDimAtHorizon) * up);

        Colours out;
        out.sky = clampColour(Hdr::toEncoded(skyLinear), maximumColour);
        out.horizon = clampColour(Hdr::toEncoded(horizonLinear), maximumColour);
        out.ambient = clampColour(Hdr::toEncoded(ambientLinear), maximumColour);
        out.sun = clampColour(Hdr::toEncoded(sunLinear), maximumColour);
        out.cloudTint = clampColour(Hdr::toEncoded(cloudLinear), 1.f); // the cloud shader multiplies encoded colours
        return out;
    }

    // The colours to use: the weather's own (`vanilla`) blended towards the model.
    //  - weight = dayWeight * clearness (a weather with no glare, or the night, is the vanilla colours exactly);
    //  - Vanilla look: half the weight, and the model's colour is first scaled to the vanilla colour's brightness, so
    //    only the hue moves;
    //  - Physical look: the full weight, the model's brightness as well.
    inline Colours applyLook(const Colours& vanilla, float elevation, float clearness, Look look)
    {
        using namespace Detail;
        const float c = std::clamp(clearness, 0.f, 1.f);
        const float weight = dayWeight(elevation) * c * (look == Look::Vanilla ? 0.5f : 1.f);
        if (weight <= 0.f)
            return vanilla;
        const Colours m = model(elevation, c);
        const auto blend = [&](const vsg::vec3& from, const vsg::vec3& to) {
            vsg::vec3 target = to;
            if (look == Look::Vanilla)
            {
                const float lumaTo = luma(to);
                if (lumaTo > 1e-6f)
                    target = matchBrightness(to, luma(from));
            }
            return mix(from, target, weight);
        };
        Colours out;
        out.sky = blend(vanilla.sky, m.sky);
        out.horizon = blend(vanilla.horizon, m.horizon);
        out.ambient = blend(vanilla.ambient, m.ambient);
        out.sun = blend(vanilla.sun, m.sun);
        // The tint multiplies a texture; its brightness is the point, so it is blended as it is.
        out.cloudTint = mix(vanilla.cloudTint, m.cloudTint, weight);
        return out;
    }
}

#endif
