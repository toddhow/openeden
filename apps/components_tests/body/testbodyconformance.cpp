// Task 0063 (AdvancedBodies B1), stage d: the conformance suite. Generated reference packages (a humanoid and a
// non-humanoid here; the legacy-assembled body is in testbodylegacy.cpp because it needs the game's store), the
// findings each must give, single-defect copies that must give exactly one code, and the folder/pack digest equality.
// This suite (BodyConformance) is what every later contract change must keep passing.

#include "bodyfixtures.hpp"

#include <components/body/bodyjson.hpp>
#include <components/body/bodypack.hpp>
#include <components/body/bodyvalidate.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace BodyTest;

    constexpr double sNaN = std::numeric_limits<double>::quiet_NaN();

    enum class Fixture
    {
        Humanoid,
        TailedQuadruped,
    };

    Body::BodyPackage makeFixture(Fixture fixture)
    {
        return fixture == Fixture::Humanoid ? humanoid() : tailedQuadruped();
    }

    struct Defect
    {
        std::string mName;
        Fixture mFixture;
        std::function<void(Body::BodyPackage&)> mMutate;
        std::vector<std::string> mExpectedCodes; // exactly these, nothing else (sorted before comparing)
    };

    std::vector<Defect> defects()
    {
        const Fixture h = Fixture::Humanoid;
        const Fixture q = Fixture::TailedQuadruped;
        std::vector<Defect> table;
        table.push_back({ "package contract newer", h,
            [](Body::BodyPackage& p) {
                p.mIdentity.mContractVersion = { Body::sContractMajor + 1, 0 };
            },
            { "BODY-STRUCT-001" } });
        table.push_back({ "invalid package id", h, [](Body::BodyPackage& p) { p.mIdentity.mId = "Bad Id"; },
            { "BODY-STRUCT-003" } });
        table.push_back({ "two meshes in one slot", h, [](Body::BodyPackage& p) { p.mMeshes[1].mLod = 0; },
            { "BODY-STRUCT-005" } });
        table.push_back({ "unsafe mesh path", h, [](Body::BodyPackage& p) { p.mMeshes[0].mPath = "../x.glb"; },
            { "BODY-STRUCT-006" } });
        table.push_back({ "version without patch", h, [](Body::BodyPackage& p) { p.mIdentity.mVersion = "1.0"; },
            { "BODY-STRUCT-007" } });
        table.push_back({ "empty target", h, [](Body::BodyPackage& p) { p.mTargets.push_back(Body::Target{}); },
            { "BODY-TARGET-001" } });
        table.push_back({ "missing proxy with capsules", q,
            [](Body::BodyPackage& p) { p.mProxy->mOrigin = Body::Origin::Missing; }, { "BODY-ORIGIN-001" } });
        table.push_back({ "derived morph set without provenance", h,
            [](Body::BodyPackage& p) { p.mMorphSet->mOrigin = Body::Origin::Derived; }, { "BODY-PROV-001" } });
        table.push_back({ "provenance for an authored proxy", h,
            [](Body::BodyPackage& p) {
                p.mProvenance.push_back({ "proxy", std::string(64, 'a'), "t", "1.0.0", "" });
            },
            { "BODY-PROV-002" } });
        table.push_back({ "profile without a name", h,
            [](Body::BodyPackage& p) { p.mBinding.mSkeletonProfile->name = ""; }, { "BODY-BIND-001" } });
        table.push_back({ "unmapped bone listed twice", h,
            [](Body::BodyPackage& p) { p.mBinding.mUnmappedBones.push_back("Bip Root"); }, { "BODY-BIND-003" } });
        table.push_back({ "skin uses an unmapped bone", h,
            [](Body::BodyPackage& p) { p.mMeshes[0].mSummary->mBoneNames.push_back("Bip Ghost"); },
            { "BODY-BIND-004" } });
        table.push_back({ "no rest skeleton", h, [](Body::BodyPackage& p) { p.mBinding.mRestSkeleton.reset(); },
            { "BODY-BIND-014" } });
        table.push_back({ "weights not summing to one", h,
            [](Body::BodyPackage& p) {
                setInfluences(*p.mMeshes[4].mSummary, 0, { { 0, 0.9 } });
            },
            { "BODY-SKIN-002" } });
        table.push_back({ "bone index out of range", h,
            [](Body::BodyPackage& p) {
                setInfluences(*p.mMeshes[4].mSummary, 0, { { 7, 1.0 } });
            },
            { "BODY-SKIN-004" } });
        table.push_back({ "triangle index out of range", h,
            [](Body::BodyPackage& p) { p.mMeshes[4].mSummary->mTriangleIndices[1] = 600; }, { "BODY-GEOM-003" } });
        table.push_back({ "triangle index count", h,
            [](Body::BodyPackage& p) { p.mMeshes[4].mSummary->mTriangleIndices.pop_back(); }, { "BODY-GEOM-004" } });
        table.push_back({ "region gap", h, [](Body::BodyPackage& p) { p.mRegionMap->mRegions[1].mRanges[0].mEnd = 7; },
            { "BODY-GEOM-005" } });
        table.push_back({ "region overlap", h,
            [](Body::BodyPackage& p) { p.mRegionMap->mRegions[1].mRanges[0].mBegin = 3; }, { "BODY-GEOM-006" } });
        table.push_back({ "region range past the end", h,
            [](Body::BodyPackage& p) { p.mRegionMap->mRegions[1].mRanges[0].mEnd = 9; }, { "BODY-GEOM-007" } });
        table.push_back({ "region map without a mesh", h, [](Body::BodyPackage& p) { p.mRegionMap->mMesh = "nope"; },
            { "BODY-GEOM-008" } });
        table.push_back({ "mesh without a summary", h, [](Body::BodyPackage& p) { p.mMeshes[4].mSummary.reset(); },
            { "BODY-MESH-001" } });
        table.push_back({ "morph vertex out of range", h,
            [](Body::BodyPackage& p) { p.mMorphSet->mMorphs[0].mTargets[0].mVertices.back() = 99; },
            { "BODY-MORPH-001" } });
        table.push_back({ "morph delta NaN", h,
            [](Body::BodyPackage& p) { p.mMorphSet->mMorphs[0].mTargets[0].mDeltas[0][0] = sNaN; },
            { "BODY-MORPH-002" } });
        table.push_back({ "morph default outside range", h,
            [](Body::BodyPackage& p) { p.mMorphSet->mMorphs[0].mDefault = 3.0; }, { "BODY-MORPH-003" } });
        table.push_back({ "duplicate morph id", h,
            [](Body::BodyPackage& p) { p.mMorphSet->mMorphs.push_back(p.mMorphSet->mMorphs[0]); },
            { "BODY-MORPH-004" } });
        table.push_back({ "morph target mesh unknown", h,
            [](Body::BodyPackage& p) { p.mMorphSet->mMorphs[0].mTargets[0].mMesh = "nope"; }, { "BODY-MORPH-005" } });
        table.push_back({ "morph vertices not increasing", h,
            [](Body::BodyPackage& p) {
                std::swap(
                    p.mMorphSet->mMorphs[0].mTargets[0].mVertices[2], p.mMorphSet->mMorphs[0].mTargets[0].mVertices[3]);
            },
            { "BODY-MORPH-006" } });
        table.push_back({ "capsule radius zero", h, [](Body::BodyPackage& p) { p.mProxy->mCapsules[0].mRadius = 0; },
            { "BODY-PROXY-001" } });
        table.push_back({ "capsule bone unknown", h,
            [](Body::BodyPackage& p) { p.mProxy->mCapsules[0].mBone = "Nope"; }, { "BODY-PROXY-002" } });
        table.push_back({ "shape size zero", q,
            [](Body::BodyPackage& p) { p.mPhysicsProfile->mShapes[0].mSize[0] = 0; }, { "BODY-PHYS-001" } });
        table.push_back({ "mass zero", q, [](Body::BodyPackage& p) { p.mPhysicsProfile->mMasses[0].mKilograms = 0; },
            { "BODY-PHYS-002" } });
        table.push_back({ "joint rest outside limits", q,
            [](Body::BodyPackage& p) { p.mPhysicsProfile->mJointLimits[0].mRest = 9.0; }, { "BODY-PHYS-003" } });
        table.push_back({ "physics bone unknown", q,
            [](Body::BodyPackage& p) { p.mPhysicsProfile->mShapes[0].mBone = "Nope"; }, { "BODY-PHYS-004" } });
        table.push_back({ "rig path without a reader", q,
            [](Body::BodyPackage& p) { p.mPhysicsProfile->mRigPath = "rigs/q.rig.json"; }, { "BODY-PHYS-010" } });
        table.push_back({ "negative measurement", q,
            [](Body::BodyPackage& p) { p.mMeasurements->mEntries[0].mValue = -1; }, { "BODY-MEAS-001" } });
        table.push_back({ "landmark on unknown bone", q,
            [](Body::BodyPackage& p) { p.mLandmarks->mEntries[0].mBone = "Nope"; }, { "BODY-LAND-001" } });
        table.push_back({ "soft tissue mass zero", q, [](Body::BodyPackage& p) { p.mSoftTissue->mNodes[0].mMass = 0; },
            { "BODY-SOFT-001" } });
        table.push_back({ "restricted tier without adult", q,
            [](Body::BodyPackage& p) { p.mPhysicsProfile->mRestrictedTier = true; }, { "BODY-AGE-001" } });
        return table;
    }

    std::vector<std::string> sortedCodes(const Body::Report& report)
    {
        std::vector<std::string> codes = codesOf(report);
        std::sort(codes.begin(), codes.end());
        return codes;
    }

    void expectFolderAndPackDigestsAgree(const Body::BodyPackage& package, const std::string& name)
    {
        SCOPED_TRACE(name);
        TempFolder source;
        TempFolder output;
        writeTextFile(source.path() / "body.json", Body::writeBodyPackage(package));
        std::string error;
        const std::optional<Body::PackageFiles> folderFiles = Body::readPackageFolder(source.path(), error);
        ASSERT_TRUE(folderFiles.has_value()) << error;
        const std::string folderDigest = Body::packageDigest(*folderFiles, error);
        ASSERT_FALSE(folderDigest.empty()) << error;

        const std::filesystem::path pack = output.path() / "fixture.edenpak";
        ASSERT_TRUE(Body::writePackageFile(*folderFiles, pack, error)) << error;
        const std::optional<Body::PackageFiles> packFiles = Body::readPackageFile(pack, error);
        ASSERT_TRUE(packFiles.has_value()) << error;
        EXPECT_EQ(packFiles->mFiles, folderFiles->mFiles);
        EXPECT_EQ(Body::packageDigest(*packFiles, error), folderDigest);
    }
}

