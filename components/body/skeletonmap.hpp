#ifndef OPENEDEN_COMPONENTS_BODY_SKELETONMAP_H
#define OPENEDEN_COMPONENTS_BODY_SKELETONMAP_H

// OpenEden AdvancedBodies, skeleton mapping (task 0070, stage b): the "openeden.skeletonmap" v1 file, its reader, its
// canonical writer, its validator and the digest that ties a map to the skeleton it was made for.
//
// WHAT IT IS. Body replacers come with extended skeletons (extra soft-tissue, twist, cloth, attach and helper bones).
// OpenEden never adopts such a skeleton (decision of toddhow, 2026-11-03): the actor keeps its own skeleton, and the
// map says, for every bone of the source skeleton, either "this is the role X of OUR canonical skeleton" or what to do
// with it (secondary, fold, attach, drop). A compatibility pack SHIPS the map for its game's one extended skeleton; for
// a skeleton without a shipped map the suggestion engine (skeletonsuggest.hpp) writes one that a person must accept.
//
// THE FORMAT (*.skeletonmap.json). Top-level keys in this order (the reader refuses any other key):
//   format "openeden.skeletonmap", version 1, name, source, profile, roles, extra, proportions
//   source:      {"boneCount": N, "digest": "<64 lower-case hex>"}   digest = skeletonDigest() of the source skeleton
//   profile:     the canonical profile this map targets, e.g. "humanoid-to-eden" (a non-empty short string)
//   roles:       [{"role", "bone", "aliases"?}]   role is a canonical role (canonicalRoles()); bone is the source bone;
//                aliases are other names that bone has in other skeletons (at most 16, may not exist in the source)
//   extra:       [{"bone", "action", ...}] one entry for every source bone that is not a role bone. action is one of
//                "secondary" | "fold" | "attach" | "drop" | "ask". The other keys, in this order, each only where
//                the action allows it:
//                  parent        secondary, attach: a role name or the name of a secondary bone (its parent)
//                  kind          secondary: breast | belly | butt | cloth | hair | tail | twist | other
//                  useClipTrack  secondary: true to let a clip drive it (default false: it stays at rest)
//                  rule          fold: "nearest-mapped-ancestor" (default) | "named" | "distribute"
//                  to            fold with rule named or distribute: a role name or a mapped bone
//                  toSecond      fold with rule distribute: the second target
//                  attachPoint   attach: an engine attach point name such as "weapon.back"
//                  reason        drop: why (a drop WITH weights is a warning)
//                  suggested     {"heuristic", "confidence": low|medium|high}   written by the suggestion engine
//                  accepted      true or false; REQUIRED. A map with an entry that is not accepted, or whose action is
//                                "ask", is refused by the consumer (skeletonmapping.hpp) and by the validator.
//   proportions: {"mode": "tier1" | "inverse-bind", "note"?}  how rest differences are handled (task 0070, D6)
// CANONICAL TEXT is the one of bodyjson.hpp: two-space indent, "\n", one trailing newline, a leaf object (only scalars
// and flat arrays) on one line, everything else expanded, optional keys omitted when empty or default. write(read(x))
// is byte-identical for canonical x, and read(write(m)) == m.
//
// LIMITS (named constants, the sources in skeletonmap.cpp): a hostile file cannot make the reader recurse or allocate.
// Every failure is a LoadError (file as passed, 1-based line, JSON pointer, message). The reader never throws.
// The validator's rules have the ids BODY-MAP-001 and up (documented at validateSkeletonMap).
//
// Ownership: all results are values. No clock, randomness or threads.

#include "bodypackage.hpp"

