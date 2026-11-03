#ifndef OPENEDEN_COMPONENTS_BODY_BODYVALIDATE_H
#define OPENEDEN_COMPONENTS_BODY_BODYVALIDATE_H

// OpenEden AdvancedBodies, stage B1 (task 0063): the one rule set that decides whether a body package is usable.
// Loaded and programmatic packages follow the same rules. A package with any Error finding is rejected (later stages
// fall back to the default body); Warnings never reject. Pure function: no clock, no randomness, no threads.
//
// THE FINDING CODES (stable strings; the path is a JSON pointer into body.json):
//   STRUCT-001 E /identity/contractVersion   package contract newer than sContractMajor.sContractMinor, or major below
//   1 STRUCT-002 E /meshes                     no meshes STRUCT-003 E <path of the id>            invalid id (package
//   id, mesh id, morph id, region id, ...) STRUCT-004 E <path of the 2nd id>        duplicate id among meshes, regions
//   and soft tissue nodes (a duplicate morph id
//                                            is MORPH-004, a duplicate landmark name LAND-001, only those codes)
//   STRUCT-005 E /meshes/N/lod               two meshes with the same (partType or id, role, lod); also lod < 0
//   STRUCT-006 E <path>                      unsafe path (isSafePackagePath) in a mesh path, rig or profile path
//   STRUCT-007 E /identity/version           not "major.minor.patch" (digits only)
//   TARGET-001 W /targets/N                  a target with no race, sex or creatureType
//   ORIGIN-001 E /<part>/origin              a part with origin "missing" that holds content
//   PROV-001   E /<part>/origin              origin "derived" without a provenance record for the part
//   PROV-002   W /provenance/N               a provenance record for a part that is not derived or not present
//   BIND-001   E /binding/skeleton           the inline/loaded profile fails validateSkeletonProfile
//   BIND-002   E /binding/rootBone           root bone is neither a profile target nor in unmappedBones
//   BIND-003   E /binding/unmappedBones/N    an unmapped bone is also a profile target, or listed twice
//   BIND-004   E /meshes/N/summary/boneNames/M  a skin bone is neither a profile target nor unmapped
//   BIND-005   W /binding/skeleton           profile given as a path and no file reader: not checked
//   BIND-006   E /binding/skeleton           profile path could not be read or parsed
//   BIND-010   E /binding/restSkeleton       the rest skeleton is invalid (opas::rig::validateSkeleton's message)
//   BIND-012   E /binding/restSkeleton/N/rotation   not a unit quaternion
//   BIND-013   E /binding/restSkeleton/N     a bone above the root bone (an ancestor of it) has a rotation
//                                            other than identity: "nothing rotating above the animation root"
//   BIND-014   W /binding/restSkeleton       no rest skeleton: CreativeMotion's assumptions not checked
//   SKIN-001   E <summary>/influences/N      weight not finite or negative
//   SKIN-002   E <summary>/influenceOffsets/V   a vertex's weights do not sum to 1 within sWeightSumTolerance
//   SKIN-003   E <summary>/influenceOffsets/V   a vertex has more than sMaxInfluencesPerVertex influences
//   SKIN-004   E <summary>/influences/N      bone index >= boneNames.size()
//   SKIN-005   E <summary>/influenceOffsets/V   a vertex of a skinned mesh (boneNames non-empty) has no influence
//   SKIN-006   E <summary>/influenceOffsets  offsets malformed (size != vertexCount + 1, decreasing, last != size)
//   GEOM-001   E <summary>/boundsMin         bounds not finite or min > max on some axis
//   GEOM-002   E /meshes/N/summary           the extent of the LOD 0 skin meshes' bounds (largest side of the union) is
//                                            outside [sMinBodyExtentM, sMaxBodyExtentM]
//   GEOM-003   E <summary>/triangleIndices/N a triangle index >= vertexCount
//   GEOM-004   E <summary>/triangleIndices   given but size != 3 * triangleCount
//   GEOM-005   E /regionMap/regions          a triangle of the region map's mesh is in no region (gap)
//   GEOM-006   E /regionMap/regions/N/ranges/M  a triangle is in two regions or twice in one (overlap)
//   GEOM-007   E /regionMap/regions/N/ranges/M  range with begin > end, or end > triangleCount
//   GEOM-008   E /regionMap/mesh             the region map names no mesh of the package
//   MESH-002   W <summary>/influenceOffsets  a skinned mesh (boneNames non-empty) has no influence data: its skin
//   weights
//                                            are not checked
//   INTERNAL-001 E ""                        the validator itself failed (out of memory and the like); never expected
//   MESH-001   W /meshes/N                   no summary available (inline or from the MeshSource): skin and geometry
//                                            rules, morph vertex indices and the region map are not checked for it
//   MORPH-001  E <target>/vertices/N         vertex index >= the mesh's vertexCount (needs a summary)
//   MORPH-002  E <target>/deltas/N           delta not finite
//   MORPH-003  E /morphSet/morphs/N/range    min > max, or default outside [min, max]
//   MORPH-004  E /morphSet/morphs/N/id       duplicate morph id (in addition to STRUCT-004? no: ONLY this code)
//   MORPH-005  E <target>/mesh               target names no mesh of the package
//   MORPH-006  E <target>/vertices           vertices not strictly increasing, or deltas.size() != vertices.size()
//   PROXY-001  E /proxy/capsules/N/radius    radius not finite or <= 0
//   PROXY-002  E /proxy/capsules/N/bone      bone is not a known bone (profile target, unmapped, or rest skeleton)
//   PHYS-001   E /physicsProfile/shapes/N/size   a size used by the kind is not finite or <= 0
//   PHYS-002   E /physicsProfile/masses/N/kilograms   mass not finite or <= 0
//   PHYS-003   E /physicsProfile/jointLimits/N   min > max, or rest outside [min, max]
//   PHYS-004   E /physicsProfile/<list>/N/bone   bone is not a known bone
//   PHYS-005   E /physicsProfile/rig|profile the referenced CreativeMotion file fails its own loader/validator
//   PHYS-010   W /physicsProfile/rig|profile referenced file present but no file reader: not checked
//   MEAS-001   E /measurements/entries/N/value   value not finite or negative; also duplicate names
//   LAND-001   E /landmarks/entries/N        bone unknown, or duplicate landmark name
//   SOFT-001   E /softTissue/nodes/N         mass/stiffness/damping not finite or negative (mass <= 0), or bone unknown
//   AGE-001    E /physicsProfile/restrictedTier or /softTissue/restrictedTier   restricted tier without ageClass
//   "adult"
// "Known bone" = a profile target, an unmapped bone or a rest skeleton bone. When the skeleton profile is a path and
// cannot be read, only the rest skeleton and unmapped bones count and bone-existence rules are skipped.
//
// Report order (deterministic): Errors before Warnings, then by path (byte order), then by code, then by message.

