#include "bodyfixtures.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

namespace BodyTest
{
    namespace
    {
        struct BoneSpec
        {
            const char* mTarget;
            const char* mSource; // nullptr: not mapped (listed in unmappedBones)
            int mParent;
            std::array<double, 3> mTranslation;
        };

        Body::Binding makeBinding(
            const std::string& profileName, const std::vector<BoneSpec>& bones, std::vector<std::string> legChain)
        {
            Body::Binding binding;
            opas::anim::SkeletonProfile profile;
            profile.name = profileName;
            for (const BoneSpec& bone : bones)
            {
                if (bone.mSource != nullptr)
                {
                    profile.bones.push_back({ bone.mSource, bone.mTarget });
                }
                else
                {
                    binding.mUnmappedBones.emplace_back(bone.mTarget);
                }
            }
            profile.legChain = std::move(legChain);
            binding.mSkeletonProfile = std::move(profile);
            binding.mRootBone = bones.front().mTarget;
            std::vector<Body::RestBone> rest;
            for (const BoneSpec& bone : bones)
            {
                Body::RestBone restBone;
                restBone.mName = bone.mTarget;
                restBone.mParent = bone.mParent;
                restBone.mTranslation = bone.mTranslation;
                rest.push_back(std::move(restBone));
            }
            binding.mRestSkeleton = std::move(rest);
            return binding;
        }

        Body::Identity makeIdentity(const char* id, const char* name)
        {
            Body::Identity identity;
            identity.mId = id;
            identity.mName = name;
            identity.mVersion = "1.0.0";
            identity.mAuthor = "OpenEden tests";
            identity.mLicense = "CC0-1.0";
            identity.mAgeClass = Body::AgeClass::Unspecified;
            return identity;
        }

        Body::MeshEntry makeMesh(const char* id, const char* path, int lod, Body::MeshRole role, const char* partType,
            const char* attachBone, Body::MeshSummary summary)
        {
            Body::MeshEntry mesh;
            mesh.mId = id;
            mesh.mPath = path;
            mesh.mFormat = Body::MeshFormat::Gltf;
            mesh.mLod = lod;
            mesh.mRole = role;
            mesh.mPartType = partType;
            mesh.mAttachBone = attachBone;
            mesh.mSummary = std::move(summary);
            return mesh;
        }

        const std::vector<BoneSpec>& humanoidBones()
        {
            static const std::vector<BoneSpec> bones = {
                { "Bip Root", nullptr, -1, { 0, 0, 0 } },
                { "Bip Pelvis", "Pelvis", 0, { 0, 0, 0.95 } },
                { "Bip Spine", "Spine", 1, { 0, 0, 0.15 } },
                { "Bip Chest", "Chest", 2, { 0, 0, 0.2 } },
                { "Bip Neck", "Neck", 3, { 0, 0, 0.25 } },
                { "Bip Head", "Head", 4, { 0, 0, 0.1 } },
                { "Bip LUpperArm", "LeftUpperArm", 3, { 0.2, 0, 0.2 } },
                { "Bip LLowerArm", "LeftLowerArm", 6, { 0.28, 0, 0 } },
                { "Bip LHand", "LeftHand", 7, { 0.26, 0, 0 } },
                { "Bip RUpperArm", "RightUpperArm", 3, { -0.2, 0, 0.2 } },
                { "Bip RLowerArm", "RightLowerArm", 9, { -0.28, 0, 0 } },
                { "Bip RHand", "RightHand", 10, { -0.26, 0, 0 } },
                { "Bip LUpperLeg", "LeftUpperLeg", 1, { 0.1, 0, -0.05 } },
                { "Bip LLowerLeg", "LeftLowerLeg", 12, { 0, 0, -0.45 } },
                { "Bip LFoot", "LeftFoot", 13, { 0, 0, -0.42 } },
                { "Bip LToe", "LeftToe", 14, { 0, 0.12, -0.05 } },
                { "Bip RUpperLeg", "RightUpperLeg", 1, { -0.1, 0, -0.05 } },
                { "Bip RLowerLeg", "RightLowerLeg", 16, { 0, 0, -0.45 } },
                { "Bip RFoot", "RightFoot", 17, { 0, 0, -0.42 } },
                { "Bip RToe", "RightToe", 18, { 0, 0.12, -0.05 } },
            };
            return bones;
        }

