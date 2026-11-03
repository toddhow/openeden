// Task 0063 (AdvancedBodies B1), stage b: the validator, its report formats and the age gate. Written from the task
// spec and the contract headers only.

#include "bodyfixtures.hpp"

#include <components/body/bodyjson.hpp>
#include <components/body/bodyvalidate.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace
{
    using namespace BodyTest;

    constexpr double sNaN = std::numeric_limits<double>::quiet_NaN();

    /// A package, the options to validate it with and the mesh source behind them.
    struct CaseSetup
    {
        explicit CaseSetup(Body::BodyPackage package)
            : mPackage(std::move(package))
        {
        }
        CaseSetup(const CaseSetup&) = delete;
        CaseSetup& operator=(const CaseSetup&) = delete;

        Body::BodyPackage mPackage;
        Body::ValidateOptions mOptions;
        FakeMeshSource mSource;
    };

    using Mutator = std::function<void(CaseSetup&)>;

    enum class Base
    {
        Humanoid,
        Quadruped,
    };

    struct Case
    {
        std::string mName;
        std::string mCode; // for example "BODY-SKIN-002"
        Body::Severity mSeverity;
        Base mBase;
        Mutator mMutate;
        std::string mPath; // exact path; when empty the prefix and suffix are checked
        std::string mPathPrefix;
        std::string mPathSuffix;
        std::vector<std::string> mAllowedExtra; // codes that may appear in addition
    };

    Body::Report run(CaseSetup& setup)
    {
        setup.mOptions.mMeshSource = &setup.mSource;
        return Body::validateBodyPackage(setup.mPackage, setup.mOptions);
    }

    std::unique_ptr<CaseSetup> makeSetup(Base base)
    {
        return std::make_unique<CaseSetup>(base == Base::Humanoid ? humanoid() : tailedQuadruped());
    }

    bool startsWith(const std::string& text, const std::string& prefix)
    {
        return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
    }

    bool endsWith(const std::string& text, const std::string& suffix)
    {
        return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    void checkCase(const Case& testCase)
    {
        SCOPED_TRACE(testCase.mName + " (" + testCase.mCode + ")");
        const std::unique_ptr<CaseSetup> setup = makeSetup(testCase.mBase);
        testCase.mMutate(*setup);
        const Body::Report report = run(*setup);
        const Body::Finding* finding = findCode(report, testCase.mCode);
        if (finding == nullptr)
        {
            ADD_FAILURE() << "no finding " << testCase.mCode << ". Report:\n" << describe(report);
            return;
        }
        EXPECT_EQ(finding->mSeverity, testCase.mSeverity) << describe(report);
        if (!testCase.mPath.empty())
        {
            EXPECT_EQ(finding->mPath, testCase.mPath) << describe(report);
        }
        else
        {
            EXPECT_TRUE(
                startsWith(finding->mPath, testCase.mPathPrefix) && endsWith(finding->mPath, testCase.mPathSuffix))
                << "path '" << finding->mPath << "' should start with '" << testCase.mPathPrefix << "' and end with '"
                << testCase.mPathSuffix << "'";
        }
        std::set<std::string> allowed(testCase.mAllowedExtra.begin(), testCase.mAllowedExtra.end());
        allowed.insert(testCase.mCode);
        for (const Body::Finding& other : report.mFindings)
        {
            EXPECT_TRUE(allowed.count(other.mCode) == 1)
                << "unexpected " << other.mCode << " " << other.mPath << ": " << other.mMessage;
            EXPECT_FALSE(other.mMessage.empty()) << other.mCode;
            EXPECT_EQ(other.mMessage.find('\n'), std::string::npos) << other.mCode;
        }
        EXPECT_EQ(report.ok(), testCase.mSeverity != Body::Severity::Error && report.errorCount() == 0);
    }

    Case errorCase(std::string name, std::string code, Base base, Mutator mutate, std::string path,
        std::vector<std::string> allowedExtra = {})
    {
        return Case{ std::move(name), std::move(code), Body::Severity::Error, base, std::move(mutate), std::move(path),
            "", "", std::move(allowedExtra) };
    }

    Case errorCaseAt(std::string name, std::string code, Base base, Mutator mutate, std::string prefix,
        std::string suffix, std::vector<std::string> allowedExtra = {})
    {
        return Case{ std::move(name), std::move(code), Body::Severity::Error, base, std::move(mutate), "",
            std::move(prefix), std::move(suffix), std::move(allowedExtra) };
    }

    Case warningCase(std::string name, std::string code, Base base, Mutator mutate, std::string path,
        std::vector<std::string> allowedExtra = {})
    {
        return Case{ std::move(name), std::move(code), Body::Severity::Warning, base, std::move(mutate),
            std::move(path), "", "", std::move(allowedExtra) };
    }

    std::vector<Case> allCases()
    {
        const Base h = Base::Humanoid;
        const Base q = Base::Quadruped;
        std::vector<Case> cases;

        // --- Structure
        cases.push_back(errorCase(
            "contract major newer", "BODY-STRUCT-001", h,
            [](CaseSetup& x) {
                x.mPackage.mIdentity.mContractVersion = { Body::sContractMajor + 1, 0 };
            },
            "/identity/contractVersion"));
        cases.push_back(errorCase(
            "contract minor newer", "BODY-STRUCT-001", h,
            [](CaseSetup& x) {
                x.mPackage.mIdentity.mContractVersion = { Body::sContractMajor, Body::sContractMinor + 1 };
            },
            "/identity/contractVersion"));
        cases.push_back(errorCase("no meshes", "BODY-STRUCT-002", h,
            [](CaseSetup& x) {
                x.mPackage.mMeshes.clear();
                x.mPackage.mMorphSet.reset();
                x.mPackage.mRegionMap.reset();
            },
            "/meshes", { "BODY-GEOM-002" }));
        cases.push_back(errorCase(
            "invalid package id", "BODY-STRUCT-003", h, [](CaseSetup& x) { x.mPackage.mIdentity.mId = "Bad Id"; },
            "/identity/id"));
        cases.push_back(errorCase(
            "empty package id", "BODY-STRUCT-003", h, [](CaseSetup& x) { x.mPackage.mIdentity.mId = ""; },
            "/identity/id"));
        cases.push_back(errorCase(
            "invalid mesh id", "BODY-STRUCT-003", h, [](CaseSetup& x) { x.mPackage.mMeshes[3].mId = "Bad Mesh"; },
            "/meshes/3/id"));
        cases.push_back(errorCase(
            "invalid morph id", "BODY-STRUCT-003", h,
            [](CaseSetup& x) { x.mPackage.mMorphSet->mMorphs[0].mId = "Bad Morph"; }, "/morphSet/morphs/0/id"));
        cases.push_back(errorCase(
            "invalid region id", "BODY-STRUCT-003", h,
            [](CaseSetup& x) { x.mPackage.mRegionMap->mRegions[0].mId = "Bad Region"; }, "/regionMap/regions/0/id"));
        cases.push_back(errorCase("duplicate mesh id", "BODY-STRUCT-004", h,
            [](CaseSetup& x) {
                Body::MeshEntry copy = x.mPackage.mMeshes[3];
                copy.mId = x.mPackage.mMeshes[0].mId;
                copy.mPartType = "hair2";
                x.mPackage.mMeshes.push_back(copy);
            },
            "/meshes/5/id", { "BODY-MORPH-005", "BODY-GEOM-008" }));
        cases.push_back(errorCase(
            "duplicate region id", "BODY-STRUCT-004", h,
            [](CaseSetup& x) { x.mPackage.mRegionMap->mRegions[1].mId = x.mPackage.mRegionMap->mRegions[0].mId; },
            "/regionMap/regions/1/id"));
        cases.push_back(errorCase(
            "duplicate soft tissue node id", "BODY-STRUCT-004", q,
            [](CaseSetup& x) { x.mPackage.mSoftTissue->mNodes.push_back(x.mPackage.mSoftTissue->mNodes[0]); },
            "/softTissue/nodes/1/id"));
        cases.push_back(errorCase(
            "two meshes same part, role and lod", "BODY-STRUCT-005", h,
            [](CaseSetup& x) { x.mPackage.mMeshes[1].mLod = 0; }, "/meshes/1/lod"));
        cases.push_back(errorCase(
            "negative lod", "BODY-STRUCT-005", h, [](CaseSetup& x) { x.mPackage.mMeshes[4].mLod = -1; },
            "/meshes/4/lod"));
        cases.push_back(errorCase(
            "unsafe mesh path (parent)", "BODY-STRUCT-006", h,
            [](CaseSetup& x) { x.mPackage.mMeshes[0].mPath = "../body.glb"; }, "/meshes/0/path"));
        cases.push_back(errorCase(
            "unsafe mesh path (absolute)", "BODY-STRUCT-006", h,
            [](CaseSetup& x) { x.mPackage.mMeshes[0].mPath = "/body.glb"; }, "/meshes/0/path"));
        cases.push_back(errorCase(
            "unsafe mesh path (backslash)", "BODY-STRUCT-006", h,
            [](CaseSetup& x) { x.mPackage.mMeshes[0].mPath = "mesh\\body.glb"; }, "/meshes/0/path"));
        cases.push_back(errorCase("unsafe rig path", "BODY-STRUCT-006", q,
            [](CaseSetup& x) { x.mPackage.mPhysicsProfile->mRigPath = "../rig.json"; }, "/physicsProfile/rig",
            { "BODY-PHYS-010", "BODY-PHYS-005" }));
        cases.push_back(errorCase("unsafe profile path", "BODY-STRUCT-006", q,
            [](CaseSetup& x) { x.mPackage.mPhysicsProfile->mProfilePath = "a/../../profile.json"; },
            "/physicsProfile/profile", { "BODY-PHYS-010", "BODY-PHYS-005" }));
        cases.push_back(errorCase(
            "version without patch", "BODY-STRUCT-007", h, [](CaseSetup& x) { x.mPackage.mIdentity.mVersion = "1.0"; },
            "/identity/version"));
        cases.push_back(errorCase(
            "version with letters", "BODY-STRUCT-007", h, [](CaseSetup& x) { x.mPackage.mIdentity.mVersion = "1.0.x"; },
            "/identity/version"));
        cases.push_back(errorCase(
            "empty version", "BODY-STRUCT-007", h, [](CaseSetup& x) { x.mPackage.mIdentity.mVersion = ""; },
            "/identity/version"));
        cases.push_back(warningCase(
            "target with nothing in it", "BODY-TARGET-001", h,
            [](CaseSetup& x) { x.mPackage.mTargets.push_back(Body::Target{}); }, "/targets/0"));

        // --- Origin and provenance
        cases.push_back(errorCase(
            "missing proxy that holds capsules", "BODY-ORIGIN-001", q,
            [](CaseSetup& x) { x.mPackage.mProxy->mOrigin = Body::Origin::Missing; }, "/proxy/origin"));
        cases.push_back(errorCase(
            "missing region map that holds regions", "BODY-ORIGIN-001", h,
            [](CaseSetup& x) { x.mPackage.mRegionMap->mOrigin = Body::Origin::Missing; }, "/regionMap/origin"));
        cases.push_back(errorCase(
            "missing measurements that hold entries", "BODY-ORIGIN-001", q,
            [](CaseSetup& x) { x.mPackage.mMeasurements->mOrigin = Body::Origin::Missing; }, "/measurements/origin"));
        cases.push_back(errorCase(
            "derived morph set without provenance", "BODY-PROV-001", h,
            [](CaseSetup& x) { x.mPackage.mMorphSet->mOrigin = Body::Origin::Derived; }, "/morphSet/origin"));
        cases.push_back(errorCase(
            "derived proxy without provenance", "BODY-PROV-001", q,
            [](CaseSetup& x) { x.mPackage.mProxy->mOrigin = Body::Origin::Derived; }, "/proxy/origin"));
        cases.push_back(warningCase(
            "provenance for a part that is not derived", "BODY-PROV-002", h,
            [](CaseSetup& x) {
                x.mPackage.mProvenance.push_back({ "proxy", std::string(64, 'a'), "tool", "1.0.0", "" });
            },
            "/provenance/0"));
        cases.push_back(warningCase(
            "provenance for a part that is absent", "BODY-PROV-002", h,
            [](CaseSetup& x) {
                x.mPackage.mProvenance.push_back({ "landmarks", std::string(64, 'a'), "tool", "1.0.0", "" });
            },
            "/provenance/0"));

        // --- Binding
        cases.push_back(errorCase(
            "profile fails its validation", "BODY-BIND-001", h,
            [](CaseSetup& x) { x.mPackage.mBinding.mSkeletonProfile->name = ""; }, "/binding/skeleton"));
        cases.push_back(errorCase("profile maps a target twice", "BODY-BIND-001", h,
            [](CaseSetup& x) {
                x.mPackage.mBinding.mSkeletonProfile->bones[2].target
                    = x.mPackage.mBinding.mSkeletonProfile->bones[1].target;
            },
            "/binding/skeleton", { "BODY-BIND-004", "BODY-PROXY-002", "BODY-BIND-002" }));
        cases.push_back(errorCase("root bone is unknown", "BODY-BIND-002", h,
            [](CaseSetup& x) { x.mPackage.mBinding.mRootBone = "Nowhere"; }, "/binding/rootBone",
            { "BODY-BIND-010", "BODY-BIND-013" }));
        cases.push_back(errorCase(
            "unmapped bone listed twice", "BODY-BIND-003", h,
            [](CaseSetup& x) { x.mPackage.mBinding.mUnmappedBones.push_back("Bip Root"); },
            "/binding/unmappedBones/1"));
        cases.push_back(errorCase(
            "unmapped bone is also a profile target", "BODY-BIND-003", h,
            [](CaseSetup& x) { x.mPackage.mBinding.mUnmappedBones.push_back("Bip Pelvis"); },
            "/binding/unmappedBones/1"));
        cases.push_back(errorCase(
            "skin uses a bone nobody mapped", "BODY-BIND-004", h,
            [](CaseSetup& x) { x.mPackage.mMeshes[0].mSummary->mBoneNames.push_back("Bip Ghost"); },
            "/meshes/0/summary/boneNames/3"));
        cases.push_back(warningCase(
            "skeleton path and no file reader", "BODY-BIND-005", h,
            [](CaseSetup& x) {
                x.mPackage.mBinding.mSkeletonProfile.reset();
                x.mPackage.mBinding.mSkeletonPath = "profiles/h.skeletonprofile.json";
            },
            "/binding/skeleton"));
        cases.push_back(errorCase(
            "skeleton path that cannot be read", "BODY-BIND-006", h,
            [](CaseSetup& x) {
                x.mPackage.mBinding.mSkeletonProfile.reset();
                x.mPackage.mBinding.mSkeletonPath = "profiles/h.skeletonprofile.json";
                x.mOptions.mReadFile = [](std::string_view) { return std::optional<std::string>(); };
            },
            "/binding/skeleton"));
        cases.push_back(errorCase(
            "skeleton path with unparsable file", "BODY-BIND-006", h,
            [](CaseSetup& x) {
                x.mPackage.mBinding.mSkeletonProfile.reset();
                x.mPackage.mBinding.mSkeletonPath = "profiles/h.skeletonprofile.json";
                x.mOptions.mReadFile = [](std::string_view) { return std::optional<std::string>("not json at all"); };
            },
            "/binding/skeleton"));
        cases.push_back(errorCase("rest skeleton with a parent after its child", "BODY-BIND-010", h,
            [](CaseSetup& x) { x.mPackage.mBinding.mRestSkeleton->at(1).mParent = 2; }, "/binding/restSkeleton",
            { "BODY-BIND-013" }));
        cases.push_back(errorCase("rest skeleton with a repeated bone name", "BODY-BIND-010", h,
            [](CaseSetup& x) { x.mPackage.mBinding.mRestSkeleton->at(2).mName = "Bip Pelvis"; },
            "/binding/restSkeleton", { "BODY-BIND-013" }));
        cases.push_back(errorCase("rest rotation that is not a unit quaternion", "BODY-BIND-012", h,
            [](CaseSetup& x) {
                x.mPackage.mBinding.mRestSkeleton->at(3).mRotation = { 0, 0, 0, 2 };
            },
            "/binding/restSkeleton/3/rotation", { "BODY-BIND-010" }));
        cases.push_back(errorCase("something rotates above the animation root", "BODY-BIND-013", h,
            [](CaseSetup& x) {
                x.mPackage.mBinding.mRootBone = "Bip Pelvis";
                x.mPackage.mBinding.mRestSkeleton->at(0).mRotation = { 0, 0, std::sqrt(0.5), std::sqrt(0.5) };
            },
            "/binding/restSkeleton/0", { "BODY-BIND-002" }));
        cases.push_back(warningCase(
            "no rest skeleton", "BODY-BIND-014", h, [](CaseSetup& x) { x.mPackage.mBinding.mRestSkeleton.reset(); },
            "/binding/restSkeleton"));

        // --- Skinning (mesh 4 "hands": 6 vertices, bones Bip LHand and Bip RHand)
        cases.push_back(errorCase(
            "negative weight", "BODY-SKIN-001", h,
            [](CaseSetup& x) {
                setInfluences(*x.mPackage.mMeshes[4].mSummary, 1, { { 0, -0.25 }, { 1, 1.25 } });
            },
            "/meshes/4/summary/influences/1"));
        cases.push_back(errorCase(
            "weights that do not sum to one", "BODY-SKIN-002", h,
            [](CaseSetup& x) {
                setInfluences(*x.mPackage.mMeshes[4].mSummary, 0, { { 0, 0.9 } });
            },
            "/meshes/4/summary/influenceOffsets/0"));
        cases.push_back(errorCase(
            "weights sum to more than one", "BODY-SKIN-002", h,
            [](CaseSetup& x) {
                setInfluences(*x.mPackage.mMeshes[4].mSummary, 0, { { 0, 0.5 }, { 1, 0.75 } });
            },
            "/meshes/4/summary/influenceOffsets/0"));
        cases.push_back(errorCase(
            "nine influences on a vertex", "BODY-SKIN-003", h,
            [](CaseSetup& x) {
                Body::MeshSummary& summary = *x.mPackage.mMeshes[4].mSummary;
                summary.mBoneNames = { "Bip Pelvis", "Bip Spine", "Bip Chest", "Bip Neck", "Bip Head", "Bip LHand",
                    "Bip RHand", "Bip LFoot", "Bip RFoot" };
                std::vector<Body::MeshSummary::Influence> nine;
                for (std::uint32_t i = 0; i < 9; ++i)
                {
                    nine.push_back({ i, 1.0 / 9.0 });
                }
                setInfluences(summary, 0, nine);
            },
            "/meshes/4/summary/influenceOffsets/0"));
        cases.push_back(errorCase(
            "bone index out of range", "BODY-SKIN-004", h,
            [](CaseSetup& x) {
                setInfluences(*x.mPackage.mMeshes[4].mSummary, 0, { { 5, 1.0 } });
            },
            "/meshes/4/summary/influences/0"));
        cases.push_back(errorCase("vertex with no influence on a skinned mesh", "BODY-SKIN-005", h,
            [](CaseSetup& x) { setInfluences(*x.mPackage.mMeshes[4].mSummary, 0, {}); },
            "/meshes/4/summary/influenceOffsets/0", { "BODY-SKIN-002" }));
        cases.push_back(errorCase("offsets one short", "BODY-SKIN-006", h,
            [](CaseSetup& x) { x.mPackage.mMeshes[4].mSummary->mInfluenceOffsets.pop_back(); },
            "/meshes/4/summary/influenceOffsets", { "BODY-SKIN-002", "BODY-SKIN-003", "BODY-SKIN-005" }));
        cases.push_back(errorCase("offsets decreasing", "BODY-SKIN-006", h,
            [](CaseSetup& x) {
                std::swap(x.mPackage.mMeshes[4].mSummary->mInfluenceOffsets[2],
                    x.mPackage.mMeshes[4].mSummary->mInfluenceOffsets[3]);
            },
            "/meshes/4/summary/influenceOffsets", { "BODY-SKIN-002", "BODY-SKIN-003", "BODY-SKIN-005" }));

        // --- Geometry
        cases.push_back(errorCase(
            "bounds with min above max", "BODY-GEOM-001", h,
            [](CaseSetup& x) { x.mPackage.mMeshes[2].mSummary->mBoundsMin[0] = 1.0; }, "/meshes/2/summary/boundsMin"));
        cases.push_back(errorCaseAt(
            "bounds that are NaN", "BODY-GEOM-001", h,
            [](CaseSetup& x) { x.mPackage.mMeshes[2].mSummary->mBoundsMax[1] = sNaN; }, "/meshes/2/summary/bounds",
            ""));
        cases.push_back(errorCaseAt(
            "body far too large", "BODY-GEOM-002", h,
            [](CaseSetup& x) { x.mPackage.mMeshes[0].mSummary->mBoundsMax[2] = 100.0; }, "/meshes/", "/summary"));
        cases.push_back(errorCaseAt(
            "body far too small", "BODY-GEOM-002", h,
            [](CaseSetup& x) {
                for (const std::size_t index : { std::size_t{ 0 }, std::size_t{ 4 } })
                {
                    x.mPackage.mMeshes[index].mSummary->mBoundsMin = { 0, 0, 0 };
                    x.mPackage.mMeshes[index].mSummary->mBoundsMax = { 0.01, 0.01, 0.01 };
                }
            },
            "/meshes/", "/summary"));
        cases.push_back(errorCase(
            "triangle index out of range", "BODY-GEOM-003", h,
            [](CaseSetup& x) { x.mPackage.mMeshes[4].mSummary->mTriangleIndices[2] = 99; },
            "/meshes/4/summary/triangleIndices/2"));
        cases.push_back(errorCase(
            "triangle indices of the wrong count", "BODY-GEOM-004", h,
            [](CaseSetup& x) { x.mPackage.mMeshes[4].mSummary->mTriangleIndices.pop_back(); },
            "/meshes/4/summary/triangleIndices"));
        cases.push_back(errorCase(
            "region map with a gap", "BODY-GEOM-005", h,
            [](CaseSetup& x) { x.mPackage.mRegionMap->mRegions[1].mRanges[0].mEnd = 7; }, "/regionMap/regions"));
        cases.push_back(errorCaseAt(
            "region map with an overlap", "BODY-GEOM-006", h,
            [](CaseSetup& x) { x.mPackage.mRegionMap->mRegions[1].mRanges[0].mBegin = 3; }, "/regionMap/regions/",
            "/ranges/0"));
        cases.push_back(errorCase(
            "region range past the last triangle", "BODY-GEOM-007", h,
            [](CaseSetup& x) { x.mPackage.mRegionMap->mRegions[1].mRanges[0].mEnd = 9; },
            "/regionMap/regions/1/ranges/0"));
        cases.push_back(errorCase("region range with begin after end", "BODY-GEOM-007", h,
            [](CaseSetup& x) {
                x.mPackage.mRegionMap->mRegions[1].mRanges[0] = { 6, 4 };
            },
            "/regionMap/regions/1/ranges/0", { "BODY-GEOM-005", "BODY-GEOM-006" }));
        cases.push_back(errorCase(
            "region map names no mesh", "BODY-GEOM-008", h, [](CaseSetup& x) { x.mPackage.mRegionMap->mMesh = "nope"; },
            "/regionMap/mesh"));
        cases.push_back(warningCase(
            "mesh without a summary", "BODY-MESH-001", h, [](CaseSetup& x) { x.mPackage.mMeshes[4].mSummary.reset(); },
            "/meshes/4"));

        // --- Morphs
        cases.push_back(errorCase(
            "morph vertex out of range", "BODY-MORPH-001", h,
            [](CaseSetup& x) { x.mPackage.mMorphSet->mMorphs[0].mTargets[0].mVertices.back() = 99; },
            "/morphSet/morphs/0/targets/0/vertices/9"));
        cases.push_back(errorCase(
            "morph delta that is NaN", "BODY-MORPH-002", h,
            [](CaseSetup& x) { x.mPackage.mMorphSet->mMorphs[0].mTargets[0].mDeltas[3][1] = sNaN; },
            "/morphSet/morphs/0/targets/0/deltas/3"));
        cases.push_back(errorCase(
            "morph delta that is infinite", "BODY-MORPH-002", h,
            [](CaseSetup& x) {
                x.mPackage.mMorphSet->mMorphs[0].mTargets[0].mDeltas[0][2] = std::numeric_limits<double>::infinity();
            },
            "/morphSet/morphs/0/targets/0/deltas/0"));
        cases.push_back(errorCase(
            "morph default outside its range", "BODY-MORPH-003", h,
            [](CaseSetup& x) { x.mPackage.mMorphSet->mMorphs[0].mDefault = 5.0; }, "/morphSet/morphs/0/range"));
        cases.push_back(errorCase(
            "morph range with min above max", "BODY-MORPH-003", h,
            [](CaseSetup& x) {
                x.mPackage.mMorphSet->mMorphs[0].mRangeMin = 2.0;
                x.mPackage.mMorphSet->mMorphs[0].mRangeMax = 1.0;
            },
            "/morphSet/morphs/0/range"));
        cases.push_back(errorCase(
            "duplicate morph id", "BODY-MORPH-004", h,
            [](CaseSetup& x) { x.mPackage.mMorphSet->mMorphs.push_back(x.mPackage.mMorphSet->mMorphs[0]); },
            "/morphSet/morphs/1/id"));
        cases.push_back(errorCase(
            "morph target names no mesh", "BODY-MORPH-005", h,
            [](CaseSetup& x) { x.mPackage.mMorphSet->mMorphs[0].mTargets[0].mMesh = "nope"; },
            "/morphSet/morphs/0/targets/0/mesh"));
        cases.push_back(errorCase(
            "morph vertices not increasing", "BODY-MORPH-006", h,
            [](CaseSetup& x) {
                std::swap(x.mPackage.mMorphSet->mMorphs[0].mTargets[0].mVertices[0],
                    x.mPackage.mMorphSet->mMorphs[0].mTargets[0].mVertices[1]);
            },
            "/morphSet/morphs/0/targets/0/vertices"));
        cases.push_back(errorCase("morph deltas and vertices differ in count", "BODY-MORPH-006", h,
            [](CaseSetup& x) { x.mPackage.mMorphSet->mMorphs[0].mTargets[0].mDeltas.pop_back(); },
            "/morphSet/morphs/0/targets/0/vertices", { "BODY-MORPH-002" }));

        // --- Proxy
        cases.push_back(errorCase(
            "capsule radius zero", "BODY-PROXY-001", h,
            [](CaseSetup& x) { x.mPackage.mProxy->mCapsules[0].mRadius = 0; }, "/proxy/capsules/0/radius"));
        cases.push_back(errorCase(
            "capsule radius NaN", "BODY-PROXY-001", q,
            [](CaseSetup& x) { x.mPackage.mProxy->mCapsules[1].mRadius = sNaN; }, "/proxy/capsules/1/radius"));
        cases.push_back(errorCase(
            "capsule on an unknown bone", "BODY-PROXY-002", h,
            [](CaseSetup& x) { x.mPackage.mProxy->mCapsules[0].mBone = "Nope"; }, "/proxy/capsules/0/bone"));

        // --- Physics profile (quadruped)
        cases.push_back(errorCase(
            "shape size zero", "BODY-PHYS-001", q,
            [](CaseSetup& x) { x.mPackage.mPhysicsProfile->mShapes[0].mSize[0] = 0; },
            "/physicsProfile/shapes/0/size"));
        cases.push_back(errorCase(
            "shape size negative", "BODY-PHYS-001", q,
            [](CaseSetup& x) { x.mPackage.mPhysicsProfile->mShapes[1].mSize[0] = -0.1; },
            "/physicsProfile/shapes/1/size"));
        cases.push_back(errorCase(
            "mass zero", "BODY-PHYS-002", q,
            [](CaseSetup& x) { x.mPackage.mPhysicsProfile->mMasses[0].mKilograms = 0; },
            "/physicsProfile/masses/0/kilograms"));
        cases.push_back(errorCase(
            "mass NaN", "BODY-PHYS-002", q,
            [](CaseSetup& x) { x.mPackage.mPhysicsProfile->mMasses[1].mKilograms = sNaN; },
            "/physicsProfile/masses/1/kilograms"));
        cases.push_back(errorCase(
            "joint rest outside limits", "BODY-PHYS-003", q,
            [](CaseSetup& x) { x.mPackage.mPhysicsProfile->mJointLimits[0].mRest = 5.0; },
            "/physicsProfile/jointLimits/0"));
        cases.push_back(errorCase(
            "joint limits with min above max", "BODY-PHYS-003", q,
            [](CaseSetup& x) {
                x.mPackage.mPhysicsProfile->mJointLimits[1].mMin = 2.0;
                x.mPackage.mPhysicsProfile->mJointLimits[1].mMax = 1.0;
            },
            "/physicsProfile/jointLimits/1"));
        cases.push_back(errorCase(
            "shape on an unknown bone", "BODY-PHYS-004", q,
            [](CaseSetup& x) { x.mPackage.mPhysicsProfile->mShapes[0].mBone = "Nope"; },
            "/physicsProfile/shapes/0/bone"));
        cases.push_back(errorCase(
            "mass on an unknown bone", "BODY-PHYS-004", q,
            [](CaseSetup& x) { x.mPackage.mPhysicsProfile->mMasses[0].mBone = "Nope"; },
            "/physicsProfile/masses/0/bone"));
        cases.push_back(errorCase(
            "joint limit on an unknown bone", "BODY-PHYS-004", q,
            [](CaseSetup& x) { x.mPackage.mPhysicsProfile->mJointLimits[0].mBone = "Nope"; },
            "/physicsProfile/jointLimits/0/bone"));
        cases.push_back(errorCase(
            "rig file that fails its own loader", "BODY-PHYS-005", q,
            [](CaseSetup& x) {
                x.mPackage.mPhysicsProfile->mRigPath = "rigs/q.rig.json";
                x.mOptions.mReadFile = [](std::string_view) { return std::optional<std::string>("garbage"); };
            },
            "/physicsProfile/rig"));
        cases.push_back(errorCase(
            "profile file that fails its own loader", "BODY-PHYS-005", q,
            [](CaseSetup& x) {
                x.mPackage.mPhysicsProfile->mProfilePath = "rigs/q.profile.json";
                x.mOptions.mReadFile = [](std::string_view) { return std::optional<std::string>("garbage"); };
            },
            "/physicsProfile/profile"));
        cases.push_back(warningCase(
            "rig path and no file reader", "BODY-PHYS-010", q,
            [](CaseSetup& x) { x.mPackage.mPhysicsProfile->mRigPath = "rigs/q.rig.json"; }, "/physicsProfile/rig"));
        cases.push_back(warningCase(
            "profile path and no file reader", "BODY-PHYS-010", q,
            [](CaseSetup& x) { x.mPackage.mPhysicsProfile->mProfilePath = "rigs/q.profile.json"; },
            "/physicsProfile/profile"));

        // --- Measurements, landmarks, soft tissue
        cases.push_back(errorCase(
            "negative measurement", "BODY-MEAS-001", q,
            [](CaseSetup& x) { x.mPackage.mMeasurements->mEntries[0].mValue = -1.0; },
            "/measurements/entries/0/value"));
        cases.push_back(errorCase(
            "measurement that is NaN", "BODY-MEAS-001", q,
            [](CaseSetup& x) { x.mPackage.mMeasurements->mEntries[1].mValue = sNaN; },
            "/measurements/entries/1/value"));
        cases.push_back(errorCaseAt(
            "duplicate measurement name", "BODY-MEAS-001", q,
            [](CaseSetup& x) {
                x.mPackage.mMeasurements->mEntries[1].mName = x.mPackage.mMeasurements->mEntries[0].mName;
            },
            "/measurements/entries/", ""));
        cases.push_back(errorCase(
            "landmark on an unknown bone", "BODY-LAND-001", q,
            [](CaseSetup& x) { x.mPackage.mLandmarks->mEntries[0].mBone = "Nope"; }, "/landmarks/entries/0"));
        cases.push_back(errorCaseAt(
            "duplicate landmark name", "BODY-LAND-001", q,
            [](CaseSetup& x) { x.mPackage.mLandmarks->mEntries[1].mName = x.mPackage.mLandmarks->mEntries[0].mName; },
            "/landmarks/entries/", ""));
        cases.push_back(errorCase(
            "soft tissue mass zero", "BODY-SOFT-001", q,
            [](CaseSetup& x) { x.mPackage.mSoftTissue->mNodes[0].mMass = 0; }, "/softTissue/nodes/0"));
        cases.push_back(errorCase(
            "soft tissue stiffness negative", "BODY-SOFT-001", q,
            [](CaseSetup& x) { x.mPackage.mSoftTissue->mNodes[0].mStiffness = -1; }, "/softTissue/nodes/0"));
        cases.push_back(errorCase(
            "soft tissue damping NaN", "BODY-SOFT-001", q,
            [](CaseSetup& x) { x.mPackage.mSoftTissue->mNodes[0].mDamping = sNaN; }, "/softTissue/nodes/0"));
        cases.push_back(errorCase(
            "soft tissue on an unknown bone", "BODY-SOFT-001", q,
            [](CaseSetup& x) { x.mPackage.mSoftTissue->mNodes[0].mBone = "Nope"; }, "/softTissue/nodes/0"));

        // --- Age gate
        cases.push_back(errorCase(
            "restricted physics tier, age unspecified", "BODY-AGE-001", q,
            [](CaseSetup& x) { x.mPackage.mPhysicsProfile->mRestrictedTier = true; },
            "/physicsProfile/restrictedTier"));
        cases.push_back(errorCase(
            "restricted physics tier, age minor", "BODY-AGE-001", q,
            [](CaseSetup& x) {
                x.mPackage.mPhysicsProfile->mRestrictedTier = true;
                x.mPackage.mIdentity.mAgeClass = Body::AgeClass::Minor;
            },
            "/physicsProfile/restrictedTier"));
        cases.push_back(errorCase(
            "restricted soft tissue tier, age unspecified", "BODY-AGE-001", q,
            [](CaseSetup& x) { x.mPackage.mSoftTissue->mRestrictedTier = true; }, "/softTissue/restrictedTier"));
        cases.push_back(errorCase(
            "restricted soft tissue tier, age minor", "BODY-AGE-001", q,
            [](CaseSetup& x) {
                x.mPackage.mSoftTissue->mRestrictedTier = true;
                x.mPackage.mIdentity.mAgeClass = Body::AgeClass::Minor;
            },
            "/softTissue/restrictedTier"));
        return cases;
    }

    std::string wholeText(const Body::Report& report)
    {
        return Body::formatReportText(report) + "\n---\n" + Body::formatReportJson(report);
    }
}

TEST(BodyValidate, CleanHumanoidHasNoFindings)
{
    const Body::Report report = Body::validateBodyPackage(humanoid());
    EXPECT_TRUE(report.mFindings.empty()) << describe(report);
    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.errorCount(), 0);
    EXPECT_EQ(report.warningCount(), 0);
    EXPECT_TRUE(report.mHumanoidGait);
}