TEST(BodyConformance, HumanoidIsCleanAndHasHumanoidGait)
{
    const Body::BodyPackage package = humanoid();
    const Body::Report report = Body::validateBodyPackage(package);
    EXPECT_TRUE(report.mFindings.empty()) << describe(report);
    EXPECT_TRUE(report.mHumanoidGait);
    EXPECT_GE(package.mBinding.mRestSkeleton->size(), 18u) << "about 20 bones";
    EXPECT_LE(package.mBinding.mRestSkeleton->size(), 24u);
    EXPECT_EQ(package.mMeshes.size(), 5u);
    ASSERT_TRUE(package.mRegionMap.has_value());
    ASSERT_TRUE(package.mMorphSet.has_value());
}

TEST(BodyConformance, TailedQuadrupedIsCleanAndHasNoHumanoidGait)
{
    const Body::BodyPackage package = tailedQuadruped();
    const Body::Report report = Body::validateBodyPackage(package);
    EXPECT_TRUE(report.mFindings.empty()) << describe(report);
    EXPECT_FALSE(report.mHumanoidGait);
    for (const opas::anim::BoneMapping& mapping : package.mBinding.mSkeletonProfile->bones)
    {
        EXPECT_NE(mapping.source, "Pelvis");
        EXPECT_NE(mapping.source, "LeftFoot");
        EXPECT_NE(mapping.source, "RightFoot");
    }
    // Four legs and a tail chain.
    int legBones = 0;
    int tailBones = 0;
    for (const Body::RestBone& bone : *package.mBinding.mRestSkeleton)
    {
        legBones += bone.mName.find("Upper") != std::string::npos || bone.mName.find("Lower") != std::string::npos
            || bone.mName.find("Paw") != std::string::npos;
        tailBones += bone.mName.find("Tail") != std::string::npos;
    }
    EXPECT_EQ(legBones, 12);
    EXPECT_GE(tailBones, 3);
}

