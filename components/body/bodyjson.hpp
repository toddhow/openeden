#ifndef OPENEDEN_COMPONENTS_BODY_BODYJSON_H
#define OPENEDEN_COMPONENTS_BODY_BODYJSON_H

// OpenEden AdvancedBodies, stage B1 (task 0063): the "openeden.body" v1 reader and canonical writer.
//
// THE FORMAT (body.json). Top-level keys, in this order (the writer emits them in this order, omits absent optional
// ones, and the reader refuses any other key: decision D3 of the task, strict in v1):
//   format "openeden.body", version 1, identity, targets?, binding, meshes, morphSet?, proxy?, physicsProfile?,
//   measurements?, regionMap?, landmarks?, softTissue?, provenance?
// Required: format, version, identity, binding, meshes (the array may be empty; the VALIDATOR then complains).
//
//   identity:   {"id","name","version","author","license","contractVersion":{"major","minor"},"ageClass"}
//               ageClass is optional on read (default "unspecified") and always written; "unspecified"|"minor"|"adult".
//   targets:    [{"race"?,"sex"?,"creatureType"?,"priority"}]            (priority is an integer, default 0 on read)
//   binding:    {"skeleton": <string path> | <inline opas.skeletonprofile object>, "rootBone", "unmappedBones": [..],
//                "restSkeleton"?: [{"name","parent","translation":[x,y,z],"rotation":[x,y,z,w]}]}
//               unmappedBones is optional on read (default empty), always written.
//   meshes: [{"id","path","meshFormat":"nif"|"gltf"|"summary","lod","role":"skin"|"head"|"hair"|"garmentBase"|"other",
//                 "partType"?,"attachBone"?,"summary"?}]
//   summary:    {"vertexCount","triangleCount","boundsMin":[x,y,z],"boundsMax":[x,y,z],"boneNames":[..],
//                "influenceOffsets":[..],"influences":[[bone,weight],..],"triangleIndices":[..],"uvSetCount"}
//   morphSet:   {"origin","morphs":[{"id","range":[min,max],"default","targets":[{"mesh","vertices":[..],
//                "deltas":[[x,y,z],..]}]}]}
//   proxy:      {"origin","capsules":[{"bone","from":[x,y,z],"to":[x,y,z],"radius"}]}
//   physicsProfile: {"origin","rig"?,"profile"?,"shapes":[{"bone","kind":"sphere"|"capsule"|"box","size":[a,b,c]}],
//                "masses":[{"bone","kilograms"}],"jointLimits":[{"bone","min","max","rest"}],"restrictedTier"}
//   measurements: {"origin","entries":[{"name","value"}]}
//   regionMap:  {"origin","mesh","regions":[{"id","ranges":[[begin,end],..]}]}
//   landmarks:  {"origin","entries":[{"name","bone","offset":[x,y,z]}]}
//   softTissue: {"origin","nodes":[{"id","bone","mass","stiffness","damping"}],"restrictedTier"}
//   provenance: [{"part","sourceDigest","toolId","toolVersion","parameters"}]
//   "origin" is "authored"|"derived"|"missing". Inside an optional part every key other than "origin" except the
//   ones marked optional (?) is required on read, except the arrays and "restrictedTier", which default to empty
//   and false; the writer always writes them. A part with origin "missing" is written as {"origin": "missing"}
//   plus its (empty) arrays, nothing else.
//   Keys inside objects are written in the order listed above. Unknown keys are an error everywhere.
//
// CANONICAL TEXT (write(read(canonical)) is byte-identical):
//   * two-space indent, "\n" line ends, one trailing newline, UTF-8;
//   * scalars: numbers by opas::format::detail::number (shortest text that reads back to the same double, negative
//     zero as 0), strings by jsonString, booleans true/false; integers (version, lod, counts, indices, priority)
//     are written as integers;
//   * an array that holds only scalars is written on one line: [1, 2, 3] (", " between elements), [] when empty;
//   * an object whose values are all scalars or such flat arrays is a "leaf object", written on one line:
//     {"a": 1, "b": [1, 2]}; {} when empty;
//   * every other array or object is expanded: one element or member per line, indented by two more spaces, the
//     closing bracket on its own line at the parent's indent; members are written as "key": value.
//   See apps/components_tests/body/data/humanoid_min.body.json for a complete example of canonical text.
//
// LOCATED ERRORS: every failure returns a LoadError with the file name as passed, the 1-based line of the offending
// value (1 when unknown), the JSON pointer ("/meshes/0/lod") and a one-line message. The reader never throws,
// never crashes and never hangs on any input. It checks types, finiteness, enum names, integer ranges, id and path
// syntax (isValidPackageId, isSafePackagePath), array and string size limits (below), nesting depth, and nothing
// else: every semantic rule is the validator's.

#include "bodypackage.hpp"

#include <opas/format/LoadError.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Body
{
    /// Caps on untrusted input (task 0063, "Untrusted input"). Exceeding one is a located error.
    namespace Limits
    {
        inline constexpr std::size_t sMaxFileBytes = 64u * 1024 * 1024;
        inline constexpr std::size_t sMaxNesting = 32; // JSON nesting depth
        inline constexpr std::size_t sMaxStringBytes = 4096;
        inline constexpr std::size_t sMaxMeshes = 64;
        inline constexpr std::size_t sMaxBones = 1024; // restSkeleton, boneNames, unmappedBones, profile bones
        inline constexpr std::size_t sMaxListEntries = 4096; // morphs, capsules, shapes, regions, landmarks, ...
        inline constexpr std::size_t sMaxArrayElements = 4u * 1024 * 1024; // any single numeric array
        inline constexpr std::size_t sMaxTotalElements = 16u * 1024 * 1024; // all numeric arrays of one file
        inline constexpr std::size_t sMaxArraysInFile
            = 2u * 1024 * 1024; // all "[" of one file (influence pairs, deltas)
        inline constexpr std::size_t sMaxObjectsInFile = 65536; // all "{" of one file
        inline constexpr std::size_t sMaxRegionRanges = 262144; // all region ranges of one file
        inline constexpr std::size_t sMaxWeldGroups = 65536; // weld groups of one file (task 0069)
        inline constexpr std::size_t sMaxWeldMembers = 1u << 20; // vertex indices in all weld groups of one file
    }

    struct BodyLoadResult
    {
        std::optional<BodyPackage> mValue;
        opas::format::LoadError mError; // meaningful only when mValue is empty
    };

    /// Parses body.json text. @param fileName only appears in errors. Never throws.
    BodyLoadResult readBodyPackage(std::string_view text, std::string_view fileName);

    /// Reads a file (size-capped by Limits::sMaxFileBytes). A file that cannot be read is a LoadError with line 0.
    BodyLoadResult readBodyPackageFile(const std::filesystem::path& path);

    /// The canonical text of a package. Writes whatever the package holds; it does not validate.
    std::string writeBodyPackage(const BodyPackage& package);
}

#endif
