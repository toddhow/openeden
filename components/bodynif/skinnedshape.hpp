#ifndef OPENEDEN_COMPONENTS_BODYNIF_SKINNEDSHAPE_H
#define OPENEDEN_COMPONENTS_BODYNIF_SKINNEDSHAPE_H

// OpenEden AdvancedBodies, merged legacy body (task 0069, stage a): reads the skinned shapes of one body part NIF
// into plain arrays. Pure code over the parsed Nif records (components/nif only): no ESM, no VFS, no scene graph,
// no GPU. The engine's own reader (components/vsgadapters/nif/nif.cpp, handleSkin) builds scene graph nodes and
// cannot be used by tools and tests; this one mirrors what it decides and says where it differs:
//   - which shapes a part uses: the same skin filter (a node whose name starts with the filter, or with "tri " plus
//     the filter, case-insensitively, starts the part; ancestors' transforms are dropped), applied only when the file
//     is skinned (Nif::NIFFile::mUseSkinning in the engine; here: the tree holds a skin instance);
//   - the influence cut: the 4 largest weights per vertex, NOT renormalised afterwards. Differs: equal weights are
//     ordered by bone index (handleSkin uses std::sort on the weight alone, so the order of equal weights is
//     unspecified there and a merger needs a total order).
//
// Positions are in the part file's own space after the node transforms from the matched node down to the shape.
// Whether the parts of a body share one bind space is a question stage a answers with numbers.
//
// Hostile input: counts are capped (ReadLimits), every index is checked, and a shape that cannot be used is left out
// with a located error in ReadResult::mErrors. Nothing here throws for a bad file. Threading: pure functions.

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <components/nif/node.hpp>

namespace BodyNif
{
    using Vec2 = std::array<float, 2>;
    using Vec3 = std::array<float, 3>;

    /// The engine keeps at most this many influences per vertex (handleSkin: maxInfluenceCount).
    inline constexpr std::size_t maxInfluences = 4;

    struct Influence
    {
        std::uint16_t mBone = 0; // index into Shape::mBones
        float mWeight = 0.f;
    };

    struct VertexSkin
    {
        std::uint8_t mCount = 0; // influences kept after the cut (0 to 4)
        std::uint8_t mBeforeCut = 0; // influences before the cut (saturates at 255)
        std::array<Influence, maxInfluences> mInfluences{}; // largest weight first, equal weights by bone index
    };

    /// A 3 x 4 matrix, row-major, [rotation and scale | translation]: v' = M3 * v + t.
    using Matrix34 = std::array<double, 12>;

    /// The skeleton's bones at rest: bone name in lower case to the node's world matrix (the node transforms from the
    /// root of the skeleton file down to the bone). The first node of a name wins.
    using RestPose = std::map<std::string, Matrix34>;

    struct Shape
    {
        std::string mName; // the shape node's name
        std::string mTexture; // the base texture file as the NIF stores it; empty when none
        bool mAlphaTest = false;
        bool mAlphaBlend = false;
        bool mSkinned = false;
        /// The node transforms from the matched node to the shape have a negative determinant (a mirrored part).
        bool mMirrored = false;
        /// Placed in the skeleton's space by its attach bone (rigid) or its bones (skinned); see Placement.
        bool mPlaced = false;
        /// The node transforms and the skin's own transform are all the identity.
        bool mIdentityTransform = true;
        std::vector<Vec3> mPositions;
        std::vector<Vec3> mNormals; // empty or one per vertex
        std::vector<Vec2> mUvs; // empty or one per vertex (the first UV set)
        std::vector<std::uint32_t> mTriangles; // three indices per triangle
        std::vector<std::string> mBones; // skinned only: bone names, in the skin instance's order
        std::vector<VertexSkin> mSkin; // skinned only: one per vertex
    };

    struct ReadLimits
    {
        std::size_t mMaxVertices = 200000; // per shape
        std::size_t mMaxTriangles = 400000; // per shape
        std::size_t mMaxBones = 256;
        std::size_t mMaxShapes = 64;
        std::size_t mMaxDepth = 64;
    };

    struct ReadResult
    {
        std::vector<Shape> mShapes;
        /// Located messages, in the order found ("shape 'Tri Left Hand': weight for vertex 900 of bone 'X' ...").
        std::vector<std::string> mErrors;
        /// The tree holds at least one skin instance: the skin filter was applied.
        bool mSkinning = false;
    };

    /// The engine's skin filter test (nif.cpp, filterMatches): the node name starts with `filter` or with "tri " +
    /// `filter`, ignoring case. An empty filter matches everything.
    bool filterMatches(std::string_view nodeName, std::string_view filter);

    /// Reads the triangle shapes under `root`. With a non-empty `skinFilter` and a skinned tree, only the subtrees the
    /// filter selects are read. Triangle strips become triangle lists (degenerate triangles are dropped).
    /// Where the shapes of a part are in the skeleton's space. A rigid (unskinned) part hangs under a named bone of the
    /// skeleton (npc.cpp, attachBonesAndNode), so its vertices are in that bone's space: with a rest pose and the bone
    /// name they are brought into the skeleton's space (the bone's world matrix times the node chain; a bone whose name
    /// contains "Left" mirrors the part in x first, as the engine does, because left and right share a file). A skinned
    /// part's vertices are in the skin's space: with a rest pose each goes to its bind pose, the weighted sum over its
    /// kept influences of (the bone's world matrix times its skin transform times the skin's own transform) applied to
    /// the stored vertex (the attach bone is not used). A bone the pose lacks is one error per shape and leaves the
    /// shape where it is. Without a rest pose nothing is placed.
    struct Placement
    {
        const RestPose* mRest = nullptr;
        std::string mBone;
    };

    /// The world matrix of every named node under `root` at rest. Never throws; a tree deeper than the limit is cut.
    RestPose readRestPose(const Nif::NiAVObject& root, const ReadLimits& limits = {});

    ReadResult readShapes(const Nif::NiAVObject& root, std::string_view skinFilter, const ReadLimits& limits = {},
        const Placement& placement = {});
}

#endif