TEST(BodyValidate, CleanQuadrupedHasNoFindings)
{
    const Body::Report report = Body::validateBodyPackage(tailedQuadruped());
    EXPECT_TRUE(report.mFindings.empty()) << describe(report);
    EXPECT_TRUE(report.ok());
    EXPECT_FALSE(report.mHumanoidGait);
}

TEST(BodyValidate, CleanFixturesStayCleanAfterAJsonRoundTrip)
{
    for (const Body::BodyPackage& package : { humanoid(), tailedQuadruped() })
    {
        const Body::BodyLoadResult loaded = Body::readBodyPackage(Body::writeBodyPackage(package), "x");
        ASSERT_TRUE(loaded.mValue.has_value()) << loaded.mError.toString();
        const Body::Report report = Body::validateBodyPackage(*loaded.mValue);
        EXPECT_TRUE(report.mFindings.empty()) << describe(report);
    }
}

TEST(BodyValidate, EveryFindingCodeHasAFailingFixture)
{
    const std::vector<Case> cases = allCases();
    std::set<std::string> covered;
    for (const Case& testCase : cases)
    {
        checkCase(testCase);
        covered.insert(testCase.mCode);
    }
    // Every code of the table in bodyvalidate.hpp.
    const char* const table[] = { "STRUCT-001", "STRUCT-002", "STRUCT-003", "STRUCT-004", "STRUCT-005", "STRUCT-006",
        "STRUCT-007", "TARGET-001", "ORIGIN-001", "PROV-001", "PROV-002", "BIND-001", "BIND-002", "BIND-003",
        "BIND-004", "BIND-005", "BIND-006", "BIND-010", "BIND-012", "BIND-013", "BIND-014", "SKIN-001", "SKIN-002",
        "SKIN-003", "SKIN-004", "SKIN-005", "SKIN-006", "GEOM-001", "GEOM-002", "GEOM-003", "GEOM-004", "GEOM-005",
        "GEOM-006", "GEOM-007", "GEOM-008", "MESH-001", "MORPH-001", "MORPH-002", "MORPH-003", "MORPH-004", "MORPH-005",
        "MORPH-006", "PROXY-001", "PROXY-002", "PHYS-001", "PHYS-002", "PHYS-003", "PHYS-004", "PHYS-005", "PHYS-010",
        "MEAS-001", "LAND-001", "SOFT-001", "AGE-001" };
    for (const char* code : table)
    {
        EXPECT_EQ(covered.count(std::string("BODY-") + code), 1u) << "no failing fixture for " << code;
    }
}

