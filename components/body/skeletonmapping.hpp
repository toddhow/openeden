#ifndef OPENEDEN_COMPONENTS_BODY_SKELETONMAPPING_H
#define OPENEDEN_COMPONENTS_BODY_SKELETONMAPPING_H

// OpenEden AdvancedBodies, skeleton mapping (task 0070, stages b to d): the consumer of a SkeletonMap and the maths
// of applying it. Nothing here touches the engine: the loader option in the NIF skin reader and the virtual bones of
// the actor are stage e.
//
// THE CONSUMER API REFUSES. buildMapping() returns a SkeletonMapping only for a map that is valid (validateSkeletonMap
// has no error), whose source digest and bone count match the skeleton it is applied to, and whose extra entries are
// all accepted. An entry that is not accepted or has the action "ask" is a refusal listing the bones, UNLESS the caller
// sets BuildOptions::mAcceptAllSuggestions (the explicit "accept all", toddhow's answer 3): that accepts the unaccepted
// entries that have an action, never an "ask". A refused map means the stock body is drawn, with one message (task
// 0070, D7).
//
// WHAT A MAPPING IS. The actor keeps its own skeleton. Target bones are, in this order: the role bones of the actor in
// canonicalRoles() order (named by the pack's role table), then the VIRTUAL bones (secondary and attach entries) sorted
// by (depth in the map's parent chains, name). A source bone resolves to one of: a role bone (direct), a virtual bone
// (direct), a fold (its weights move to the target(s)), or a drop (its weights are discarded). The mapping is
// immutable.
//
// WEIGHT REMAP (stage d). remapVertex() applies a table to one vertex's influences (skin bone index, weight):
//   * direct: the weight goes to the target bone; fold: nearest-mapped-ancestor (the nearest ancestor in the SOURCE
//     skeleton that is a role bone or a secondary bone), named (the entry's `to`), distribute (`to` and `toSecond`, the
//     first gets d2 / (d1 + d2) of the weight, the second d1 / (d1 + d2), where d1 and d2 are the distances in the
//     source rest pose from the folded bone to each target; both zero: half and half);
//   * drop: the weight is discarded and the rest renormalised; a vertex left with no weight takes weight 1 on the
//     fallback target (each dropped bone's nearest mapped ancestor) that received the most dropped weight, summed
//     per target (ties: the lower target index), so no mass is lost;
//   * then the shared rules of bodyinfluences.hpp: duplicates add, zeros drop, the four largest are kept (ties by the
//     lower target index), renormalised in double to sum 1, written as float.
// The result does not depend on the order of the input influences, and applying an identity table to its own output
// changes nothing.
//
// REST DIFFERENCES (stage d, task 0070 D6). The preferred way is tier-1 proportions (per-bone scale and offset in the
// body's proportions so the actor's rest coincides with the source's). compareRest() measures, per role bone, the
// difference between the source skeleton's local rest and the actor's (translation delta, length ratio, rotation
// angle); planRest() says whether proportions suffice (every rotation difference under a tolerance) or the inverse-bind
// correction is needed. The correction is
//     corrected = inverseBind * sourceWorldRest * inverse(canonicalWorldRest)
// with ROW-VECTOR matrices (a point is p * M, the world matrix of a bone is local * parentWorld, a skin matrix is
// inverseBind * boneWorld): then the skin matrix of a bone at the actor's rest pose is the identity again for a mesh
// whose inverse bind was made for the source skeleton.
//
// NAMES are looked up by engineBoneKey() (the engine's case-insensitive lookup), never byte for byte.
//
// Hostile input: names, counts, depth and rest values are checked by the validator before anything is built; the
// functions do not throw (buildMapping catches std::bad_alloc and reports it). Determinism: all orders are total (no
// hash order); weights are accumulated in double. Ownership: values. Threading: pure functions; a built SkeletonMapping
// is immutable and may be shared (shared_ptr<const>).

