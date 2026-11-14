#ifndef OPENMW_LODNEUTRALOBJECTS_H
#define OPENMW_LODNEUTRALOBJECTS_H

// Task 0133 stage 2 (milestone M-S2): the part of the far object cluster bake that is specific to a game pack's
// neutral world (Skyrim SE). The bake itself (apps/openmw/lodobjectbaker.cpp) is the Morrowind one; this file holds the
// pure decisions the neutral branch adds, so that tests can pin them without a game:
//  - which placed objects may stand in a far cluster (statics and trees; everything with a use or a motion stays live),
//  - the placement's transform as the row-major 3 x 4 matrix the cluster builder takes,
//  - a Skyrim model (NifScene::Flat::FlatModel) as the Lod::FlattenedModel the builder takes, with the same foliage
//    rule Morrowind's flattener applies (Lod::flattenNifModel).
// Host layer (it names both the formats and the LOD library). No VSG, no file, no clock.

#include <array>
#include <string>
#include <string_view>

#include <openeden/game/world_model.hpp>

#include <components/lod/nifflatten.hpp>
#include <components/nifscene/flatten.hpp>

namespace OMW::NeutralLod
{
    /// Kinds whose placed objects may be baked into a far cluster: Static and Tree. (Doors, activators, furniture,
    /// containers, flora, lights, items, actors and movable statics are used, moved or owned by the game and stay
    /// live.)
    [[nodiscard]] bool isFarStaticKind(openeden::sdk::game::ObjectKind kind) noexcept;

    /// The rule the live path applies to hide editor markers (MWClass::Neutral::getModel): a model path starting with
    /// "marker", case-insensitive.
    [[nodiscard]] bool isMarkerModel(std::string_view modelPath) noexcept;

    /// The placement as a row-major 3 x 4 matrix, model space to world space (rows [m00 m01 m02 tx]): the rotation
    /// (the unit quaternion of the transform, active) times the uniform scale, then the position. False, with `out`
    /// untouched, for a transform the bake refuses: a coordinate not finite or beyond 1e8, a scale not finite, not
    /// above 0 or 1000 and over, a quaternion that is not finite or has a length that is not within 1e-3 of 1.
    [[nodiscard]] bool placementMatrix(
        const openeden::sdk::game::WorldTransform& transform, std::array<double, 12>& out);

    /// A model path as the data stores it ("Architecture\\Whiterun\\x.nif") as the VFS path of the mesh
    /// ("meshes/architecture/whiterun/x.nif"). Empty for an empty path.
    [[nodiscard]] std::string meshVfsPath(std::string_view modelPath);

    /// A texture path as a flat shape stores it ("textures\\architecture\\x.dds") as the VFS path
    /// ("textures/architecture/x.dds"). Empty for an empty path.
    [[nodiscard]] std::string textureVfsPath(std::string_view texturePath);

    /// The flat shapes of a Skyrim model as cluster pieces, in model space. Rules, one per shape:
    ///  - alpha test (the key's mTest): a cutout at mThreshold / 255;
    ///  - ordinary blend (source alpha, one minus source alpha) without a test: a cutout at Lod::blendCutoutThreshold
    ///    when `blendAsCutout`, the shape has a texture, the material alpha is at least 0.99 and the texture does not
    ///    look translucent (Lod::textureLooksTranslucent); otherwise the shape is left out and the model is marked
    ///    alpha blended (the classifier then keeps the whole object out);
    ///  - any other blend, the same;
    ///  - two sided: clusterFlagTwoSided;
    ///  - vertex colours: the shape's, as bytes, when it has them; opaque white otherwise.
    /// The traits: Static, the triangle count of the pieces, the radius of the sphere round the box centre that holds
    /// every vertex, and no animation, skin or particles (a flat model has none). Pieces' mTexture is the shape's
    /// diffuse path as stored (the caller makes it a VFS path with textureVfsPath).
    [[nodiscard]] Lod::FlattenedModel toFlattenedModel(const NifScene::Flat::FlatModel& model, bool blendAsCutout);
}

#endif