TEST(BodyValidate, ErrorsRejectAndWarningsDoNot)
{
    for (const Case& testCase : allCases())
    {
        SCOPED_TRACE(testCase.mName);
        const std::unique_ptr<CaseSetup> setup = makeSetup(testCase.mBase);
        testCase.mMutate(*setup);
        const Body::Report report = run(*setup);
        int errors = 0;
        int warnings = 0;
        for (const Body::Finding& finding : report.mFindings)
        {
            if (finding.mSeverity == Body::Severity::Error)
            {
                ++errors;
            }
            else
            {
                ++warnings;
            }
        }
        EXPECT_EQ(report.errorCount(), errors);
        EXPECT_EQ(report.warningCount(), warnings);
        EXPECT_EQ(report.ok(), errors == 0);
        if (testCase.mSeverity == Body::Severity::Warning)
        {
            EXPECT_GE(warnings, 1);
        }
    }
}

TEST(BodyValidate, WarningOnlyCasesAreStillOk)
{
    // BODY-BIND-014 is the one warning that comes with no other finding on the humanoid.
    Body::BodyPackage package = humanoid();
    package.mBinding.mRestSkeleton.reset();
    const Body::Report report = Body::validateBodyPackage(package);
    EXPECT_TRUE(report.ok());
    EXPECT_EQ(report.errorCount(), 0);
    EXPECT_EQ(report.warningCount(), 1);
    ASSERT_EQ(report.mFindings.size(), 1u);
    EXPECT_EQ(report.mFindings[0].mCode, "BODY-BIND-014");
    EXPECT_EQ(report.mFindings[0].mSeverity, Body::Severity::Warning);
}