        const std::vector<BoneSpec>& quadrupedBones()
        {
            static const std::vector<BoneSpec> bones = {
                { "Quad Root", nullptr, -1, { 0, 0, 0 } },
                { "Quad Body", "Body", 0, { 0, 0, 0.6 } },
                { "Quad Chest", "Chest", 1, { 0, 0.4, 0.05 } },
                { "Quad Neck", "Neck", 2, { 0, 0.25, 0.1 } },
                { "Quad Head", "Head", 3, { 0, 0.2, 0.05 } },
                { "Quad Tail1", "Tail1", 1, { 0, -0.4, 0 } },
                { "Quad Tail2", "Tail2", 5, { 0, -0.3, 0 } },
                { "Quad Tail3", "Tail3", 6, { 0, -0.3, 0 } },
                { "Quad Tail4", "Tail4", 7, { 0, -0.3, 0 } },
                { "Quad FLUpper", "FrontLeftUpper", 2, { 0.15, 0, -0.1 } },
                { "Quad FLLower", "FrontLeftLower", 9, { 0, 0, -0.3 } },
                { "Quad FLPaw", "FrontLeftPaw", 10, { 0, 0, -0.3 } },
                { "Quad FRUpper", "FrontRightUpper", 2, { -0.15, 0, -0.1 } },
                { "Quad FRLower", "FrontRightLower", 12, { 0, 0, -0.3 } },
                { "Quad FRPaw", "FrontRightPaw", 13, { 0, 0, -0.3 } },
                { "Quad BLUpper", "BackLeftUpper", 1, { 0.15, -0.3, -0.1 } },
                { "Quad BLLower", "BackLeftLower", 15, { 0, 0, -0.3 } },
                { "Quad BLPaw", "BackLeftPaw", 16, { 0, 0, -0.3 } },
                { "Quad BRUpper", "BackRightUpper", 1, { -0.15, -0.3, -0.1 } },
                { "Quad BRLower", "BackRightLower", 18, { 0, 0, -0.3 } },
                { "Quad BRPaw", "BackRightPaw", 19, { 0, 0, -0.3 } },
            };
            return bones;
        }
    }

    Body::MeshSummary makeSummary(std::uint32_t vertexCount, const std::vector<std::string>& boneNames,
        std::array<double, 3> boundsMin, std::array<double, 3> boundsMax)
    {
        Body::MeshSummary summary;
        summary.mVertexCount = vertexCount;
        summary.mTriangleCount = vertexCount - 2;
        summary.mBoundsMin = boundsMin;
        summary.mBoundsMax = boundsMax;
        summary.mBoneNames = boneNames;
        summary.mUvSetCount = 1;
        for (std::uint32_t i = 1; i + 1 < vertexCount; ++i)
        {
            summary.mTriangleIndices.push_back(0);
            summary.mTriangleIndices.push_back(i);
            summary.mTriangleIndices.push_back(i + 1);
        }
        if (!boneNames.empty())
        {
            const std::uint32_t boneCount = static_cast<std::uint32_t>(boneNames.size());
            summary.mInfluenceOffsets.push_back(0);
            for (std::uint32_t v = 0; v < vertexCount; ++v)
            {
                if (v % 2 == 0)
                {
                    summary.mInfluences.push_back({ v % boneCount, 1.0 });
                }
                else
                {
                    summary.mInfluences.push_back({ v % boneCount, 0.25 });
                    summary.mInfluences.push_back({ (v + 1) % boneCount, 0.75 });
                }
                summary.mInfluenceOffsets.push_back(static_cast<std::uint32_t>(summary.mInfluences.size()));
            }
        }
        return summary;
    }

    void setInfluences(
        Body::MeshSummary& summary, std::uint32_t vertex, const std::vector<Body::MeshSummary::Influence>& influences)
    {
        std::vector<std::vector<Body::MeshSummary::Influence>> perVertex(summary.mVertexCount);
        for (std::uint32_t v = 0; v < summary.mVertexCount; ++v)
        {
            for (std::uint32_t i = summary.mInfluenceOffsets[v]; i < summary.mInfluenceOffsets[v + 1]; ++i)
            {
                perVertex[v].push_back(summary.mInfluences[i]);
            }
        }
        perVertex[vertex] = influences;
        summary.mInfluences.clear();
        summary.mInfluenceOffsets.assign(1, 0);
        for (const auto& list : perVertex)
        {
            summary.mInfluences.insert(summary.mInfluences.end(), list.begin(), list.end());
            summary.mInfluenceOffsets.push_back(static_cast<std::uint32_t>(summary.mInfluences.size()));
        }
    }

