#ifndef OPENMW_COMPONENTS_LOD_NIFFLATTEN_H
#define OPENMW_COMPONENTS_LOD_NIFFLATTEN_H

// OpenEden world LOD (task 0059, stage S4c). Turns a NIF model into flat triangle lists for the object cluster
// baker: one SourcePiece per piece of geometry, in the model's own space (node transforms folded in), with the
// base texture's file name and the alpha test state, and the facts the classifier needs (animated, skinned,
// particles, alpha blended). Pure code: no GPU, no VFS, no scene graph; it reads the parsed Nif records only.
//
// The texture name is the file name as the NIF stores it (no "textures/" prefix, no case change); the caller makes it
// a VFS path.

#include <cstdint>
#include <string_view>

#include <components/nif/node.hpp>

#include "clusterbuild.hpp"
#include "clustereffect.hpp"

namespace Lod
{
    /// The cutoff given to a blend-only piece turned into a cutout: half the alpha range (chosen here; the NIF stores
    /// no alpha test for such a piece).
    inline constexpr std::uint8_t blendCutoutThreshold = 128;
    /// Bumped when the foliage rule (flattenNifModel's blendAsCutout, textureLooksTranslucent) changes; the baker puts
    /// it in the digest parameters so that older packs go stale.
    inline constexpr std::uint32_t foliageRuleVersion = 3;

    /// Whether a texture file name is a translucent surface (water, ice, glass, mist, smoke, rope, web, a window, a
    /// bottle, a ghost, bubbles, glows and shafts of light) that would draw wrong as a solid cutout. The base name
    /// (without folders and extension) is split into words at everything that is not a letter and lower-cased; a word
    /// matches when it equals a keyword (ray, rays, mist, fog, rope, ropes, web, webs, window, windows, bottle,
    /// bottles, ghost, bubble, bubbles, smoke, steam, glass, glow, cloud, clouds, water, ice) or begins with one of
    /// water, ice, glass, glow, smoke, steam, cloud, ghost, bubble, window, rope, fog, mist (so icelayer, waterfall,
    /// windowpane, ropebridge and fogbank match; gray, tray, array,
    /// office and notice do not).
    bool textureLooksTranslucent(std::string_view texture);

    struct FlattenedModel
    {
        std::vector<SourcePiece> mPieces;
        /// mKind is Static; the radius is that of the bounding sphere of all vertices (model units); the triangle
        /// count is the sum over pieces; the flags are set when anything in the model animates, is skinned, is a
        /// particle system, or is drawn with sorted alpha blending.
        ObjectTraits mTraits;
    };

    /// Flattens the subtree under `root` (collision nodes, hidden nodes and nodes named "Bounding Box" are skipped;
    /// geometry that is not a triangle list or strip is skipped). Never throws for a model it cannot use: the result
    /// then has no pieces.
    ///
    /// With `blendAsCutout`, geometry that is alpha blended with the ordinary blend (source alpha, one minus source
    /// alpha) but not alpha tested (Morrowind's foliage and many trees) becomes alpha tested at blendCutoutThreshold
    /// instead of being left out, unless its texture looks translucent, its material alpha is below 0.99 or it has no
    /// texture (then it is left out, and the model is marked alpha blended, as without the option). A merged mesh
    /// cannot be sorted for blending, and leaving the blended models out made most trees vanish from the far field.
    ///
    /// Controllers (task 0096): any controller on a node, a geometry or a property marks the model animated (a2: the
    /// property controllers used to be missed and such a model was baked as a rest pose). With `uvEffects`, a
    /// NiUVController on a shape that is a constant scroll (see Flattener::uvScroll) is instead turned into the
    /// ClusterEffect of that shape's pieces and does not mark the model animated, and a NiBSAnimationNode with no
    /// controller of its own no longer marks it either; every other controller still does.
    FlattenedModel flattenNifModel(const Nif::NiAVObject& root, bool blendAsCutout = false, bool uvEffects = false);
}

#endif