TEST(BodyValidate, ContractVersionBoundary)
{
    Body::BodyPackage package = humanoid();
    package.mIdentity.mContractVersion = { Body::sContractMajor, Body::sContractMinor };
    EXPECT_TRUE(Body::validateBodyPackage(package).mFindings.empty());
    package.mIdentity.mContractVersion = { Body::sContractMajor, Body::sContractMinor + 1 };
    const Body::Report newer = Body::validateBodyPackage(package);
    EXPECT_FALSE(newer.ok());
    EXPECT_NE(findCode(newer, "BODY-STRUCT-001"), nullptr);
    package.mIdentity.mContractVersion = { Body::sContractMajor + 1, 0 };
    EXPECT_FALSE(Body::validateBodyPackage(package).ok());
}

TEST(BodyValidate, VersionStringsAreDigitsOnly)
{
    for (const char* good : { "0.0.0", "1.2.3", "10.20.30" })
    {
        Body::BodyPackage package = humanoid();
        package.mIdentity.mVersion = good;
        EXPECT_EQ(findCode(Body::validateBodyPackage(package), "BODY-STRUCT-007"), nullptr) << good;
    }
    for (const char* bad : { "1", "1.2", "1.2.3.4", "a.b.c", "1..3", "1.2.", ".1.2", "-1.2.3", "1.2.3-beta", " 1.2.3" })
    {
        Body::BodyPackage package = humanoid();
        package.mIdentity.mVersion = bad;
        EXPECT_NE(findCode(Body::validateBodyPackage(package), "BODY-STRUCT-007"), nullptr) << bad;
    }
}