    Body::BodyPackage humanoid()
    {
        Body::BodyPackage package;
        package.mIdentity = makeIdentity("humanoid", "Test humanoid");
        package.mBinding
            = makeBinding("humanoid-test", humanoidBones(), { "Bip LUpperLeg", "Bip LLowerLeg", "Bip LFoot" });

        const std::vector<std::string> bodyBones = { "Bip Pelvis", "Bip Spine", "Bip Chest" };
        package.mMeshes.push_back(makeMesh("body", "mesh/body.glb", 0, Body::MeshRole::Skin, "torso", "Bip Spine",
            makeSummary(10, bodyBones, { -0.3, -0.15, 0 }, { 0.3, 0.15, 1.8 })));
        package.mMeshes.push_back(makeMesh("body_lod1", "mesh/body_lod1.glb", 1, Body::MeshRole::Skin, "torso",
            "Bip Spine", makeSummary(6, bodyBones, { -0.3, -0.15, 0 }, { 0.3, 0.15, 1.8 })));
        package.mMeshes.push_back(makeMesh("head", "mesh/head.glb", 0, Body::MeshRole::Head, "head", "Bip Head",
            makeSummary(5, { "Bip Neck", "Bip Head" }, { -0.1, -0.1, 1.6 }, { 0.1, 0.12, 1.8 })));
        package.mMeshes.push_back(makeMesh("hair", "mesh/hair.glb", 0, Body::MeshRole::Hair, "hair", "Bip Head",
            makeSummary(4, {}, { -0.11, -0.11, 1.65 }, { 0.11, 0.13, 1.85 })));
        package.mMeshes.push_back(makeMesh("hands", "mesh/hands.glb", 0, Body::MeshRole::Skin, "hands", "Bip LHand",
            makeSummary(6, { "Bip LHand", "Bip RHand" }, { -0.6, -0.05, 0.8 }, { 0.6, 0.05, 1.2 })));

        Body::MorphSet morphs;
        morphs.mOrigin = Body::Origin::Authored;
        Body::Morph height;
        height.mId = "height";
        height.mRangeMin = -1;
        height.mRangeMax = 1;
        height.mDefault = 0;
        Body::MorphTarget target;
        target.mMesh = "body";
        for (std::uint32_t v = 0; v < 10; ++v)
        {
            target.mVertices.push_back(v);
            target.mDeltas.push_back({ 0, 0, 0.0625 * (v + 1) });
        }
        height.mTargets.push_back(std::move(target));
        morphs.mMorphs.push_back(std::move(height));
        package.mMorphSet = std::move(morphs);

        Body::RegionMap regions;
        regions.mOrigin = Body::Origin::Authored;
        regions.mMesh = "body";
        regions.mRegions.push_back({ "torso", { { 0, 4 } } });
        regions.mRegions.push_back({ "legs", { { 4, 8 } } });
        package.mRegionMap = std::move(regions);

        Body::Proxy proxy;
        proxy.mOrigin = Body::Origin::Authored;
        proxy.mCapsules.push_back({ "Bip Spine", { 0, 0, 0 }, { 0, 0, 0.2 }, 0.15 });
        proxy.mCapsules.push_back({ "Bip Head", { 0, 0, 0 }, { 0, 0, 0.1 }, 0.1 });
        package.mProxy = std::move(proxy);
        return package;
    }

