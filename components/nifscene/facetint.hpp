#ifndef OPENEDEN_COMPONENTS_NIFSCENE_FACETINT_HPP
#define OPENEDEN_COMPONENTS_NIFSCENE_FACETINT_HPP

// Task 0125 (neutral world, Skyrim SE faces): the pure half of colouring an actor's face. A Skyrim face is drawn from
// a grey-brown detail map (MaleHead.dds, shared by many faces) and a per-actor tint (the file under
// FaceGenData/FaceTint/<plugin>/<id>.dds, the actor's skin tone, blush and paint baked by the Creation Kit); the
// game's shader multiplies them. The object shader here has one diffuse map, so the multiplication is done once on
// the CPU. No Vulkan, VSG or engine type; nothing throws.

#include <cstdint>
#include <string>
#include <vector>

namespace NifScene::FaceTint
{
    /// The factor the tint is multiplied in with: colour = base * tint * kTintGain. 2 because the detail map alone is a
    /// dark grey-brown (the shared MaleHead.dds averages (77, 69, 65) of 255, measured) and the baked tints are
    /// light skin colours (mean (194, 167, 159) for one sampled file): base * tint alone would be darker than the
    /// base; with 2 the product is a plausible skin tone. RECALLED-VERIFY: that the game's own shader uses exactly 2
    /// (and not an overlay blend) is not read from any source here; it is judged by frames.
    inline constexpr unsigned kTintGain = 2;

    struct Image
    {
        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;
        std::vector<std::uint8_t> mRgba; ///< mWidth * mHeight * 4 bytes, row 0 first
    };

    /// out = base with every colour channel multiplied by the tint (bilinearly sampled at the same normalised texture
    /// coordinates, so the two maps may differ in size) and by kTintGain, clamped to 255; alpha is the base's. False,
    /// with `error`, when either image is empty or its byte count does not match its size; `out` is then empty.
    [[nodiscard]] bool composite(const Image& base, const Image& tint, Image& out, std::string& error);

    /// A texture path as a game file names it, in the form the engine's texture lookup takes: forward slashes, lower
    /// case, no leading "data/", and a leading "textures/" when the stored path lacks it (the face's own texture slots
    /// say Actors\Character\Male\MaleHead.dds and data\Textures\Actors\...; the game adds the root itself).
    [[nodiscard]] std::string normaliseTexturePath(const std::string& stored);
}

#endif