TEST(BodyValidate, ThresholdBoundaries)
{
    // Weight sum: inside the tolerance passes, outside fails.
    {
        Body::BodyPackage package = humanoid();
        setInfluences(*package.mMeshes[4].mSummary, 0, { { 0, 1.0 - 0.5 * Body::Thresholds::sWeightSumTolerance } });
        EXPECT_EQ(findCode(Body::validateBodyPackage(package), "BODY-SKIN-002"), nullptr);
        setInfluences(*package.mMeshes[4].mSummary, 0, { { 0, 1.0 - 2.0 * Body::Thresholds::sWeightSumTolerance } });
        EXPECT_NE(findCode(Body::validateBodyPackage(package), "BODY-SKIN-002"), nullptr);
    }
    // Influences per vertex: the maximum passes, one more fails.
    {
        for (int count : { Body::Thresholds::sMaxInfluencesPerVertex, Body::Thresholds::sMaxInfluencesPerVertex + 1 })
        {
            Body::BodyPackage package = humanoid();
            Body::MeshSummary& summary = *package.mMeshes[4].mSummary;
            summary.mBoneNames = { "Bip Pelvis", "Bip Spine", "Bip Chest", "Bip Neck", "Bip Head", "Bip LHand",
                "Bip RHand", "Bip LFoot", "Bip RFoot" };
            std::vector<Body::MeshSummary::Influence> list;
            for (int i = 0; i < count; ++i)
            {
                list.push_back({ static_cast<std::uint32_t>(i), 1.0 / count });
            }
            setInfluences(summary, 0, list);
            const bool tooMany = count > Body::Thresholds::sMaxInfluencesPerVertex;
            EXPECT_EQ(findCode(Body::validateBodyPackage(package), "BODY-SKIN-003") != nullptr, tooMany) << count;
        }
    }
    // Body extent.
    {
        const auto extentFinding = [](double extent) {
            Body::BodyPackage package = humanoid();
            for (const std::size_t index : { std::size_t{ 0 }, std::size_t{ 4 } })
            {
                package.mMeshes[index].mSummary->mBoundsMin = { 0, 0, 0 };
                package.mMeshes[index].mSummary->mBoundsMax = { extent, extent * 0.5, extent };
            }
            return findCode(Body::validateBodyPackage(package), "BODY-GEOM-002") != nullptr;
        };
        EXPECT_FALSE(extentFinding(Body::Thresholds::sMinBodyExtentM * 1.2));
        EXPECT_FALSE(extentFinding(Body::Thresholds::sMaxBodyExtentM * 0.9));
        EXPECT_TRUE(extentFinding(Body::Thresholds::sMinBodyExtentM * 0.8));
        EXPECT_TRUE(extentFinding(Body::Thresholds::sMaxBodyExtentM * 1.1));
    }
}

