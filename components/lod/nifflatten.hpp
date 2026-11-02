#ifndef OPENMW_COMPONENTS_LOD_NIFFLATTEN_H
#define OPENMW_COMPONENTS_LOD_NIFFLATTEN_H

// OpenEden world LOD (task 0059, stage S4c). Turns a NIF model into flat triangle lists for the object cluster
// baker: one SourcePiece per piece of geometry, in the model's own space (node transforms folded in), with the
// base texture's file name and the alpha test state, and the facts the classifier needs (animated, skinned,
// particles, alpha blended). Pure code: no GPU, no VFS, no scene graph; it reads the parsed Nif records only.
//
// The texture name is the file name as the NIF stores it (no "textures/" prefix, no case change); the caller makes it
// a VFS path.

#include <components/nif/node.hpp>

#include "clusterbuild.hpp"

namespace Lod
{
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
    /// alpha) but not alpha tested (Morrowind's foliage and many trees) becomes alpha tested at 0.5 instead of being
    /// left out: a merged mesh cannot be sorted for blending, and leaving the blended models out made most trees vanish
    /// from the far field.
    FlattenedModel flattenNifModel(const Nif::NiAVObject& root, bool blendAsCutout = false);
}

#endif
