#ifndef OPENEDEN_COMPONENTS_NIFSCENE_FLATTEN_HPP
#define OPENEDEN_COMPONENTS_NIFSCENE_FLATTEN_HPP

// Task 0110 (neutral world stage N3a-4): the first half of drawing a Nif::Scene (a Skyrim SE model) in the game. This
// half has no Vulkan, VSG or engine type and runs anywhere: it turns the scene into a few flat shapes that a static
// draw needs, with
//  - every node's world transform baked into the vertices (the model is static: no skinning, no controllers),
//  - the material reduced to what the engine's object shader can use (MaterialKey), and
//  - shapes with the same key merged, so a model is a few draws, not one per NiTriShape.
// Everything a hostile or odd file can do is caught here: counts are capped, non-finite or absurd numbers drop the
// triangles that use them, and nothing is ever indexed outside its array. The second half
// (apps/openmw/mwrender/skyrimmodel.hpp, host layer: it needs both this and the renderer) builds the VSG nodes from the
// result.
//
// Policy decisions, all stated by tests (apps/components_tests/nifscene/testflatten.cpp):
//  - hidden nodes (NiAVObject flag bit 0), editor markers (a name starting "EditorMarker") and their subtrees are left
//  out;
//  - skinned shapes (actors, worn armour) are left out: they need the animation system (out of scope of N3a-4);
//  - shapes with no shader property are left out (the game does not draw them either); effect shader shapes (flames,
//    glow cards, magic) are left out because they need additive blending and animation this slice does not have;
//  - vertex colours are used only when the shader says so (Vertex_Colors flag) and their alpha only with Vertex_Alpha;
//  - the shader's UV offset and scale are baked into the texture coordinates;
//  - the diffuse texture is the first slot of the shape's texture set; no normal, glow, environment or height map.

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "scene.hpp"

namespace NifScene::Flat
{
    /// Skyrim's BSShaderProperty flag bits (nif.xml SkyrimShaderPropertyFlags1/2, RECALLED-VERIFY: the double sided and
    /// vertex colour bits are checked against the installed meshes by tools/neutral_session_check's counters).
    namespace ShaderFlags
    {
        constexpr std::uint32_t sVertexAlpha = 1u << 3; // flags 1
        // Heat shimmer and glass: the Whiterun inn's fire planes carry both bits (flags 0x02418309), measured.
        constexpr std::uint32_t sRefraction = 1u << 15; // flags 1
        constexpr std::uint32_t sFireRefraction = 1u << 16; // flags 1
        constexpr std::uint32_t sDoubleSided = 1u << 4; // flags 2
        constexpr std::uint32_t sVertexColors = 1u << 5; // flags 2
    }

    struct Limits
    {
        std::size_t mMaxNodes = 100000; // nodes walked
        std::size_t mMaxShapes = 4096; // geometries drawn
        std::size_t mMaxVertices = 4000000; // vertices of all shapes together, before merging
        std::size_t mMaxTriangles = 4000000; // triangles of all shapes together
        std::size_t mMaxMergedVertices = 262144; // a merged shape never grows beyond this
        double mMaxCoordinate = 1.0e7; // a baked coordinate beyond this (or not a number) is a bad vertex
        std::size_t mMaxPathLength = 260; // a longer texture path is ignored
    };

    /// What makes two shapes drawable by one StateGroup.
    struct MaterialKey
    {
        std::string mDiffuse; // the texture set's first slot as stored ("textures\x.dds"), empty: untextured
        bool mDoubleSided = false;
        bool mVertexColors = false;
        bool mBlend = false;
        bool mTest = false;
        std::uint8_t mSourceBlend = 6; // NiAlphaProperty codes (scene.hpp)
        std::uint8_t mDestinationBlend = 7;
        std::uint8_t mTestMode = 4;
        std::uint8_t mThreshold = 128;
        bool mNoSorter = false;
        std::uint8_t mClampMode = 3; // 0 clamp/clamp, 1 clamp S wrap T, 2 wrap S clamp T, 3 wrap/wrap
        float mAlpha = 1.f;
        friend auto operator<=>(const MaterialKey&, const MaterialKey&) = default;
    };

    struct Shape
    {
        MaterialKey mKey;
        std::string mName; // the first geometry's name (diagnostics)
        std::vector<std::array<float, 3>> mPositions;
        std::vector<std::array<float, 3>> mNormals; // one per vertex, unit length
        std::vector<std::array<float, 2>> mUV; // empty, or one per vertex
        std::vector<std::array<float, 4>> mColors; // empty, or one per vertex
        std::vector<std::uint32_t> mIndices; // three per triangle, all below mPositions.size()
        std::uint32_t mMergedFrom = 1; // geometries that went into this shape
        std::uint32_t mFlags1 = 0; // the first geometry's shader flag words (diagnostics; not part of the key)
        std::uint32_t mFlags2 = 0;
    };

    /// What flatten did, for logging and tests. Counts of geometries unless said otherwise.
    struct Report
    {
        std::uint32_t mGeometries = 0; // in the scene
        std::uint32_t mDrawn = 0; // geometries that contributed at least one triangle
        std::uint32_t mSkippedHidden = 0;
        std::uint32_t mSkippedSkinned = 0;
        std::uint32_t mSkippedNoMaterial = 0;
        std::uint32_t mSkippedEffect = 0;
        std::uint32_t mSkippedNoGeometry = 0; // no vertex data, no triangles, or none left
        std::uint32_t mSkippedOverCap = 0;
        std::uint32_t mBadVertices = 0; // vertices that were not a usable number after baking
        std::uint32_t mDroppedTriangles = 0; // triangles with an index outside the shape or a bad vertex
        std::uint32_t mTexturesNamed = 0; // distinct diffuse paths
        std::uint32_t mVertices = 0; // after merging
        std::uint32_t mTriangles = 0; // after merging
        bool mNodesOverCap = false;
    };

    struct FlatModel
    {
        std::vector<Shape> mShapes;
        Report mReport;
        /// The axis-aligned bounds of all shapes (min, max); both zero when nothing is drawn.
        std::array<float, 3> mBoundsMin{ 0.f, 0.f, 0.f };
        std::array<float, 3> mBoundsMax{ 0.f, 0.f, 0.f };
    };

    /// Never throws for any scene; the result obeys the caps in `limits`.
    FlatModel flatten(const Scene& scene, const Limits& limits = {});

    /// Whether the first bytes of a node name mark an editor marker (case-insensitive prefix "EditorMarker").
    bool isEditorMarkerName(const std::string& name);
}

#endif