#include "bodypackage.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Body
{
    /// Thresholds, each with a written source in components/body/CLAUDE.md (task 0063, decision D7).
    namespace Thresholds
    {
        inline constexpr double sWeightSumTolerance = 1e-3;
        inline constexpr int sMaxInfluencesPerVertex = 8;
        inline constexpr double sMinBodyExtentM = 0.05;
        inline constexpr double sMaxBodyExtentM = 20.0;
    }

    enum class Severity
    {
        Error,
        Warning,
    };

    struct Finding
    {
        std::string mCode; // for example "BODY-SKIN-002"
        Severity mSeverity = Severity::Error;
        std::string mPath; // JSON pointer, "" for the whole document
        std::string mMessage; // plain text, one line
        friend bool operator==(const Finding&, const Finding&) = default;
    };

    struct Report
    {
        std::vector<Finding> mFindings; // sorted as described above
        /// Derived from the skeleton, not an error: true when the profile maps a pelvis and two feet (canonical source
        /// names "Pelvis", "LeftFoot", "RightFoot"). Balance and stepping apply only then.
        bool mHumanoidGait = false;
        /// Number of Error findings is zero.
        bool ok() const;
        int errorCount() const;
        int warningCount() const;
    };

    struct ValidateOptions
    {
        /// Gives summaries to meshes without an inline one. May be null.
        const MeshSource* mMeshSource = nullptr;
        /// Reads a file by the path the package names (a skeleton profile, rig or profile file). May be empty:
        /// the references are then not checked (warnings BODY-BIND-005 / BODY-PHYS-010).
        std::function<std::optional<std::string>(std::string_view path)> mReadFile;
    };

    /// Checks a package. Never throws.
    Report validateBodyPackage(const BodyPackage& package, const ValidateOptions& options = {});

    /// One finding per line, "error BODY-SKIN-002 /meshes/0/summary/...: message" (severity word, code, path, message),
    /// followed by a last line "<n> error(s), <m> warning(s), humanoidGait=true|false". Always ends with "\n".
    std::string formatReportText(const Report& report);

    /// Canonical JSON: {"findings": [{"code","severity","path","message"},..], "humanoidGait": bool,
    /// "errors": n, "warnings": m}, same layout rules as bodyjson.hpp.
    std::string formatReportJson(const Report& report);
}

#endif
