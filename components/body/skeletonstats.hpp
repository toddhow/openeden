#ifndef OPENEDEN_COMPONENTS_BODY_SKELETONSTATS_H
#define OPENEDEN_COMPONENTS_BODY_SKELETONSTATS_H

// OpenEden AdvancedBodies, skeleton mapping (task 0070, stage a): the aggregate measurement of real extended skeletons
// for the read-only tool apps/edenskelstat. Pure functions over plain values: a source skeleton (RestBone, metres), the
// Morrowind base skeleton with its role table, and the skin weight masses of the body sets that use the skeleton. The
// NIF reading is components/bodynif/skeletonread.hpp; nothing here knows a file format or a game.
//
// WHAT IS MEASURED, per skeleton (definitions; the research note quotes them):
//   bones, depth (root 0), leaves, height (z extent of the world rest positions);
//   the base set: the bones whose boneComparisonKey equals a key of a base skeleton bone ("in base"); the others are
//   "not in base";
//   the EXTRA bones: the bones that suggestSkeletonMap does not make a role bone (with the pack-style role table when
//   the input has one, else by tokens only); for them the token stems (boneTokenStems, counted once per bone), the
//   rest offset to the parent (length of the local translation) and the distance to the nearest role ancestor in the
//   rest pose, as quantiles;
//   suggestions: counts by (heuristic, confidence, action, kind): skeleton only by tokens, skeleton only with the
//   role table, and with each body set's masses (and the table);
//   a skeleton that fails only BODY-MAP-022 (colliding names) keeps that verdict, and its suggestions are measured on
//   a relaxed copy (SuggestionStats::mRelaxed);
//   per body set: the share of skin weight on role bones, on extra bones, and on skin bones the skeleton lacks;
//   rest differences of the roles against the base skeleton (compareRest, planRest).
// The AGGREGATE report (writeSkeletonStatsReport) prints labels, canonical role names, our heuristic ids, token stems
// and numbers only: never a bone name, a path or file content, so it may be quoted in the repository. The DETAIL
// report (writeSkeletonStatsDetail) lists every bone by name and stays in the work folder.
//
// Determinism: maps and sorted vectors only; numbers printed with fixed formats; nothing depends on the order of the
// source bones beyond their parent links (a test reorders siblings). No clock, randomness or threads.

