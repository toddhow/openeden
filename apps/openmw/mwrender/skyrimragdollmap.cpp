#include "skyrimragdollmap.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <mutex>
#include <sstream>
#include <system_error>

#include <components/body/skeletonsuggest.hpp>
#include <components/debug/debuglog.hpp>

namespace MWRender::SkyrimRagdoll
{
    namespace
    {
        // The canonical profile the map targets (the rig's own: CreativeMotion's humanoid).
        constexpr const char* sProfile = "humanoid-to-eden";

        // Row-major 3x3 rotation to a unit quaternion (x, y, z, w). A damaged matrix gives the identity.
        std::array<double, 4> quaternionOf(const std::array<float, 9>& m)
        {
            const double trace = static_cast<double>(m[0]) + m[4] + m[8];
            std::array<double, 4> q{ 0.0, 0.0, 0.0, 1.0 };
            if (trace > 0.0)
            {
                const double s = std::sqrt(trace + 1.0) * 2.0;
                q = { (m[7] - m[5]) / s, (m[2] - m[6]) / s, (m[3] - m[1]) / s, 0.25 * s };
            }
            else if (m[0] > m[4] && m[0] > m[8])
            {
                const double s = std::sqrt(1.0 + m[0] - m[4] - m[8]) * 2.0;
                q = { 0.25 * s, (m[1] + m[3]) / s, (m[2] + m[6]) / s, (m[7] - m[5]) / s };
            }
            else if (m[4] > m[8])
            {
                const double s = std::sqrt(1.0 + m[4] - m[0] - m[8]) * 2.0;
                q = { (m[1] + m[3]) / s, 0.25 * s, (m[5] + m[7]) / s, (m[2] - m[6]) / s };
            }
            else
            {
                const double s = std::sqrt(1.0 + m[8] - m[0] - m[4]) * 2.0;
                q = { (m[2] + m[6]) / s, (m[5] + m[7]) / s, 0.25 * s, (m[3] - m[1]) / s };
            }
            const double length = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
            if (!(length > 1e-6) || !std::isfinite(length))
                return { 0.0, 0.0, 0.0, 1.0 };
            for (double& value : q)
                value /= length;
            return q;
        }

        // Case-insensitive search by the engine's own rule (Body::engineBoneKey).
        std::uint32_t findNode(const NifScene::Skeleton::Plan& plan, const std::string& name)
        {
            const std::string key = Body::engineBoneKey(name);
            for (std::size_t i = 0; i < plan.mNodes.size(); ++i)
                if (Body::engineBoneKey(plan.mNodes[i].mName) == key)
                    return static_cast<std::uint32_t>(i);
            return NifScene::sNone;
        }

        std::string shortDigest(const std::string& digest)
        {
            return digest.substr(0, 16);
        }
    }

    std::filesystem::path mapFolder(const std::filesystem::path& userData)
    {
        return userData / "openeden" / "skeletonmaps";
    }

    namespace
    {
        std::mutex sFolderMutex;
        std::filesystem::path sFolder;
    }

    void setConfiguredMapFolder(const std::filesystem::path& folder)
    {
        std::scoped_lock lock(sFolderMutex);
        sFolder = folder;
    }

    std::filesystem::path configuredMapFolder()
    {
        std::scoped_lock lock(sFolderMutex);
        return sFolder;
    }

    std::vector<Body::RestBone> restBonesOf(const NifScene::Skeleton::Plan& plan)
    {
        std::vector<Body::RestBone> bones;
        bones.reserve(plan.mNodes.size());
        for (const NifScene::Skeleton::BoneNode& node : plan.mNodes)
        {
            Body::RestBone bone;
            bone.mName = node.mName;
            bone.mParent = node.mParent == NifScene::sNone ? -1 : static_cast<int>(node.mParent);
            for (std::size_t i = 0; i < 3; ++i)
                bone.mTranslation[i] = node.mLocal.mTranslation[i];
            bone.mRotation = quaternionOf(node.mLocal.mRotation);
            bones.push_back(std::move(bone));
        }
        return bones;
    }

