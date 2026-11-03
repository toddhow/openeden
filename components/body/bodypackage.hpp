#ifndef OPENEDEN_COMPONENTS_BODY_BODYPACKAGE_H
#define OPENEDEN_COMPONENTS_BODY_BODYPACKAGE_H

// OpenEden AdvancedBodies, stage B1 (task 0063): the body package as plain data. The JSON form ("openeden.body" v1,
// file body.json) is read and written by bodyjson.hpp; the rules a package must satisfy are in bodyvalidate.hpp.
//
// Every optional part carries an Origin: authored (given by the package), derived (worked out by the engine, B2 on)
// or missing. In B1 nothing is derived by the engine, but a package may already say so, and then it must say how
// (a ProvenanceRecord).
//
// Ownership: a BodyPackage is a value type. Once loaded it is shared read-only as
// std::shared_ptr<const BodyPackage> (BodyPackageHandle), because every actor using the package reads the same
// immutable data.

#include "bodycontract.hpp"

#include <opas/anim/SkeletonProfile.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Body
{
    enum class Origin
    {
        Authored,
        Derived,
        Missing,
    };

    enum class MeshFormat
    {
        Nif,
        Gltf,
        Summary,
    };

    enum class MeshRole
    {
        Skin,
        Head,
        Hair,
        GarmentBase,
        Other,
    };

    struct ContractVersion
    {
        int mMajor = sContractMajor;
        int mMinor = sContractMinor;
        friend bool operator==(const ContractVersion&, const ContractVersion&) = default;
    };

    struct Identity
    {
        std::string mId; // lowercase letters, digits, '_', '.', '-'; at most 64 bytes
        std::string mName;
        std::string mVersion; // the package's own version, "major.minor.patch"
        std::string mAuthor;
        std::string mLicense; // SPDX id or free text
        ContractVersion mContractVersion;
        AgeClass mAgeClass = AgeClass::Unspecified;
        friend bool operator==(const Identity&, const Identity&) = default;
    };

    /// What a package replaces (used by B5; B1 checks the shape only).
    struct Target
    {
        std::optional<std::string> mRace;
        std::optional<std::string> mSex;
        std::optional<std::string> mCreatureType;
        int mPriority = 0;
        friend bool operator==(const Target&, const Target&) = default;
    };

    /// One bone of the rest skeleton (optional in the binding), in the parent's frame. Rotation is [x, y, z, w].
    struct RestBone
    {
        std::string mName;
        int mParent = -1; // index into the list; -1 for a root; a parent comes before its children
        std::array<double, 3> mTranslation{};
        std::array<double, 4> mRotation{ 0, 0, 0, 1 };
        friend bool operator==(const RestBone&, const RestBone&) = default;
    };

    struct Binding
    {
        /// Exactly one of the two is set: a path to an opas.skeletonprofile file, or the profile inline.
        std::optional<std::string> mSkeletonPath;
        std::optional<opas::anim::SkeletonProfile> mSkeletonProfile;
        std::string mRootBone; // the animation root: nothing above it may rotate
        std::vector<std::string> mUnmappedBones; // bones the profile does not name
        /// The rest pose of the bound skeleton, when the package carries it. Without it the checks of
        /// CreativeMotion's skeleton assumptions are skipped with a warning (BODY-BIND-014).
        std::optional<std::vector<RestBone>> mRestSkeleton;
        friend bool operator==(const Binding&, const Binding&) = default;
    };

    struct MeshEntry
    {
        std::string mId;
        std::string mPath; // VFS path inside the game, or relative to the package folder
        MeshFormat mFormat = MeshFormat::Nif;
        int mLod = 0; // 0 is the highest detail
        MeshRole mRole = MeshRole::Skin;
        std::optional<std::string> mPartType; // a game-neutral string such as "neck" or "rhand"
        std::optional<std::string> mAttachBone;
        std::optional<MeshSummary> mSummary; // inline summary (tests, precomputed data)
        friend bool operator==(const MeshEntry&, const MeshEntry&) = default;
    };

    struct MorphTarget
    {
        std::string mMesh; // a mesh id
        std::vector<std::uint32_t> mVertices; // strictly increasing vertex indices
        std::vector<std::array<double, 3>> mDeltas; // one per vertex above, metres
        friend bool operator==(const MorphTarget&, const MorphTarget&) = default;
    };

    struct Morph
    {
        std::string mId;
        double mRangeMin = 0;
        double mRangeMax = 1;
        double mDefault = 0;
        std::vector<MorphTarget> mTargets;
        friend bool operator==(const Morph&, const Morph&) = default;
    };

    struct MorphSet
    {
        Origin mOrigin = Origin::Authored;
        std::vector<Morph> mMorphs;
        friend bool operator==(const MorphSet&, const MorphSet&) = default;
    };

    struct ProxyCapsule
    {
        std::string mBone;
        std::array<double, 3> mFrom{}; // bone-local, metres
        std::array<double, 3> mTo{};
        double mRadius = 0;
        friend bool operator==(const ProxyCapsule&, const ProxyCapsule&) = default;
    };

    struct Proxy
    {
        Origin mOrigin = Origin::Authored;
        std::vector<ProxyCapsule> mCapsules;
        friend bool operator==(const Proxy&, const Proxy&) = default;
    };

    enum class ShapeKind
    {
        Sphere, // mSize[0] is the radius
        Capsule, // mSize[0] radius, mSize[1] length of the cylinder part
        Box, // mSize is the full extent
    };

    struct PhysicsShape
    {
        std::string mBone;
        ShapeKind mKind = ShapeKind::Capsule;
        std::array<double, 3> mSize{};
        friend bool operator==(const PhysicsShape&, const PhysicsShape&) = default;
    };

    struct PhysicsMass
    {
        std::string mBone;
        double mKilograms = 0;
        friend bool operator==(const PhysicsMass&, const PhysicsMass&) = default;
    };

    struct JointLimit
    {
        std::string mBone;
        double mMin = 0; // radians
        double mMax = 0;
        double mRest = 0; // the rest pose value, must lie within [mMin, mMax]
        friend bool operator==(const JointLimit&, const JointLimit&) = default;
    };

    struct PhysicsProfile
    {
        Origin mOrigin = Origin::Authored;
        std::optional<std::string> mRigPath; // a CreativeMotion rig file
        std::optional<std::string> mProfilePath; // a CreativeMotion profile file
        std::vector<PhysicsShape> mShapes;
        std::vector<PhysicsMass> mMasses;
        std::vector<JointLimit> mJointLimits;
        /// Reserved for B9: enables a restricted tier. Allowed only with AgeClass::Adult (BODY-AGE-001).
        bool mRestrictedTier = false;
        friend bool operator==(const PhysicsProfile&, const PhysicsProfile&) = default;
    };

    struct Measurement
    {
        std::string mName;
        double mValue = 0; // metres, or kilograms for names ending in "mass"
        friend bool operator==(const Measurement&, const Measurement&) = default;
    };

    struct Measurements
    {
        Origin mOrigin = Origin::Authored;
        std::vector<Measurement> mEntries;
        friend bool operator==(const Measurements&, const Measurements&) = default;
    };

    /// Triangles [mBegin, mEnd) of the region map's mesh.
    struct TriangleRange
    {
        std::uint32_t mBegin = 0;
        std::uint32_t mEnd = 0;
        friend bool operator==(const TriangleRange&, const TriangleRange&) = default;
    };

    struct Region
    {
        std::string mId;
        std::vector<TriangleRange> mRanges;
        friend bool operator==(const Region&, const Region&) = default;
    };

    struct RegionMap
    {
        Origin mOrigin = Origin::Authored;
        std::string mMesh; // a mesh id; every triangle of it must lie in exactly one region
        std::vector<Region> mRegions;
        friend bool operator==(const RegionMap&, const RegionMap&) = default;
    };

    struct Landmark
    {
        std::string mName;
        std::string mBone;
        std::array<double, 3> mOffset{}; // bone-local, metres
        friend bool operator==(const Landmark&, const Landmark&) = default;
    };

    struct Landmarks
    {
        Origin mOrigin = Origin::Authored;
        std::vector<Landmark> mEntries;
        friend bool operator==(const Landmarks&, const Landmarks&) = default;
    };

    struct SoftTissueNode
    {
        std::string mId;
        std::string mBone;
        double mMass = 0;
        double mStiffness = 0;
        double mDamping = 0;
        friend bool operator==(const SoftTissueNode&, const SoftTissueNode&) = default;
    };

    struct SoftTissue
    {
        Origin mOrigin = Origin::Authored;
        std::vector<SoftTissueNode> mNodes;
        /// Reserved for B9, as in PhysicsProfile.
        bool mRestrictedTier = false;
        friend bool operator==(const SoftTissue&, const SoftTissue&) = default;
    };

    /// How a derived part was made (the archive design's provenance record; B2 fills it).
    struct ProvenanceRecord
    {
        std::string
            mPart; // "morphSet", "proxy", "physicsProfile", "measurements", "regionMap", "landmarks", "softTissue"
        std::string mSourceDigest; // lowercase hex
        std::string mToolId;
        std::string mToolVersion;
        std::string mParameters;
        friend bool operator==(const ProvenanceRecord&, const ProvenanceRecord&) = default;
    };

    struct BodyPackage
    {
        Identity mIdentity;
        std::vector<Target> mTargets;
        Binding mBinding;
        std::vector<MeshEntry> mMeshes;
        std::optional<MorphSet> mMorphSet;
        std::optional<Proxy> mProxy;
        std::optional<PhysicsProfile> mPhysicsProfile;
        std::optional<Measurements> mMeasurements;
        std::optional<RegionMap> mRegionMap;
        std::optional<Landmarks> mLandmarks;
        std::optional<SoftTissue> mSoftTissue;
        std::vector<ProvenanceRecord> mProvenance;
        friend bool operator==(const BodyPackage&, const BodyPackage&) = default;
    };

    /// The package handle other systems keep: immutable and shared by every actor that uses the package.
    using BodyPackageHandle = std::shared_ptr<const BodyPackage>;

    /// Enum names as they appear in the JSON ("authored", "nif", "skin", "adult", "capsule", ...).
    std::string_view toString(Origin value) noexcept;
    std::string_view toString(MeshFormat value) noexcept;
    std::string_view toString(MeshRole value) noexcept;
    std::string_view toString(AgeClass value) noexcept;
    std::string_view toString(ShapeKind value) noexcept;

    /// A package-relative or VFS path is safe when it is non-empty, at most 512 bytes, relative, uses only '/'
    /// as separator, has no empty, "." or ".." component, no control characters and no ':' or '\\'. One function,
    /// used by the loader and the validator.
    bool isSafePackagePath(std::string_view path) noexcept;

    /// Ids (package, mesh, morph...) are 1 to 64 bytes of lowercase letters, digits, '_', '.', '-'.
    bool isValidPackageId(std::string_view id) noexcept;
}

#endif
