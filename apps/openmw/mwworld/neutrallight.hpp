#ifndef OPENMW_MWWORLD_NEUTRALLIGHT_H
#define OPENMW_MWWORLD_NEUTRALLIGHT_H

#include <array>
#include <cstdint>
#include <optional>

#include <components/esm3/loadligh.hpp>

#include <openeden/game/world_model.hpp>

// Lights of the neutral world (decision 0016, design document 10.4 R2, task 0113 stage N3a-5).
//
// The engine already draws a light from an ESM::Light (MWAnim::createObject -> addLight: a point light whose radius is
// ESM::Light::mData.mRadius and whose colour is mData.mColor, animated by the Flicker, FlickerSlow, Pulse and
// PulseSlow bits, see components/mwanimation/light.cpp). A neutral light (openeden::sdk::game::LightComponent) is
// turned into that record on the spot, so the engine's light path, its flicker and its point light shadows are used
// unchanged and a neutral light behaves as a Morrowind light with the same flags does.
//
// Pure functions, no engine state. Main thread, but nothing here needs it.

namespace MWWorld
{
    /// The colour bytes of a light with its fade value applied: the fade multiplies the three channels, capped so that
    /// the brightest channel does not pass 255 (the hue is kept; a fade above what the colour has room for is not
    /// applied, because ESM::Light stores 8 bit colour). A fade of 1 gives the colour unchanged.
    [[nodiscard]] std::array<std::uint8_t, 3> neutralLightColor(const openeden::sdk::game::LightComponent& light);

    /// A colour as ESM::Light and ESM::Cell store it: red in the low byte, then green and blue, alpha 0.
    [[nodiscard]] std::uint32_t packNeutralColor(const std::array<std::uint8_t, 3>& rgb);

    /// The ESM::Light that stands for the component: the radius (rounded, at least 0), the faded colour, the flag bits
    /// of the animation (the engine tests Flicker or FlickerSlow first, then Pulse or PulseSlow, so one bit is set) and
    /// Negative; the Dynamic bit is always set. Off By Default is NOT a flag of the record: the class decides whether
    /// to ask for the light at all (a light that is off by default is not created).
    /// With `fadeGain` the brightness the fade has beyond what neutralLightColor could put in the colour byte (fade
    /// over 255 / the brightest channel) is kept as the record's NeutralGain multiplier (task 0355 stage b; 39 percent
    /// of the Skyrim light records, up to five times, lost it before); without it the record is as before.
    /// With `ownFlicker` (task 0355 stage h) a light that animates and whose record has a usable flicker period and
    /// intensity amplitude carries them in the record (ESM::Light::NeutralFlicker, mData.mTime packed by
    /// MWAnim::NeutralFlicker::packTimeField, mData.mValue = `seed`), so MWAnim::addLight runs the light's own flicker
    /// or pulse; without it, or with no usable numbers, the record is as before (Morrowind's rule).
    [[nodiscard]] ESM::Light makeNeutralEsmLight(const openeden::sdk::game::LightComponent& light,
        bool fadeGain = false, bool ownFlicker = false, std::uint32_t seed = 0);

    /// A per-light seed from a placement's identity (content file index and reference number): the same placement
    /// always gets the same seed, whatever else is loaded.
    [[nodiscard]] std::uint32_t neutralLightSeed(std::uint32_t contentFile, std::uint32_t index);

    /// The multiplier above 1 that neutralLightColor leaves out (1 when none): fade / the scale it applied.
    [[nodiscard]] float neutralLightGain(const openeden::sdk::game::LightComponent& light);

    /// Task 0358: what the baked global illumination (`--bake-gi`) needs of a neutral light: the same colour and
    /// radius the engine's light gets (makeNeutralEsmLight), so the bake lights a room as the game does.
    struct NeutralGiLight
    {
        std::uint32_t mColor = 0; ///< As ESM::Light::mData.mColor (sRGB bytes, red lowest)
        float mGain = 1.f; ///< The brightness the colour byte cannot hold (1 when none); a multiplier of the linear colour
        float mRadius = 0.f; ///< Units, as the engine's light radius
        bool mNegative = false;
    };

    /// The light the bake sees for a neutral light: nothing for a light that is off by default or has no radius.
    [[nodiscard]] std::optional<NeutralGiLight> neutralGiLight(
        const openeden::sdk::game::LightComponent& light, bool fadeGain);
}

#endif