    Body::BodyPackage tailedQuadruped()
    {
        Body::BodyPackage package;
        package.mIdentity = makeIdentity("tailed_quadruped", "Test tailed quadruped");
        package.mBinding = makeBinding("quadruped-test", quadrupedBones(), {});

        const std::vector<std::string> bodyBones = { "Quad Body", "Quad Chest", "Quad FLUpper", "Quad BLUpper" };
        package.mMeshes.push_back(makeMesh("body", "mesh/quad_body.glb", 0, Body::MeshRole::Skin, "body", "Quad Body",
            makeSummary(12, bodyBones, { -0.2, -0.7, 0 }, { 0.2, 0.7, 0.8 })));
        package.mMeshes.push_back(makeMesh("head", "mesh/quad_head.glb", 0, Body::MeshRole::Head, "head", "Quad Head",
            makeSummary(5, { "Quad Neck", "Quad Head" }, { -0.1, 0.5, 0.6 }, { 0.1, 0.8, 0.9 })));
        package.mMeshes.push_back(makeMesh("tail", "mesh/quad_tail.glb", 0, Body::MeshRole::Skin, "tail", "Quad Tail1",
            makeSummary(8, { "Quad Tail1", "Quad Tail2", "Quad Tail3", "Quad Tail4" }, { -0.05, -1.5, 0.5 },
                { 0.05, -0.6, 0.7 })));

        Body::Proxy proxy;
        proxy.mOrigin = Body::Origin::Authored;
        proxy.mCapsules.push_back({ "Quad FLLower", { 0, 0, 0 }, { 0, 0, -0.3 }, 0.05 });
        proxy.mCapsules.push_back({ "Quad BLLower", { 0, 0, 0 }, { 0, 0, -0.3 }, 0.05 });
        package.mProxy = std::move(proxy);

        Body::PhysicsProfile physics;
        physics.mOrigin = Body::Origin::Authored;
        physics.mShapes.push_back({ "Quad Body", Body::ShapeKind::Capsule, { 0.2, 0.6, 0.2 } });
        physics.mShapes.push_back({ "Quad Head", Body::ShapeKind::Sphere, { 0.1, 0.1, 0.1 } });
        physics.mMasses.push_back({ "Quad Body", 40 });
        physics.mMasses.push_back({ "Quad Head", 5 });
        physics.mJointLimits.push_back({ "Quad FLLower", -1.0, 1.0, 0.0 });
        physics.mJointLimits.push_back({ "Quad BLLower", -1.0, 1.0, 0.0 });
        physics.mRestrictedTier = false;
        package.mPhysicsProfile = std::move(physics);

        Body::Measurements measurements;
        measurements.mOrigin = Body::Origin::Authored;
        measurements.mEntries.push_back({ "bodyLength", 1.4 });
        measurements.mEntries.push_back({ "tailLength", 1.2 });
        package.mMeasurements = std::move(measurements);

        Body::Landmarks landmarks;
        landmarks.mOrigin = Body::Origin::Authored;
        landmarks.mEntries.push_back({ "nose", "Quad Head", { 0, 0.15, 0 } });
        landmarks.mEntries.push_back({ "tailTip", "Quad Tail4", { 0, -0.3, 0 } });
        package.mLandmarks = std::move(landmarks);

        Body::SoftTissue soft;
        soft.mOrigin = Body::Origin::Authored;
        soft.mNodes.push_back({ "belly", "Quad Body", 2.0, 30.0, 0.5 });
        soft.mRestrictedTier = false;
        package.mSoftTissue = std::move(soft);
        return package;
    }

    Body::BodyPackage kitchenSink()
    {
        Body::BodyPackage package = tailedQuadruped();
        package.mIdentity.mId = "kitchen.sink-1_x";
        package.mIdentity.mAgeClass = Body::AgeClass::Adult;
        package.mIdentity.mContractVersion = { 1, 0 };
        package.mTargets.push_back({ std::string("human"), std::string("female"), std::nullopt, 5 });
        package.mTargets.push_back({ std::nullopt, std::nullopt, std::string("daedra"), -2 });
        package.mTargets.push_back({});
        package.mBinding.mSkeletonProfile.reset();
        package.mBinding.mSkeletonPath = "profiles/quadruped.skeletonprofile.json";
        package.mBinding.mRestSkeleton.reset();
        package.mMeshes[0].mFormat = Body::MeshFormat::Nif;
        package.mMeshes[1].mFormat = Body::MeshFormat::Summary;
        package.mMeshes[1].mSummary.reset();
        package.mMeshes[2].mPartType.reset();
        package.mMeshes[2].mAttachBone.reset();

        Body::MorphSet morphs;
        morphs.mOrigin = Body::Origin::Derived;
        Body::Morph morph;
        morph.mId = "fat";
        morph.mRangeMin = 0;
        morph.mRangeMax = 2.5;
        morph.mDefault = 0.1;
        morph.mTargets.push_back({ "body", { 1, 5, 9 }, { { 0.001, -0.002, 1e-7 }, { 0, 0, 0 }, { 1, 2, 3 } } });
        morphs.mMorphs.push_back(std::move(morph));
        package.mMorphSet = std::move(morphs);

        package.mProxy->mOrigin = Body::Origin::Derived;
        package.mPhysicsProfile->mRigPath = "rigs/quadruped.rig.json";
        package.mPhysicsProfile->mProfilePath = "rigs/quadruped.profile.json";
        package.mPhysicsProfile->mShapes.push_back({ "Quad Tail1", Body::ShapeKind::Box, { 0.1, 0.2, 0.3 } });
        package.mPhysicsProfile->mRestrictedTier = true;
        package.mMeasurements->mOrigin = Body::Origin::Missing;
        package.mMeasurements->mEntries.clear();
        package.mLandmarks->mOrigin = Body::Origin::Derived;
        package.mSoftTissue->mRestrictedTier = true;

        Body::RegionMap regions;
        regions.mOrigin = Body::Origin::Authored;
        regions.mMesh = "body";
        regions.mRegions.push_back({ "front", { { 0, 3 }, { 7, 8 } } });
        regions.mRegions.push_back({ "back", { { 3, 7 }, { 8, 10 } } });
        package.mRegionMap = std::move(regions);

        package.mProvenance.push_back(
            { "morphSet", std::string(64, 'a'), "edenbodytool", "0.1.0", "{\"strength\": 0.5}" });
        package.mProvenance.push_back({ "proxy", std::string(64, '0'), "edenbodytool", "0.1.0", "" });
        return package;
    }