    Body::SkeletonMap suggestMap(const NifScene::Skeleton::Plan& plan)
    {
        Body::SuggestInput input;
        input.mName = "skyrim-actor";
        input.mProfile = sProfile;
        input.mSkeleton = restBonesOf(plan);
        input.mHasMasses = false;
        return Body::suggestSkeletonMap(input).mMap;
    }

    bool BoneTable::build(const Body::SkeletonMap& map, const NifScene::Skeleton::Plan& plan, std::string& message)
    {
        const std::span<const RagdollBoneMapping> rig = ragdollBoneMappings();
        mEngineBones.assign(rig.size(), std::string());
        mMissing.clear();
        mResolved = 0;
        for (std::size_t i = 0; i < rig.size(); ++i)
        {
            const auto role = std::find_if(map.mRoles.begin(), map.mRoles.end(),
                [&](const Body::MapRole& candidate) { return candidate.mRole == rig[i].mRigBone; });
            if (role == map.mRoles.end() || findNode(plan, role->mBone) == NifScene::sNone)
            {
                mMissing.emplace_back(rig[i].mRigBone);
                continue;
            }
            mEngineBones[i] = Body::engineBoneKey(role->mBone);
            ++mResolved;
        }
        if (mEngineBones.empty() || mEngineBones[0].empty())
        {
            message = "the skeleton map has no pelvis bone";
            return false;
        }
        if (mResolved < kMinResolvedBones)
        {
            message = "only " + std::to_string(mResolved) + " of " + std::to_string(rig.size())
                + " rig bones have a bone in the skeleton map";
            return false;
        }
        // The pelvis bone's parent turns the ragdoll pelvis' world position into a local one.
        mRootBone.clear();
        const std::uint32_t pelvis = findNode(plan, mEngineBones[0]);
        if (pelvis != NifScene::sNone && plan.mNodes[pelvis].mParent != NifScene::sNone)
            mRootBone = Body::engineBoneKey(plan.mNodes[plan.mNodes[pelvis].mParent].mName);
        mMappings.clear();
        mMappings.reserve(rig.size());
        for (std::size_t i = 0; i < rig.size(); ++i)
            mMappings.push_back(RagdollBoneMapping{ rig[i].mRigBone, mEngineBones[i].c_str() });
        return true;
    }

    std::shared_ptr<const BoneTable> makeBoneTable(
        const NifScene::Skeleton::Plan& plan, const std::filesystem::path& folder, std::string& message)
    {
        try
        {
            const std::string digest = Body::skeletonDigest(restBonesOf(plan));
            const std::filesystem::path file = folder / (shortDigest(digest) + ".skeletonmap.json");

            Body::SkeletonMap map;
            std::string origin = "baked";
            bool haveMap = false;
            std::error_code ec;
            if (!folder.empty() && std::filesystem::exists(file, ec))
            {
                const Body::SkeletonMapResult read = Body::readSkeletonMapFile(file);
                if (read.mValue && read.mValue->mSource.mDigest == digest && read.mValue->mProfile == sProfile)
                {
                    map = *read.mValue;
                    origin = "file";
                    haveMap = true;
                }
                else
                {
                    Log(Debug::Warning) << "Skeleton map " << file.string()
                                        << (read.mValue ? " is for another skeleton" : " cannot be read: ")
                                        << (read.mValue ? std::string() : read.mError.toString())
                                        << "; it is baked again";
                }
            }
            if (!haveMap)
            {
                map = suggestMap(plan);
                if (!folder.empty())
                {
                    std::filesystem::create_directories(folder, ec);
                    std::ofstream out(file, std::ios::binary | std::ios::trunc);
                    const std::string text = Body::writeSkeletonMap(map);
                    out.write(text.data(), static_cast<std::streamsize>(text.size()));
                    if (!out)
                        Log(Debug::Warning) << "Skeleton map: " << file.string() << " could not be written";
                }
            }
            auto table = std::make_shared<BoneTable>();
            if (!table->build(map, plan, message))
                return {};
            table->setOrigin(digest, origin);
            return table;
        }
        catch (const std::exception& e)
        {
            message = e.what();
            return {};
        }
    }
}