TEST(BodyConformance, FixturesUseNoGameAssets)
{
    for (const Body::BodyPackage& package : { humanoid(), tailedQuadruped() })
    {
        for (const Body::MeshEntry& mesh : package.mMeshes)
        {
            EXPECT_EQ(mesh.mFormat, Body::MeshFormat::Gltf);
            EXPECT_EQ(mesh.mPath.substr(mesh.mPath.size() - 4), ".glb") << mesh.mPath;
        }
        EXPECT_EQ(package.mIdentity.mAgeClass, Body::AgeClass::Unspecified);
        EXPECT_FALSE(package.mPhysicsProfile && package.mPhysicsProfile->mRestrictedTier);
        EXPECT_FALSE(package.mSoftTissue && package.mSoftTissue->mRestrictedTier);
    }
}

TEST(BodyConformance, EverySingleDefectGivesExactlyItsCode)
{
    for (const Defect& defect : defects())
    {
        SCOPED_TRACE(defect.mName);
        Body::BodyPackage package = makeFixture(defect.mFixture);
        defect.mMutate(package);
        const Body::Report report = Body::validateBodyPackage(package);
        std::vector<std::string> expected = defect.mExpectedCodes;
        std::sort(expected.begin(), expected.end());
        EXPECT_EQ(sortedCodes(report), expected) << describe(report);
    }
}