#include "bodyinfluences.hpp"
#include "bodymerge.hpp"
#include "skeletonmap.hpp"
#include "skeletonsuggest.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Body
{
    struct BuildOptions
    {
        /// The explicit "accept all suggestions": unaccepted extra entries with an action count as accepted.
        bool mAcceptAllSuggestions = false;
        /// Every bone (node) name of the actor's skeleton, not only its role bones (review fix 3). When given:
        ///   * an extra bone of the source whose engineBoneKey names an actor bone that is NOT a role bone maps
        ///     directly onto that actor bone, whatever the map's action says (the actor's own bone is animated; a
        ///     fold would freeze it), and MappingResult::mNotes says so for every such bone;
        ///   * an extra bone whose name is the actor's bone of ANOTHER role is a refusal: it can be neither that role
        ///     nor a virtual bone of the same name.
        /// Empty: only the role table is known (the checks above are skipped).
        std::vector<std::string> mActorBones;
    };

    /// A bone the actor does not have and the mapping adds (secondary and attach entries).
    struct VirtualBone
    {
        std::string mName; // the source bone's name
        std::uint32_t mParent = 0; // target index of its parent (a role bone or an earlier virtual bone)
        MapAction mAction = MapAction::Secondary; // Secondary or Attach
        std::optional<SecondaryKind> mKind;
        bool mUseClipTrack = false;
        std::string mAttachPoint;
        /// The rest transform in the parent's frame, from the source rest pose: v_parent = R * v + t.
        std::array<double, 3> mRestTranslation{};
        std::array<double, 4> mRestRotation{ 0, 0, 0, 1 }; // x, y, z, w
    };

    /// What happens to the weight of one skin bone.
    struct RemapEntry
    {
        enum class Kind : std::uint8_t
        {
            Direct, // all weight to mTarget
            Split, // mFraction to mTarget, the rest to mTarget2
            Dropped, // discarded; mFallback receives the vertex's weight when nothing else is left
        };
        Kind mKind = Kind::Dropped;
        std::uint32_t mTarget = 0;
        std::uint32_t mTarget2 = 0;
        double mFraction = 1.0;
        std::uint32_t mFallback = 0;
        friend bool operator==(const RemapEntry&, const RemapEntry&) = default;
    };

    struct RemapTable
    {
        std::vector<RemapEntry> mEntries; // one per skin bone, in the mesh's bone list order
    };

    class SkeletonMapping
    {
    public:
        /// The actor's target bones: role bones in canonical role order, then the virtual bones.
        const std::vector<std::string>& targetBones() const { return mTargetBones; }
        const std::vector<VirtualBone>& virtualBones() const { return mVirtualBones; }
        /// The number of role bones among the target bones (they come first).
        std::size_t roleBoneCount() const { return mRoleBoneCount; }
        /// The digest of the source skeleton the mapping was built for.
        const std::string& sourceDigest() const { return mSourceDigest; }
        /// skeletonMapDigest() of the map as it was applied (after "accept all"): the part of a body cache key that
        /// changes when any decision of the map changes (review fix 4).
        const std::string& mapDigest() const { return mMapDigest; }

        /// The table for a mesh whose skin lists `skinBones` (source bone names). An error names every skin bone that
        /// has no rule in the map ("a skin bone without a rule", task 0070 D7).
        struct TableResult
        {
            std::optional<RemapTable> mValue;
            std::vector<std::string> mErrors;
        };
        TableResult remapTable(const std::vector<std::string>& skinBones) const;

        struct SourceRule
        {
            std::string mBone;
            RemapEntry mEntry;
        };
        const std::vector<SourceRule>& sourceRules() const { return mRules; }

    private:
        friend struct MappingBuilder;
        std::vector<std::string> mTargetBones;
        std::size_t mRoleBoneCount = 0;
        std::vector<VirtualBone> mVirtualBones;
        std::vector<SourceRule> mRules; // sorted by engineBoneKey of the bone name
        std::string mSourceDigest;
        std::string mMapDigest;
    };

    struct MappingResult
    {
        std::optional<SkeletonMap> mAcceptedMap; // the map after "accept all" (== the input otherwise)
        std::optional<SkeletonMapping> mValue;
        std::vector<std::string> mErrors; // why not, one line each, in a fixed order
        /// What the build decided beyond the map's words, one line each: an optional role the actor lacks folded into
        /// its canonical parent, an extra bone mapped onto the actor's bone of the same name.
        std::vector<std::string> mNotes;
    };

    /// Builds the mapping. `actorRoles` is the pack's table (role -> the actor's native bone name); every CORE role of
    /// the map must be in it. An OPTIONAL role of the map that the actor lacks (Spine2 against a 17-role actor) is not
    /// a refusal: its source bone folds into the actor's bone of the nearest canonical ancestor role the actor has,
    /// with a note (review fix 9). `source` is the skeleton the map is applied to (its digest must match the map's).
    MappingResult buildMapping(const SkeletonMap& map, const std::vector<RestBone>& source,
        const std::vector<RoleBinding>& actorRoles, const BuildOptions& options = {});

    /// Applies a table to one vertex. `skinBone` indexes the table. Influences that index no entry, or whose weight is
    /// not finite or not positive, are ignored. Allocates a small result list per call (std::bad_alloc is the only
    /// exception it can raise); load-time use only.
    CutWeights remapVertex(const RemapTable& table, const std::vector<std::pair<std::uint32_t, float>>& influences);

    // ---- Rest differences ----------------------------------------------------------------------------------------

    /// A 4 x 4 matrix, row-major, row-vector convention (a point p is multiplied as p * M).
    struct Mat4
    {
        std::array<double, 16> m{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        friend bool operator==(const Mat4&, const Mat4&) = default;
    };

    // ---- Conversions between the matrix conventions of components/body (review fix 11) ----------------------------
    // This file is ROW-VECTOR (p' = p * M, Mat4 stored row-major); the merger's Mat34 (bodymerge.hpp) is
    // COLUMN-VECTOR (v' = M3 * v + t, stored row-major); VSG's dmat4 is column-vector and stored column-major
    // (m[column][row]). The column-vector matrix is the transpose of the row-vector one, so its column-major storage
    // equals Mat4's row-major storage element for element: toColumnMajor() is a copy, and the test pins it against
    // vsg::translate * vsg::rotate. Never convert element by element or through VSG's 16-number constructor, whose
    // argument order differs (root CLAUDE.md pitfall "VSG's math reads like OSG's and is not").

    /// The merger's placement of the same transform (the affine part; Mat4's last column must be 0, 0, 0, 1).
    Mat34 toPlacement(const Mat4& matrix);
    /// The inverse of toPlacement().
    Mat4 fromPlacement(const Mat34& placement);
    /// The 16 numbers in the memory order of vsg::dmat4 (column-major storage of the column-vector matrix).
    std::array<double, 16> toColumnMajor(const Mat4& matrix);

    Mat4 multiply(const Mat4& a, const Mat4& b); // a * b: apply a first, then b
    /// The inverse of an affine matrix (last column 0, 0, 0, 1); nullopt when singular or not finite.
    std::optional<Mat4> inverse(const Mat4& matrix);
    /// local = rotation then translation as a row-vector matrix (p * R + t); rotation is x, y, z, w (normalised here).
    Mat4 restMatrix(const std::array<double, 3>& translation, const std::array<double, 4>& rotation);
    std::array<double, 3> transformPoint(const std::array<double, 3>& point, const Mat4& matrix);
    /// The world rest matrix of every bone (parents before children: world = local * parentWorld). An invalid parent
    /// index gives an empty result.
    std::vector<Mat4> worldRestMatrices(const std::vector<RestBone>& skeleton);

    /// corrected = inverseBind * sourceWorldRest * inverse(canonicalWorldRest). Nullopt when the canonical matrix is
    /// singular.
    std::optional<Mat4> correctedInverseBind(
        const Mat4& inverseBind, const Mat4& sourceWorldRest, const Mat4& canonicalWorldRest);

    struct RoleRestDifference
    {
        std::string mRole;
        double mTranslationDelta = 0; // length of (source local translation - actor local translation)
        double mLengthRatio = 1; // source local length / actor local length (1 when either is zero)
        double mRotationAngle = 0; // radians, of the rotation from the actor's local rest to the source's
    };

    struct RestComparison
    {
        std::vector<RoleRestDifference> mDifferences; // canonical role order
        /// Why the comparison cannot be trusted, one line each. Not empty when either skeleton is invalid (parents,
        /// rest values), a role of the map has no source bone, a role of the actor's table has no actor bone (looked up
        /// by engineBoneKey), a parent rest pose is singular, a value is not finite, or nothing could be compared.
        std::vector<std::string> mErrors;
    };

    /// Per role bone that both skeletons have and whose parent role bone both have: the local rest difference.
    /// It FAILS CLOSED (review fix 7): anything that prevents a comparison is an error in mErrors, never a silently
    /// skipped role. Roles the map or the actor's table do not list at all are not compared and are not errors (an
    /// optional role).
    RestComparison compareRest(const SkeletonMap& map, const std::vector<RestBone>& source,
        const std::vector<RestBone>& actor, const std::vector<RoleBinding>& actorRoles);

    struct RestPlan
    {
        std::vector<RoleRestDifference> mDifferences;
        /// False: no plan; the caller must not draw the body as if proportions sufficed (the stock body is drawn).
        bool mValid = false;
        std::vector<std::string> mErrors;
        /// False: tier-1 proportions are enough. True: some rotation difference is above the tolerance, or is not a
        /// number. Meaningful only when mValid.
        bool mNeedsInverseBindCorrection = false;
    };

    /// Named starting value: 1e-3 rad (about 0.06 degrees) is far under what skinning shows; to be tuned with stage a
    /// data.
    inline constexpr double sRestRotationTolerance = 1e-3;

    /// The plan of a comparison: invalid when it has errors or compared nothing.
    RestPlan planRest(const RestComparison& comparison, double rotationTolerance = sRestRotationTolerance);
    /// The plan of a list of differences alone: invalid when the list is empty.
    RestPlan planRest(
        const std::vector<RoleRestDifference>& differences, double rotationTolerance = sRestRotationTolerance);
}

#endif