#include <opas/format/LoadError.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Body
{
    namespace SkeletonMapLimits
    {
        /// A map is a list of bone names; a 1 MiB file holds the 1024 bones of the cap at 1 KiB each.
        inline constexpr std::size_t sMaxFileBytes = 1u << 20;
        /// The same cap as the body package's rest skeleton (Limits::sMaxBones); the largest extended skeleton known
        /// has about 200 bones (research note 0033, to be measured in stage a).
        inline constexpr std::size_t sMaxBones = 1024;
        /// A bone name is a short functional identifier.
        inline constexpr std::size_t sMaxNameBytes = 128;
        /// Names of the profile, the map and the notes.
        inline constexpr std::size_t sMaxTextBytes = 256;
        /// Hierarchy depth of the source skeleton and of parent chains inside the map.
        inline constexpr std::size_t sMaxDepth = 64;
        inline constexpr std::size_t sMaxAliasesPerRole = 16;
        /// JSON nesting: the document, a list and an entry hold at most this many levels (suggested adds one).
        inline constexpr std::size_t sMaxNesting = 6;
    }

    /// How much of the total skin weight a bone must carry (share, sum over vertices) to count as carrying weight: one
    /// hundredth of a percent. Named once for the validator's drop warning (BODY-MAP-015) and the suggestion engine.
    inline constexpr double sMapWeightMassThreshold = 1e-4;

    enum class MapAction
    {
        Secondary,
        Fold,
        Attach,
        Drop,
        Ask, // the suggestion engine could not decide; never valid in an accepted map
    };

    enum class FoldRule
    {
        NearestMappedAncestor,
        Named,
        Distribute,
    };

    enum class SecondaryKind
    {
        Breast,
        Belly,
        Butt,
        Cloth,
        Hair,
        Tail,
        Twist,
        Other,
    };

    enum class MapConfidence
    {
        Low,
        Medium,
        High,
    };

    enum class ProportionsMode
    {
        Tier1,
        InverseBind,
    };

    const char* toString(MapAction value);
    const char* toString(FoldRule value);
    const char* toString(SecondaryKind value);
    const char* toString(MapConfidence value);
    const char* toString(ProportionsMode value);

    struct MapSource
    {
        std::uint32_t mBoneCount = 0;
        std::string mDigest; // 64 lower-case hex
        friend bool operator==(const MapSource&, const MapSource&) = default;
    };

    struct MapRole
    {
        std::string mRole;
        std::string mBone;
        std::vector<std::string> mAliases;
        friend bool operator==(const MapRole&, const MapRole&) = default;
    };

    struct MapSuggestion
    {
        std::string mHeuristic; // an id such as "name.soft-tissue"
        MapConfidence mConfidence = MapConfidence::Low;
        friend bool operator==(const MapSuggestion&, const MapSuggestion&) = default;
    };

    struct MapExtra
    {
        std::string mBone;
        MapAction mAction = MapAction::Drop;
        std::string mParent; // secondary, attach
        std::optional<SecondaryKind> mKind; // secondary
        bool mUseClipTrack = false; // secondary
        std::optional<FoldRule> mRule; // fold (absent means nearest-mapped-ancestor)
        std::string mTo; // fold: named, distribute
        std::string mToSecond; // fold: distribute
        std::string mAttachPoint; // attach
        std::string mReason; // drop
        std::optional<MapSuggestion> mSuggested;
        bool mAccepted = false;
        friend bool operator==(const MapExtra&, const MapExtra&) = default;
    };

    struct SkeletonMap
    {
        std::string mName;
        MapSource mSource;
        std::string mProfile;
        std::vector<MapRole> mRoles;
        std::vector<MapExtra> mExtra;
        ProportionsMode mProportions = ProportionsMode::Tier1;
        std::string mProportionsNote;
        friend bool operator==(const SkeletonMap&, const SkeletonMap&) = default;
    };

    /// One canonical role: CreativeMotion's 17 humanoid roles ("humanoid-to-eden" profile) plus the optional ones of
    /// toddhow's answer 5 (clavicles, a third spine, toes, 15 fingers per hand, a tail chain). Roles of a game's pack
    /// that are not in the list are not valid in v1.
    struct RoleInfo
    {
        std::string_view mName;
        bool mOptional;
        std::string_view mParent; // the role this one hangs from in the canonical hierarchy; "" for the root
    };

    /// Sorted: the 17 core roles first in the profile's order, then the optional ones in a fixed order. Never changes
    /// between calls.
    const std::vector<RoleInfo>& canonicalRoles();
    const RoleInfo* findRole(std::string_view name);

    /// The name as the validator compares it: ASCII letters lower-cased, every run of ' ', '_', '-', '.', ':' one '_',
    /// leading and trailing '_' removed. Two bones whose names normalise equal are one error (BODY-MAP-004).
    std::string normalisedBoneName(std::string_view name);

    /// BLAKE3 (64 lower-case hex) of the records (name, parent name, translation, rotation) of the skeleton sorted by
    /// name (ties by parent name), each field length-prefixed or in fixed-width bytes: the same set of bones gives the
    /// same digest whatever order the list is in. The rest values are hashed as the float bits of their double values
    /// rounded to float, so a re-export that changes bits below float precision does not change the digest.
    std::string skeletonDigest(const std::vector<RestBone>& skeleton);

    struct SkeletonMapResult
    {
        std::optional<SkeletonMap> mValue;
        opas::format::LoadError mError; // meaningful only when mValue is empty
    };

    /// Parses skeletonmap text. @param fileName only appears in errors. Never throws.
    SkeletonMapResult readSkeletonMap(std::string_view text, std::string_view fileName);
    /// Reads a file, size-capped by SkeletonMapLimits::sMaxFileBytes. An unreadable file is a LoadError with line 0.
    SkeletonMapResult readSkeletonMapFile(const std::filesystem::path& path);
    /// The canonical text of a map. Writes whatever the map holds; it does not validate.
    std::string writeSkeletonMap(const SkeletonMap& map);

    enum class MapSeverity
    {
        Error,
        Warning,
    };

    struct MapFinding
    {
        std::string mId; // "BODY-MAP-007"
        MapSeverity mSeverity = MapSeverity::Error;
        std::string mPointer; // JSON pointer into the map, e.g. "/extra/3/to"
        std::string mMessage;
        friend bool operator==(const MapFinding&, const MapFinding&) = default;
    };

    struct MapReport
    {
        std::vector<MapFinding> mFindings; // sorted: errors first, then pointer, id, message
        bool hasErrors() const;
        std::size_t count(std::string_view id) const;
    };

    /// How much of the total skin weight a source bone carries (sum over vertices, as a share of all weight).
    struct BoneWeightMass
    {
        std::string mBone;
        double mMass = 0;
    };

    /// The checks of a source skeleton alone: at most sMaxBones bones, names of 1 to sMaxNameBytes bytes without
    /// control characters, no name twice (also after normalisedBoneName), a parent index that comes before its child,
    /// depth at most sMaxDepth. Rule ids BODY-MAP-020 to 023.
    MapReport validateSourceSkeleton(const std::vector<RestBone>& skeleton);

    /// The semantic rules. `source` (optional) adds the checks against the skeleton the map was made for; `masses`
    /// (optional) the warnings about what a drop loses. Rules:
    ///   BODY-MAP-001 a role is not in canonicalRoles()
    ///   BODY-MAP-002 a role is listed twice
    ///   BODY-MAP-003 a bone name is used twice (role bones, aliases and extra bones together)
    ///   BODY-MAP-004 two names normalise to the same name
    ///   BODY-MAP-005 source given: a role bone or extra bone is not a bone of the source skeleton
    ///   BODY-MAP-006 source given: a source bone is neither a role bone nor listed in extra
    ///   BODY-MAP-007 an extra entry is not accepted
    ///   BODY-MAP-008 an extra entry has action "ask"
    ///   BODY-MAP-009 a field is not allowed for the entry's action, or a required one is missing
    ///   BODY-MAP-010 a parent or fold target names no role bone / secondary bone
    ///   BODY-MAP-011 a distribute fold names the same bone twice
    ///   BODY-MAP-012 parent chains of secondary or attach bones form a cycle or are deeper than sMaxDepth
    ///   BODY-MAP-013 source given: digest or bone count does not match the source skeleton
    ///   BODY-MAP-014 more roles, extra entries or aliases than the caps allow
    ///   BODY-MAP-015 (warning) masses given: a drop of a bone that carries weight (at least sMapWeightMassThreshold)
    ///   BODY-MAP-016 a text field is empty, too long or holds a control character
    ///   BODY-MAP-017 an alias is also a role bone or an extra bone, or an alias is listed twice for one role
    ///   BODY-MAP-018 (warning) a drop has no reason
    ///   BODY-MAP-019 the map names no Pelvis role
    MapReport validateSkeletonMap(const SkeletonMap& map, const std::vector<RestBone>* source = nullptr,
        const std::vector<BoneWeightMass>* masses = nullptr);
}

#endif