TEST(BodyConformance, EveryDefectStillGivesItsCodeAfterAJsonRoundTrip)
{
    for (const Defect& defect : defects())
    {
        SCOPED_TRACE(defect.mName);
        if (defect.mName.find("NaN") != std::string::npos)
        {
            continue; // the canonical writer is not specified for non-finite numbers
        }
        Body::BodyPackage package = makeFixture(defect.mFixture);
        defect.mMutate(package);
        const Body::Report direct = Body::validateBodyPackage(package);
        const Body::BodyLoadResult loaded = Body::readBodyPackage(Body::writeBodyPackage(package), "x");
        if (!loaded.mValue.has_value())
        {
            // Some defects (NaN, unsafe paths, invalid ids) are refused by the loader before the validator.
            EXPECT_FALSE(loaded.mError.message.empty());
            continue;
        }
        EXPECT_EQ(sortedCodes(Body::validateBodyPackage(*loaded.mValue)), sortedCodes(direct));
    }
}

TEST(BodyConformance, ReportsAreByteIdenticalOnRepeat)
{
    for (const Defect& defect : defects())
    {
        SCOPED_TRACE(defect.mName);
        Body::BodyPackage package = makeFixture(defect.mFixture);
        defect.mMutate(package);
        const Body::Report first = Body::validateBodyPackage(package);
        const Body::Report second = Body::validateBodyPackage(package);
        EXPECT_EQ(Body::formatReportText(first), Body::formatReportText(second));
        EXPECT_EQ(Body::formatReportJson(first), Body::formatReportJson(second));
    }
}

TEST(BodyConformance, FolderAndPackGiveTheSameDigestForEveryFixture)
{
    expectFolderAndPackDigestsAgree(humanoid(), "humanoid");
    expectFolderAndPackDigestsAgree(tailedQuadruped(), "tailed_quadruped");
    expectFolderAndPackDigestsAgree(kitchenSink(), "kitchen_sink");
}

TEST(BodyConformance, DifferentFixturesHaveDifferentDigests)
{
    std::string error;
    Body::PackageFiles a;
    Body::PackageFiles b;
    const std::string textA = Body::writeBodyPackage(humanoid());
    const std::string textB = Body::writeBodyPackage(tailedQuadruped());
    a.mFiles["body.json"].assign(textA.begin(), textA.end());
    b.mFiles["body.json"].assign(textB.begin(), textB.end());
    EXPECT_NE(Body::packageDigest(a, error), Body::packageDigest(b, error));
}