TEST(BodyValidate, UnskinnedMeshNeedsNoInfluences)
{
    // The humanoid's hair is not skinned: no boneNames, no offsets. That is not an error.
    Body::BodyPackage package = humanoid();
    ASSERT_TRUE(package.mMeshes[3].mSummary->mBoneNames.empty());
    EXPECT_TRUE(Body::validateBodyPackage(package).mFindings.empty());
}

TEST(BodyValidate, OnlyLodZeroSkinMeshesDecideTheBodyExtent)
{
    // A huge LOD 1 mesh and a huge head do not trip GEOM-002.
    Body::BodyPackage package = humanoid();
    package.mMeshes[1].mSummary->mBoundsMax = { 500, 500, 500 };
    package.mMeshes[2].mSummary->mBoundsMax = { 500, 500, 500 };
    EXPECT_EQ(findCode(Body::validateBodyPackage(package), "BODY-GEOM-002"), nullptr);
}

TEST(BodyValidate, MeshSourceSuppliesMissingSummaries)
{
    Body::BodyPackage package = humanoid();
    FakeMeshSource source;
    moveSummariesToSource(package, source);
    for (const Body::MeshEntry& mesh : package.mMeshes)
    {
        EXPECT_FALSE(mesh.mSummary.has_value());
    }

    Body::ValidateOptions withSource;
    withSource.mMeshSource = &source;
    const Body::Report supplied = Body::validateBodyPackage(package, withSource);
    EXPECT_TRUE(supplied.mFindings.empty()) << describe(supplied);
    EXPECT_FALSE(source.mCalls.empty());
    for (const auto& call : source.mCalls)
    {
        EXPECT_TRUE(source.mSummaries.count(call.first) == 1) << call.first;
        EXPECT_EQ(call.second, "gltf");
    }

    // Without the source every mesh warns once and nothing else is said about meshes.
    const Body::Report without = Body::validateBodyPackage(package);
    EXPECT_TRUE(without.ok()) << describe(without);
    int meshWarnings = 0;
    for (const Body::Finding& finding : without.mFindings)
    {
        if (finding.mCode == "BODY-MESH-001")
        {
            ++meshWarnings;
            EXPECT_EQ(finding.mSeverity, Body::Severity::Warning);
        }
    }
    EXPECT_EQ(meshWarnings, 5);

    // A source that does not know one path: only that mesh warns.
    source.mSummaries.erase("mesh/hands.glb");
    const Body::Report partial = Body::validateBodyPackage(package, withSource);
    ASSERT_EQ(partial.mFindings.size(), 1u) << describe(partial);
    EXPECT_EQ(partial.mFindings[0].mCode, "BODY-MESH-001");
    EXPECT_EQ(partial.mFindings[0].mPath, "/meshes/4");
}

TEST(BodyValidate, InlineSummaryWinsOverTheSource)
{
    Body::BodyPackage package = humanoid();
    FakeMeshSource source;
    // The source holds a broken summary for the body; the inline one is valid, so no finding may appear.
    Body::MeshSummary broken = *package.mMeshes[0].mSummary;
    broken.mBoundsMax[2] = 1000;
    source.mSummaries[package.mMeshes[0].mPath] = broken;
    Body::ValidateOptions options;
    options.mMeshSource = &source;
    EXPECT_TRUE(Body::validateBodyPackage(package, options).mFindings.empty());
}

TEST(BodyValidate, SkeletonPathReadThroughTheFileReader)
{
    Body::BodyPackage package = humanoid();
    const opas::anim::SkeletonProfile profile = *package.mBinding.mSkeletonProfile;
    package.mBinding.mSkeletonProfile.reset();
    package.mBinding.mSkeletonPath = "profiles/h.skeletonprofile.json";

    std::vector<std::string> asked;
    Body::ValidateOptions options;
    options.mReadFile = [&asked, &profile](std::string_view path) -> std::optional<std::string> {
        asked.emplace_back(path);
        if (path == "profiles/h.skeletonprofile.json")
        {
            return opas::anim::writeSkeletonProfile(profile);
        }
        return std::nullopt;
    };
    const Body::Report report = Body::validateBodyPackage(package, options);
    EXPECT_TRUE(report.mFindings.empty()) << describe(report);
    EXPECT_TRUE(report.mHumanoidGait);
    ASSERT_FALSE(asked.empty());
    EXPECT_EQ(asked[0], "profiles/h.skeletonprofile.json");

    // The same package without a reader: one warning, still ok.
    const Body::Report noReader = Body::validateBodyPackage(package);
    EXPECT_TRUE(noReader.ok());
    ASSERT_NE(findCode(noReader, "BODY-BIND-005"), nullptr) << describe(noReader);
    EXPECT_EQ(findCode(noReader, "BODY-BIND-005")->mSeverity, Body::Severity::Warning);
}