#include "bodypackage.hpp"
#include "skeletonmap.hpp"
#include "skeletonmapping.hpp"
#include "skeletonsuggest.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Body
{
    /// The key the base-set comparison uses: boneNameTokens without the noise tokens, the first side token folded to
    /// "l" or "r" and put first, the rest joined by '_'. "Bip01 L UpperArm" and "NPC L UpperArm [LUar]" both give
    /// "l_upper_arm".
    std::string boneComparisonKey(std::string_view name);

    /// The token stems of a name for the histogram: boneNameTokens without noise tokens, side tokens folded to "l" and
    /// "r", trailing digits removed ("breast01" gives "breast"), a token of digits only becomes "#"; each stem once, in
    /// first-seen order.
    std::vector<std::string> boneTokenStems(std::string_view name);

    struct Quantiles
    {
        std::size_t mCount = 0;
        double mMin = 0;
        double mP25 = 0;
        double mMedian = 0;
        double mP75 = 0;
        double mMax = 0;
    };

    /// Nearest rank: the value at index ceil(p * n) - 1 of the sorted values (p25, p50, p75); zeros for no values.
    Quantiles quantiles(std::vector<double> values);

    struct HeuristicCount
    {
        std::string mHeuristic;
        MapConfidence mConfidence = MapConfidence::Low;
        MapAction mAction = MapAction::Ask;
        std::optional<SecondaryKind> mKind;
        std::size_t mCount = 0;
        double mShare = 0; // the share of skin weight these bones carry (0 without masses)
    };

    struct SuggestionStats
    {
        bool mValid = false; // the source skeleton passed validateSourceSkeleton
        std::vector<std::string> mInvalidRules; // the rule ids of its errors, sorted, each once
        /// The skeleton failed only BODY-MAP-022 (names used twice, or equal after normalisedBoneName): the suggestion
        /// ran on a copy in which each later bone of a colliding name has "~<n>" appended (a measurement aid only; the
        /// strict verdict above stands). mRenamed counts the renamed bones.
        bool mRelaxed = false;
        std::size_t mRenamed = 0;
        std::size_t mCoreRoles = 0; // core roles resolved
        std::size_t mOptionalRoles = 0;
        std::vector<std::string> mMissingCoreRoles; // canonical order
        std::vector<std::string> mAmbiguousRoles; // roles an ambiguity note names, canonical order
        std::size_t mNotes = 0;
        std::size_t mExtra = 0;
        std::size_t mMediumOrHigher = 0; // entries that are not "ask", with medium or high confidence
        std::size_t mAsk = 0;
        std::vector<HeuristicCount> mHeuristics; // sorted by heuristic, then confidence (high first), action, kind
    };

    struct BodySetInput
    {
        std::string mLabel;
        /// Skin weight summed over vertices per skin bone name (all shapes of all files of the set; names as in the
        /// files, merged by engineBoneKey).
        std::vector<BoneWeightMass> mMasses;
        /// Reader facts as (key, value) text, printed as given in the report. The caller must put only numbers, record
        /// type names and route names here, never a name from a file.
        std::vector<std::pair<std::string, std::string>> mFacts;
    };

    struct MassStats
    {
        std::string mLabel;
        double mTotal = 0;
        std::size_t mSkinBones = 0; // distinct skin bones with weight
        std::size_t mSkinBonesMissing = 0; // of them, not bones of the skeleton (by engineBoneKey)
        double mMissingShare = 0;
        double mRoleShare = 0;
        double mExtraShare = 0;
        std::size_t mExtraWithAnyMass = 0; // extra bones with a share above 0
        std::size_t mExtraWithMass = 0; // extra bones with a share of at least sMapWeightMassThreshold
        Quantiles mExtraMass; // over the extra bones with a share above 0
        /// Extra bones by share: 0, (0, 1e-4), [1e-4, 1e-3), [1e-3, 1e-2), at least 1e-2.
        std::array<std::size_t, 5> mExtraMassBins{};
        double mShareUnderRole = 0; // on extra bones whose parent is a role bone
        double mShareUnderRoleZeroOffset = 0; // of that, on the ones at their parent's position (sZeroOffsetM)
        /// The weight on extra bones by the rest distance to their nearest role ancestor: below 0.01 m, below 0.05 m,
        /// below 0.15 m, 0.15 m or more, no role ancestor.
        std::array<double, 5> mShareByAncestorDistance{};
        SuggestionStats mSuggestion; // suggestSkeletonMap with these masses (and the role table when there is one)
        std::vector<std::pair<std::string, std::string>> mFacts;
    };

    struct BoneDetail
    {
        std::string mName;
        std::string mParent; // "" for the root
        std::size_t mDepth = 0;
        std::string mRole; // the role it resolved to, or ""
        bool mInBase = false;
        double mParentOffset = 0;
        double mAncestorDistance = -1; // -1: no role ancestor
        std::vector<double> mShares; // per body set
        /// "action/kind heuristic confidence" or "role <Role>": tokens only, with the role table (when there is one),
        /// then per body set.
        std::vector<std::string> mSuggestions;
    };

    struct SkeletonStatInput
    {
        std::string mLabel;
        std::string mBaseLabel;
        std::vector<RestBone> mSkeleton; // metres; parents before children
        std::vector<RestBone> mBase; // the Morrowind base skeleton, metres
        std::vector<RoleBinding> mBaseRoles; // the base skeleton's role table (humanoid-to-eden)
        /// A pack-style role table for this skeleton (may be empty): when given, the suggestion runs both with tokens
        /// only and with this table, and the table's map defines the extra bones, the body-set suggestions and the
        /// rest comparison.
        std::vector<RoleBinding> mRoles;
        /// Per source bone (may be empty): its rest values are not trustworthy (a scale other than 1, a rotation that
        /// is not orthonormal) and were not carried into the RestBone.
        std::vector<bool> mUntrusted;
        std::vector<BodySetInput> mBodySets;
        std::vector<std::pair<std::string, std::string>> mFacts; // reader facts, as for BodySetInput
    };

    /// An extra bone closer than this to its parent (metres) is "at its parent's position": 0.1 mm.
    inline constexpr double sZeroOffsetM = 1e-4;

    struct SkeletonStats
    {
        std::string mLabel;
        std::string mBaseLabel;
        std::vector<std::pair<std::string, std::string>> mFacts;
        std::size_t mBones = 0;
        std::size_t mMaxDepth = 0;
        std::size_t mLeaves = 0;
        double mHeight = 0;
        std::size_t mBaseBones = 0;
        std::size_t mInBase = 0;
        std::size_t mNotInBase = 0;
        std::size_t mExactKeyDuplicates = 0; // bones whose engineBoneKey an earlier bone has
        std::size_t mNormalisedOnlyCollisions = 0; // bones whose normalisedBoneName (not engineBoneKey) collides
        std::size_t mExtra = 0;
        std::size_t mExtraUnderRole = 0; // extra bones whose parent is a role bone
        std::size_t mExtraZeroOffset = 0; // extra bones closer than sZeroOffsetM to their parent
        std::size_t mExtraOnRoleChain = 0; // extra bones with a role bone above and below them (between role bones)
        std::size_t mUntrustedOnRolePath = 0; // untrusted bones that are role bones or ancestors of one
        std::vector<std::pair<std::string, std::size_t>> mTokens; // extra bones' stems: count descending, then stem
        Quantiles mParentOffset;
        Quantiles mAncestorDistance;
        SuggestionStats mSuggestion; // skeleton only, tokens only
        bool mHasRoleTable = false;
        SuggestionStats mSuggestionWithTable; // skeleton only, with the role table (when there is one)
        std::vector<MassStats> mBodySets;
        std::vector<RoleRestDifference> mRest; // against the base
        std::size_t mRestErrors = 0;
        bool mRestValid = false;
        bool mNeedsInverseBind = false;
        std::vector<BoneDetail> mDetail; // every bone, in source order
    };

    SkeletonStats computeSkeletonStats(const SkeletonStatInput& input);

    struct RestPairStats
    {
        std::string mFirst;
        std::string mSecond;
        std::vector<RoleRestDifference> mRest; // the first's roles against the second's
        std::size_t mErrors = 0;
        bool mValid = false;
        bool mNeedsInverseBind = false;
        bool mRelaxed = false; // either skeleton was measured on its relaxed copy
    };

    /// The rest differences of the roles of two source skeletons, each resolved by suggestSkeletonMap (with its role
    /// table when given, else tokens only; on the relaxed copy when only names collide).
    RestPairStats compareSkeletonRests(const std::string& firstLabel, const std::vector<RestBone>& first,
        const std::string& secondLabel, const std::vector<RestBone>& second,
        const std::vector<RoleBinding>& firstRoles = {}, const std::vector<RoleBinding>& secondRoles = {});

    /// The aggregate report: a summary table, then one section per skeleton and per pair. Labels, role names,
    /// heuristic ids, stems and numbers only.
    std::string writeSkeletonStatsReport(
        const std::vector<SkeletonStats>& skeletons, const std::vector<RestPairStats>& pairs);

    /// Every bone by name with its numbers. For the work folder only: it names the bones of the files read.
    std::string writeSkeletonStatsDetail(const std::vector<SkeletonStats>& skeletons);

    /// The manifest of a run: tab-separated lines, '#' starts a comment line, blank lines are ignored.
    ///   base      <label> <path>
    ///   skeleton  <label> <base label> <path>
    ///   bodyset   <label> <skeleton label> <path>     (one line per file; lines with the same label add files)
    ///   compare   <skeleton label> <skeleton label>
    ///   role      <skeleton label> <canonical role> <bone name>   (a pack-style role table, one line per role)
    /// Labels are 1 to 64 characters of [A-Za-z0-9._-]. Order is kept.
    struct SkelStatManifest
    {
        struct Base
        {
            std::string mLabel;
            std::string mPath;
        };
        struct Skeleton
        {
            std::string mLabel;
            std::string mBase;
            std::string mPath;
        };
        struct BodySet
        {
            std::string mLabel;
            std::string mSkeleton;
            std::vector<std::string> mPaths;
        };
        struct Role
        {
            std::string mSkeleton;
            std::string mRole;
            std::string mBone;
        };
        std::vector<Base> mBases;
        std::vector<Skeleton> mSkeletons;
        std::vector<BodySet> mBodySets;
        std::vector<std::pair<std::string, std::string>> mCompares;
        std::vector<Role> mRoles;
    };

    struct SkelStatManifestResult
    {
        std::optional<SkelStatManifest> mValue;
        std::string mError; // "line N: ..." when mValue is empty
    };

    SkelStatManifestResult parseSkelStatManifest(std::string_view text);
}

#endif