    std::optional<Body::MeshSummary> FakeMeshSource::summarize(std::string_view path, std::string_view format) const
    {
        mCalls.emplace_back(std::string(path), std::string(format));
        const auto it = mSummaries.find(std::string(path));
        if (it == mSummaries.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    void moveSummariesToSource(Body::BodyPackage& package, FakeMeshSource& source)
    {
        for (Body::MeshEntry& mesh : package.mMeshes)
        {
            if (mesh.mSummary)
            {
                source.mSummaries[mesh.mPath] = *mesh.mSummary;
                mesh.mSummary.reset();
            }
        }
    }

    std::string readDataFile(const std::string& name)
    {
        const std::filesystem::path path = std::filesystem::path(OPENEDEN_BODY_TEST_DATA_DIR) / name;
        std::string text = readTextFile(path);
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        return text;
    }

    TempFolder::TempFolder()
    {
        static std::atomic<unsigned> counter{ 0 };
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        std::ostringstream name;
        name << "openeden_body_test_" << ticks << "_" << counter.fetch_add(1);
        mPath = std::filesystem::temp_directory_path() / name.str();
        std::filesystem::create_directories(mPath);
    }

    TempFolder::~TempFolder()
    {
        std::error_code error;
        std::filesystem::remove_all(mPath, error);
    }

    void writeTextFile(const std::filesystem::path& path, const std::string& text)
    {
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    std::string readTextFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    std::vector<std::string> codesOf(const Body::Report& report)
    {
        std::vector<std::string> codes;
        for (const Body::Finding& finding : report.mFindings)
        {
            codes.push_back(finding.mCode);
        }
        return codes;
    }

    const Body::Finding* findCode(const Body::Report& report, std::string_view code)
    {
        for (const Body::Finding& finding : report.mFindings)
        {
            if (finding.mCode == code)
            {
                return &finding;
            }
        }
        return nullptr;
    }

    std::string describe(const Body::Report& report)
    {
        return Body::formatReportText(report);
    }

    std::size_t lineOf(const std::string& text, std::string_view needle)
    {
        const std::size_t position = text.find(needle);
        if (position == std::string::npos)
        {
            return 0;
        }
        return 1
            + static_cast<std::size_t>(
                std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(position), '\n'));
    }

    std::string replaceFirst(const std::string& text, std::string_view needle, std::string_view replacement, bool* ok)
    {
        const std::size_t position = text.find(needle);
        if (position == std::string::npos)
        {
            *ok = false;
            return text;
        }
        std::string result = text;
        result.replace(position, needle.size(), replacement);
        return result;
    }

    std::string removeLineWith(const std::string& text, std::string_view needle, bool* ok)
    {
        const std::size_t position = text.find(needle);
        if (position == std::string::npos)
        {
            *ok = false;
            return text;
        }
        const std::size_t begin = text.rfind('\n', position);
        const std::size_t lineBegin = begin == std::string::npos ? 0 : begin + 1;
        const std::size_t end = text.find('\n', position);
        const std::size_t lineEnd = end == std::string::npos ? text.size() : end + 1;
        return text.substr(0, lineBegin) + text.substr(lineEnd);
    }

    std::string insertLineAfter(const std::string& text, std::string_view needle, std::string_view line, bool* ok)
    {
        const std::size_t position = text.find(needle);
        if (position == std::string::npos)
        {
            *ok = false;
            return text;
        }
        const std::size_t end = text.find('\n', position);
        const std::size_t insertAt = end == std::string::npos ? text.size() : end + 1;
        return text.substr(0, insertAt) + std::string(line) + "\n" + text.substr(insertAt);
    }
}