TEST(BodyValidate, ProfileFromFileStillChecksBoneNames)
{
    // The profile read from the file lacks a bone that a mesh's skin uses: BIND-004.
    Body::BodyPackage package = humanoid();
    opas::anim::SkeletonProfile profile = *package.mBinding.mSkeletonProfile;
    profile.bones.erase(std::remove_if(profile.bones.begin(), profile.bones.end(),
                            [](const opas::anim::BoneMapping& mapping) { return mapping.target == "Bip Chest"; }),
        profile.bones.end());
    package.mBinding.mSkeletonProfile.reset();
    package.mBinding.mSkeletonPath = "profiles/h.skeletonprofile.json";
    Body::ValidateOptions options;
    options.mReadFile = [&profile](std::string_view) -> std::optional<std::string> {
        return opas::anim::writeSkeletonProfile(profile);
    };
    const Body::Report report = Body::validateBodyPackage(package, options);
    EXPECT_NE(findCode(report, "BODY-BIND-004"), nullptr) << describe(report);
}

TEST(BodyValidate, HumanoidGaitIsDerivedFromTheSkeletonNotAnError)
{
    EXPECT_TRUE(Body::validateBodyPackage(humanoid()).mHumanoidGait);
    EXPECT_FALSE(Body::validateBodyPackage(tailedQuadruped()).mHumanoidGait);

    for (const char* source : { "Pelvis", "LeftFoot", "RightFoot" })
    {
        Body::BodyPackage package = humanoid();
        bool renamed = false;
        for (opas::anim::BoneMapping& mapping : package.mBinding.mSkeletonProfile->bones)
        {
            if (mapping.source == source)
            {
                mapping.source = std::string(source) + "Renamed";
                renamed = true;
            }
        }
        ASSERT_TRUE(renamed) << source;
        const Body::Report report = Body::validateBodyPackage(package);
        EXPECT_FALSE(report.mHumanoidGait) << "without " << source;
        EXPECT_TRUE(report.mFindings.empty()) << "without " << source << "\n" << describe(report);
    }
}

TEST(BodyValidate, NoRuleRequiresLegsAPelvisOrFeet)
{
    // A body with no legs at all (a snake: root, spine chain, head) must not produce an error about legs, a pelvis or
    // feet.
    Body::BodyPackage snake;
    snake.mIdentity.mId = "snake";
    snake.mIdentity.mName = "Snake";
    snake.mIdentity.mVersion = "1.0.0";
    snake.mIdentity.mAuthor = "OpenEden tests";
    snake.mIdentity.mLicense = "CC0-1.0";
    opas::anim::SkeletonProfile profile;
    profile.name = "snake";
    std::vector<std::string> bones = { "Snake Spine1", "Snake Spine2", "Snake Spine3", "Snake Head" };
    for (const std::string& bone : bones)
    {
        profile.bones.push_back({ "src " + bone, bone });
    }
    snake.mBinding.mSkeletonProfile = profile;
    snake.mBinding.mRootBone = "Snake Root";
    snake.mBinding.mUnmappedBones = { "Snake Root" };
    snake.mBinding.mRestSkeleton = std::vector<Body::RestBone>{};
    {
        Body::RestBone root;
        root.mName = "Snake Root";
        snake.mBinding.mRestSkeleton->push_back(root);
        int parent = 0;
        for (const std::string& bone : bones)
        {
            Body::RestBone rest;
            rest.mName = bone;
            rest.mParent = parent++;
            rest.mTranslation = { 0, 0.3, 0 };
            snake.mBinding.mRestSkeleton->push_back(rest);
        }
    }
    Body::MeshEntry mesh;
    mesh.mId = "body";
    mesh.mPath = "mesh/snake.glb";
    mesh.mFormat = Body::MeshFormat::Gltf;
    mesh.mSummary = makeSummary(8, bones, { -0.1, 0, 0 }, { 0.1, 1.5, 0.2 });
    snake.mMeshes.push_back(mesh);

    const Body::Report report = Body::validateBodyPackage(snake);
    EXPECT_TRUE(report.ok()) << describe(report);
    EXPECT_FALSE(report.mHumanoidGait);
    for (const Body::Finding& finding : report.mFindings)
    {
        std::string message = finding.mMessage;
        std::transform(message.begin(), message.end(), message.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        EXPECT_EQ(message.find("pelvis"), std::string::npos) << finding.mMessage;
        EXPECT_EQ(message.find("leg"), std::string::npos) << finding.mMessage;
        EXPECT_EQ(message.find("foot"), std::string::npos) << finding.mMessage;
        EXPECT_EQ(message.find("feet"), std::string::npos) << finding.mMessage;
    }
}

TEST(BodyValidate, NonHumanoidDefectsNeverMentionLegsPelvisOrFeet)
{
    for (const Case& testCase : allCases())
    {
        if (testCase.mBase != Base::Quadruped)
        {
            continue;
        }
        const std::unique_ptr<CaseSetup> setup = makeSetup(Base::Quadruped);
        testCase.mMutate(*setup);
        const Body::Report report = run(*setup);
        EXPECT_FALSE(report.mHumanoidGait) << testCase.mName;
        for (const Body::Finding& finding : report.mFindings)
        {
            std::string message = finding.mMessage;
            std::transform(message.begin(), message.end(), message.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            EXPECT_EQ(message.find("pelvis"), std::string::npos) << testCase.mName << ": " << finding.mMessage;
            EXPECT_EQ(message.find("two legs"), std::string::npos) << testCase.mName << ": " << finding.mMessage;
            EXPECT_EQ(message.find("feet"), std::string::npos) << testCase.mName << ": " << finding.mMessage;
        }
    }
}

TEST(BodyValidate, AgeGateTruthTable)
{
    using Body::AgeClass;
    const AgeClass all[] = { AgeClass::Unspecified, AgeClass::Minor, AgeClass::Adult };
    for (const AgeClass package : all)
    {
        for (const AgeClass actor : all)
        {
            const bool expected = package == AgeClass::Adult && actor == AgeClass::Adult;
            EXPECT_EQ(Body::restrictedTierAllowed(package, actor), expected)
                << Body::toString(package) << " package, " << Body::toString(actor) << " actor";
        }
    }
}

TEST(BodyValidate, RestrictedTierWithAdultIsAccepted)
{
    Body::BodyPackage package = tailedQuadruped();
    package.mIdentity.mAgeClass = Body::AgeClass::Adult;
    package.mPhysicsProfile->mRestrictedTier = true;
    package.mSoftTissue->mRestrictedTier = true;
    const Body::Report report = Body::validateBodyPackage(package);
    EXPECT_EQ(findCode(report, "BODY-AGE-001"), nullptr) << describe(report);
    EXPECT_TRUE(report.ok()) << describe(report);

    for (const Body::AgeClass age : { Body::AgeClass::Unspecified, Body::AgeClass::Minor })
    {
        package.mIdentity.mAgeClass = age;
        const Body::Report refused = Body::validateBodyPackage(package);
        EXPECT_FALSE(refused.ok());
        int ageErrors = 0;
        for (const Body::Finding& finding : refused.mFindings)
        {
            if (finding.mCode == "BODY-AGE-001")
            {
                ++ageErrors;
                EXPECT_EQ(finding.mSeverity, Body::Severity::Error);
            }
        }
        EXPECT_EQ(ageErrors, 2) << "one for the physics profile, one for the soft tissue";
    }
}

TEST(BodyValidate, ReportIsSortedErrorsFirstThenPathThenCode)
{
    CaseSetup setup(humanoid());
    // A mix of errors and warnings in different places.
    setup.mPackage.mIdentity.mVersion = "1.0";
    setup.mPackage.mTargets.push_back(Body::Target{});
    setup.mPackage.mBinding.mRestSkeleton.reset();
    setup.mPackage.mMorphSet->mMorphs[0].mDefault = 9.0;
    setup.mPackage.mMeshes[3].mSummary.reset();
    setInfluences(*setup.mPackage.mMeshes[4].mSummary, 0, { { 0, 0.5 } });
    setup.mPackage.mRegionMap->mMesh = "nope";
    const Body::Report report = run(setup);
    ASSERT_GE(report.mFindings.size(), 6u) << describe(report);
    EXPECT_GE(report.warningCount(), 3);
    EXPECT_GE(report.errorCount(), 4);
    for (std::size_t i = 1; i < report.mFindings.size(); ++i)
    {
        const Body::Finding& before = report.mFindings[i - 1];
        const Body::Finding& after = report.mFindings[i];
        if (before.mSeverity != after.mSeverity)
        {
            EXPECT_EQ(before.mSeverity, Body::Severity::Error) << "warnings must come after errors\n"
                                                               << describe(report);
            continue;
        }
        const auto key
            = [](const Body::Finding& finding) { return std::tie(finding.mPath, finding.mCode, finding.mMessage); };
        EXPECT_LE(key(before), key(after)) << "not sorted at " << i << "\n" << describe(report);
    }
    // No error comes after the first warning.
    bool seenWarning = false;
    for (const Body::Finding& finding : report.mFindings)
    {
        if (finding.mSeverity == Body::Severity::Warning)
        {
            seenWarning = true;
        }
        else
        {
            EXPECT_FALSE(seenWarning) << describe(report);
        }
    }
}

TEST(BodyValidate, ReportsAreDeterministicAndIndependentOfDefectOrder)
{
    // Six independent defects applied in every possible order to the same fixture give the same report bytes.
    std::vector<Mutator> defects;
    defects.push_back([](CaseSetup& x) { x.mPackage.mIdentity.mVersion = "1.0"; });
    defects.push_back([](CaseSetup& x) { x.mPackage.mTargets.push_back(Body::Target{}); });
    defects.push_back([](CaseSetup& x) { x.mPackage.mMeshes[3].mSummary.reset(); });
    defects.push_back([](CaseSetup& x) { setInfluences(*x.mPackage.mMeshes[4].mSummary, 0, { { 0, 0.5 } }); });
    defects.push_back([](CaseSetup& x) { x.mPackage.mMorphSet->mMorphs[0].mDefault = 9.0; });
    defects.push_back([](CaseSetup& x) { x.mPackage.mRegionMap->mMesh = "nope"; });

    std::vector<std::size_t> order(defects.size());
    std::iota(order.begin(), order.end(), 0);
    std::string reference;
    int permutations = 0;
    do
    {
        CaseSetup setup(humanoid());
        for (const std::size_t index : order)
        {
            defects[index](setup);
        }
        const Body::Report report = run(setup);
        const std::string text = wholeText(report);
        // Validating the same package twice also gives the same bytes.
        EXPECT_EQ(text, wholeText(run(setup)));
        if (permutations == 0)
        {
            reference = text;
            EXPECT_GE(report.mFindings.size(), 5u);
        }
        else
        {
            ASSERT_EQ(text, reference) << "order differs at permutation " << permutations;
        }
        ++permutations;
    } while (std::next_permutation(order.begin(), order.end()));
    EXPECT_EQ(permutations, 720);
}

TEST(BodyValidate, ReportTextFormat)
{
    Body::Report report;
    report.mFindings.push_back(
        { "BODY-SKIN-002", Body::Severity::Error, "/meshes/0/summary/influenceOffsets/3", "weights sum to 0.9" });
    report.mFindings.push_back(
        { "BODY-BIND-014", Body::Severity::Warning, "/binding/restSkeleton", "no rest skeleton" });
    report.mHumanoidGait = true;
    EXPECT_EQ(Body::formatReportText(report),
        "error BODY-SKIN-002 /meshes/0/summary/influenceOffsets/3: weights sum to 0.9\n"
        "warning BODY-BIND-014 /binding/restSkeleton: no rest skeleton\n"
        "1 error(s), 1 warning(s), humanoidGait=true\n");
    EXPECT_FALSE(report.ok());
    EXPECT_EQ(report.errorCount(), 1);
    EXPECT_EQ(report.warningCount(), 1);

    Body::Report empty;
    EXPECT_EQ(Body::formatReportText(empty), "0 error(s), 0 warning(s), humanoidGait=false\n");
    EXPECT_TRUE(empty.ok());
    EXPECT_EQ(empty.errorCount(), 0);
    EXPECT_EQ(empty.warningCount(), 0);

    Body::Report warningsOnly;
    warningsOnly.mFindings.push_back({ "BODY-MESH-001", Body::Severity::Warning, "/meshes/0", "no summary" });
    EXPECT_TRUE(warningsOnly.ok());
    EXPECT_EQ(warningsOnly.warningCount(), 1);
}

TEST(BodyValidate, ReportJsonFormat)
{
    Body::Report report;
    report.mFindings.push_back(
        { "BODY-SKIN-002", Body::Severity::Error, "/meshes/0/summary/influenceOffsets/3", "weights sum to 0.9" });
    report.mFindings.push_back(
        { "BODY-BIND-014", Body::Severity::Warning, "/binding/restSkeleton", "no rest skeleton" });
    report.mHumanoidGait = true;
    EXPECT_EQ(Body::formatReportJson(report),
        "{\n"
        "  \"findings\": [\n"
        "    {\"code\": \"BODY-SKIN-002\", \"severity\": \"error\", \"path\": "
        "\"/meshes/0/summary/influenceOffsets/3\", "
        "\"message\": \"weights sum to 0.9\"},\n"
        "    {\"code\": \"BODY-BIND-014\", \"severity\": \"warning\", \"path\": \"/binding/restSkeleton\", "
        "\"message\": \"no rest skeleton\"}\n"
        "  ],\n"
        "  \"humanoidGait\": true,\n"
        "  \"errors\": 1,\n"
        "  \"warnings\": 1\n"
        "}\n");

    Body::Report empty;
    EXPECT_EQ(
        Body::formatReportJson(empty), "{\"findings\": [], \"humanoidGait\": false, \"errors\": 0, \"warnings\": 0}\n");
}

TEST(BodyValidate, ValidatorNeverThrowsOnGarbagePackages)
{
    // Empty package, huge indices, zero vertex counts: a report, never an exception or a crash.
    EXPECT_NO_THROW({
        const Body::Report report = Body::validateBodyPackage(Body::BodyPackage{});
        EXPECT_FALSE(report.ok());
    });
    Body::BodyPackage package = humanoid();
    package.mMeshes[0].mSummary->mVertexCount = 0;
    package.mMeshes[1].mSummary->mTriangleCount = 0xFFFFFFFFu;
    package.mMeshes[4].mSummary->mInfluenceOffsets = { 0xFFFFFFFFu, 0, 7 };
    package.mMorphSet->mMorphs[0].mTargets[0].mVertices = { 0xFFFFFFFFu };
    package.mRegionMap->mRegions[0].mRanges = { { 0xFFFFFFFFu, 0xFFFFFFFFu }, { 5, 3 } };
    package.mBinding.mRestSkeleton->at(5).mParent = 1000;
    package.mBinding.mRestSkeleton->at(6).mParent = -7;
    EXPECT_NO_THROW({
        const Body::Report report = Body::validateBodyPackage(package);
        EXPECT_FALSE(report.ok());
    });
}

TEST(BodyValidate, SkinnedMeshWithoutInfluenceDataOnlyWarns)
{
    Body::BodyPackage package = humanoid();
    package.mMeshes[4].mSummary->mInfluenceOffsets.clear();
    package.mMeshes[4].mSummary->mInfluences.clear();
    ASSERT_FALSE(package.mMeshes[4].mSummary->mBoneNames.empty());
    const Body::Report report = Body::validateBodyPackage(package);
    EXPECT_TRUE(report.ok()) << describe(report);
    ASSERT_EQ(report.mFindings.size(), 1u) << describe(report);
    EXPECT_EQ(report.mFindings[0].mCode, "BODY-MESH-002");
    EXPECT_EQ(report.mFindings[0].mSeverity, Body::Severity::Warning);
    EXPECT_EQ(report.mFindings[0].mPath, "/meshes/4/summary/influenceOffsets");
}
