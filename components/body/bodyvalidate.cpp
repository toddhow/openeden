#include "bodyvalidate.hpp"

#include "bodyjson.hpp"

#include <opas/format/JsonSupport.hpp>
#include <opas/rig/ProfileFile.hpp>
#include <opas/rig/RigFile.hpp>
#include <opas/rig/Skeleton.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace Body
{
    namespace
    {
        // How far a rest rotation may be from unit length (and from identity above the animation root): the 1e-6 of
        // opas::rig::validateSkeleton (core/rig/src/Skeleton.cpp, kUnitTolerance) and of opas::format::detail.
        constexpr double sUnitQuatTolerance = 1e-6;
        // A single rule reports at most this many findings per scope (mesh, morph...), then one summary finding, so a
        // hostile or broken summary cannot make a report as large as the file.
        constexpr int sMaxFindingsPerScope = 64;
        // Canonical source names that make a skeleton "humanoid" for balance and stepping (bodyvalidate.hpp).
        constexpr std::string_view sGaitSources[] = { "Pelvis", "LeftFoot", "RightFoot" };

        // A message may carry names from an untrusted package. Control characters are escaped so that a finding is one
        // line and cannot send terminal escapes, and a long message is cut (at a UTF-8 boundary) and ends in "...".
        constexpr std::size_t sMaxMessageBytes = 200;

        std::string cleanMessage(std::string_view message)
        {
            std::string out;
            for (const char c : message)
            {
                const unsigned char u = static_cast<unsigned char>(c);
                if (u < 0x20 || u == 0x7F)
                {
                    constexpr std::string_view hex = "0123456789abcdef";
                    out += "\\x";
                    out += hex[u >> 4U];
                    out += hex[u & 0x0FU];
                }
                else
                {
                    out += c;
                }
            }
            if (out.size() > sMaxMessageBytes)
            {
                std::size_t cut = sMaxMessageBytes;
                while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0U) == 0x80U)
                {
                    --cut;
                }
                out.resize(cut);
                out += "...";
            }
            return out;
        }

        std::string fmt(double value)
        {
            if (!std::isfinite(value))
            {
                return std::isnan(value) ? "NaN" : (value > 0 ? "Infinity" : "-Infinity");
            }
            return opas::format::detail::number(value);
        }

        std::string at(const std::string& pointer, std::string_view key)
        {
            return pointer + "/" + std::string(key);
        }

        std::string at(const std::string& pointer, std::size_t index)
        {
            return pointer + "/" + std::to_string(index);
        }

        // The first four path segments: a finding's "scope" for the per-scope cap.
        std::string scopeOf(const std::string& path)
        {
            std::size_t slashes = 0;
            for (std::size_t i = 0; i < path.size(); ++i)
            {
                if (path[i] == '/' && ++slashes == 5)
                {
                    return path.substr(0, i);
                }
            }
            return path;
        }

        bool isFinitePositive(double value)
        {
            return std::isfinite(value) && value > 0;
        }

        bool isFiniteNonNegative(double value)
        {
            return std::isfinite(value) && value >= 0;
        }

        bool isVersionText(std::string_view version)
        {
            int parts = 0;
            std::size_t digits = 0;
            for (const char c : version)
            {
                if (c >= '0' && c <= '9')
                {
                    ++digits;
                }
                else if (c == '.' && digits > 0)
                {
                    ++parts;
                    digits = 0;
                }
                else
                {
                    return false;
                }
            }
            return digits > 0 && parts == 2;
        }

        class Validator
        {
        public:
            Validator(const BodyPackage& package, const ValidateOptions& options)
                : mPackage(package)
                , mOptions(options)
            {
            }

            Report run()
            {
                checkIdentity();
                checkTargets();
                checkBinding();
                checkMeshes();
                checkOriginsAndProvenance();
                checkRegionMap();
                checkWeldGroups();
                checkMorphs();
                checkProxy();
                checkPhysics();
                checkMeasurements();
                checkLandmarks();
                checkSoftTissue();

                Report report;
                report.mFindings = std::move(mFindings);
                report.mHumanoidGait = mHumanoidGait;
                std::stable_sort(
                    report.mFindings.begin(), report.mFindings.end(), [](const Finding& left, const Finding& right) {
                        const int leftRank = left.mSeverity == Severity::Error ? 0 : 1;
                        const int rightRank = right.mSeverity == Severity::Error ? 0 : 1;
                        return std::tie(leftRank, left.mPath, left.mCode, left.mMessage)
                            < std::tie(rightRank, right.mPath, right.mCode, right.mMessage);
                    });
                return report;
            }

        private:
            const BodyPackage& mPackage;
            const ValidateOptions& mOptions;
            std::vector<Finding> mFindings;
            std::map<std::string, int> mCounts;

            bool mHumanoidGait = false;
            bool mProfileKnown = false; // a skeleton profile was available (inline or read)
            std::set<std::string> mMappedBones; // profile targets and unmapped bones
            std::set<std::string> mKnownBones; // mMappedBones and the rest skeleton's bones
            std::vector<const MeshSummary*> mSummaries; // per mesh, null when none
            std::vector<std::optional<MeshSummary>> mFetched; // summaries obtained from the MeshSource

            void add(const char* code, Severity severity, const std::string& path, std::string message)
            {
                const std::string key = std::string(code) + "|" + scopeOf(path);
                const int count = ++mCounts[key];
                if (count > sMaxFindingsPerScope + 1)
                {
                    return;
                }
                if (count == sMaxFindingsPerScope + 1)
                {
                    message = "further findings of this kind below " + scopeOf(path) + " are not listed";
                }
                mFindings.push_back(Finding{ std::string("BODY-") + code, severity, path, cleanMessage(message) });
            }

            void error(const char* code, const std::string& path, std::string message)
            {
                add(code, Severity::Error, path, std::move(message));
            }

            void warning(const char* code, const std::string& path, std::string message)
            {
                add(code, Severity::Warning, path, std::move(message));
            }

            bool boneIsKnown(const std::string& bone) const { return !mProfileKnown || mKnownBones.count(bone) == 1; }

            // ---- identity, targets
            void checkIdentity()
            {
                const Identity& identity = mPackage.mIdentity;
                const ContractVersion& contract = identity.mContractVersion;
                if (contract.mMajor < 1 || contract.mMajor > sContractMajor
                    || (contract.mMajor == sContractMajor && contract.mMinor > sContractMinor))
                {
                    error("STRUCT-001", "/identity/contractVersion",
                        "the package needs contract " + std::to_string(contract.mMajor) + "."
                            + std::to_string(contract.mMinor) + " but this build understands "
                            + std::to_string(sContractMajor) + "." + std::to_string(sContractMinor));
                }
                if (!isValidPackageId(identity.mId))
                {
                    error("STRUCT-003", "/identity/id",
                        "the package id is not valid (1 to 64 characters of a-z, 0-9, '_', '.', '-')");
                }
                if (!isVersionText(identity.mVersion))
                {
                    error("STRUCT-007", "/identity/version",
                        "the package version must be major.minor.patch with digits only");
                }
            }

            void checkTargets()
            {
                for (std::size_t i = 0; i < mPackage.mTargets.size(); ++i)
                {
                    const Target& target = mPackage.mTargets[i];
                    if (!target.mRace && !target.mSex && !target.mCreatureType)
                    {
                        warning("TARGET-001", at("/targets", i), "the target names no race, sex or creature type");
                    }
                }
            }

            // ---- binding
            void checkBinding()
            {
                const Binding& binding = mPackage.mBinding;
                std::optional<opas::anim::SkeletonProfile> profile;
                const std::string skeletonPath = "/binding/skeleton";
                if (binding.mSkeletonProfile && binding.mSkeletonPath)
                {
                    error("BIND-001", skeletonPath, "the binding names both a skeleton path and an inline profile");
                }
                else if (!binding.mSkeletonProfile && !binding.mSkeletonPath)
                {
                    error("BIND-001", skeletonPath, "the binding names no skeleton profile");
                }
                else if (binding.mSkeletonProfile)
                {
                    profile = *binding.mSkeletonProfile;
                    const std::string problem = opas::anim::validateSkeletonProfile(*profile);
                    if (!problem.empty())
                    {
                        error("BIND-001", skeletonPath, "the skeleton profile is invalid: " + problem);
                    }
                }
                else if (!isSafePackagePath(*binding.mSkeletonPath))
                {
                    error("STRUCT-006", skeletonPath, "the skeleton path is not a safe relative path");
                }
                else if (!mOptions.mReadFile)
                {
                    warning("BIND-005", skeletonPath,
                        "the skeleton profile is a file and no file reader was given: not checked");
                }
                else
                {
                    const std::optional<std::string> text = mOptions.mReadFile(*binding.mSkeletonPath);
                    if (!text)
                    {
                        error("BIND-006", skeletonPath,
                            "the skeleton profile file '" + *binding.mSkeletonPath + "' cannot be read");
                    }
                    else
                    {
                        opas::anim::SkeletonProfileLoadResult loaded
                            = opas::anim::loadSkeletonProfile(*text, *binding.mSkeletonPath);
                        if (loaded.value)
                        {
                            profile = std::move(loaded.value);
                        }
                        else
                        {
                            error("BIND-006", skeletonPath,
                                "the skeleton profile file is invalid: " + loaded.error.toString());
                        }
                    }
                }

                std::set<std::string> targets;
                if (profile)
                {
                    mProfileKnown = true;
                    std::set<std::string> sources;
                    for (const opas::anim::BoneMapping& mapping : profile->bones)
                    {
                        targets.insert(mapping.target);
                        sources.insert(mapping.source);
                    }
                    mHumanoidGait = true;
                    for (const std::string_view needed : sGaitSources)
                    {
                        mHumanoidGait = mHumanoidGait && sources.count(std::string(needed)) == 1;
                    }
                    mMappedBones = targets;
                }

                std::set<std::string> unmapped;
                for (std::size_t i = 0; i < binding.mUnmappedBones.size(); ++i)
                {
                    const std::string& bone = binding.mUnmappedBones[i];
                    if (targets.count(bone) == 1)
                    {
                        error("BIND-003", at("/binding/unmappedBones", i),
                            "the unmapped bone '" + bone + "' is also a target of the skeleton profile");
                    }
                    else if (!unmapped.insert(bone).second)
                    {
                        error("BIND-003", at("/binding/unmappedBones", i),
                            "the unmapped bone '" + bone + "' is listed twice");
                    }
                }
                if (mProfileKnown)
                {
                    mMappedBones.insert(unmapped.begin(), unmapped.end());
                    if (mMappedBones.count(binding.mRootBone) == 0)
                    {
                        error("BIND-002", "/binding/rootBone",
                            "the root bone '" + binding.mRootBone
                                + "' is neither mapped by the profile nor listed as unmapped");
                    }
                }
                checkRestSkeleton();
                mKnownBones = mMappedBones;
                if (binding.mRestSkeleton)
                {
                    for (const RestBone& bone : *binding.mRestSkeleton)
                    {
                        mKnownBones.insert(bone.mName);
                    }
                }
            }

            static bool isIdentityRotation(const std::array<double, 4>& rotation)
            {
                return std::abs(rotation[0]) <= sUnitQuatTolerance && std::abs(rotation[1]) <= sUnitQuatTolerance
                    && std::abs(rotation[2]) <= sUnitQuatTolerance
                    && std::abs(std::abs(rotation[3]) - 1.0) <= sUnitQuatTolerance;
            }

            void checkRestSkeleton()
            {
                const Binding& binding = mPackage.mBinding;
                const std::string path = "/binding/restSkeleton";
                if (!binding.mRestSkeleton)
                {
                    warning("BIND-014", path,
                        "the package has no rest skeleton: CreativeMotion's assumptions are not checked");
                    return;
                }
                const std::vector<RestBone>& bones = *binding.mRestSkeleton;
                opas::rig::Skeleton skeleton;
                skeleton.bones.reserve(bones.size());
                for (std::size_t i = 0; i < bones.size(); ++i)
                {
                    const RestBone& bone = bones[i];
                    const double length
                        = std::sqrt(bone.mRotation[0] * bone.mRotation[0] + bone.mRotation[1] * bone.mRotation[1]
                            + bone.mRotation[2] * bone.mRotation[2] + bone.mRotation[3] * bone.mRotation[3]);
                    opas::rig::Bone converted;
                    converted.name = bone.mName;
                    converted.parent = bone.mParent;
                    converted.localRest.translation
                        = opas::math::Vec3{ bone.mTranslation[0], bone.mTranslation[1], bone.mTranslation[2] };
                    // Written so that NaN fails. The rotation is checked here with its own code; the copy given to
                    // validateSkeleton gets the identity so the same mistake is not reported twice.
                    if (!(std::abs(length - 1.0) <= sUnitQuatTolerance))
                    {
                        error("BIND-012", at(at(path, i), "rotation"),
                            "the rest rotation of bone '" + bone.mName + "' is not a unit quaternion (length "
                                + fmt(length) + ")");
                        converted.localRest.rotation = opas::math::Quat{ 0, 0, 0, 1 };
                    }
                    else
                    {
                        converted.localRest.rotation = opas::math::Quat{ bone.mRotation[0], bone.mRotation[1],
                            bone.mRotation[2], bone.mRotation[3] };
                    }
                    skeleton.bones.push_back(std::move(converted));
                }
                const std::string problem = opas::rig::validateSkeleton(skeleton);
                if (!problem.empty())
                {
                    error("BIND-010", path, "the rest skeleton is invalid: " + problem);
                    return;
                }
                // Nothing may rotate above the animation root (CreativeMotion handoff section 4).
                std::optional<std::size_t> root;
                for (std::size_t i = 0; i < bones.size() && !root; ++i)
                {
                    if (bones[i].mName == binding.mRootBone)
                    {
                        root = i;
                    }
                }
                if (!root)
                {
                    return;
                }
                int ancestor = bones[*root].mParent;
                for (std::size_t guard = 0; ancestor >= 0 && guard < bones.size(); ++guard)
                {
                    const RestBone& above = bones[static_cast<std::size_t>(ancestor)];
                    if (!isIdentityRotation(above.mRotation))
                    {
                        error("BIND-013", at(path, static_cast<std::size_t>(ancestor)),
                            "bone '" + above.mName + "' is above the animation root '" + binding.mRootBone
                                + "' and has a rotation: nothing may rotate above the root");
                    }
                    ancestor = above.mParent;
                }
            }

            // ---- meshes
            void checkMeshes()
            {
                const std::vector<MeshEntry>& meshes = mPackage.mMeshes;
                if (meshes.empty())
                {
                    error("STRUCT-002", "/meshes", "the package has no render mesh");
                }
                mSummaries.assign(meshes.size(), nullptr);
                mFetched.assign(meshes.size(), std::nullopt);

                std::set<std::string> ids;
                std::map<std::tuple<std::string, int, int>, std::size_t> slots;
                for (std::size_t i = 0; i < meshes.size(); ++i)
                {
                    const MeshEntry& mesh = meshes[i];
                    const std::string path = at("/meshes", i);
                    if (!isValidPackageId(mesh.mId))
                    {
                        error("STRUCT-003", at(path, "id"),
                            "the mesh id is not valid (1 to 64 characters of a-z, 0-9, '_', '.', '-')");
                    }
                    else if (!ids.insert(mesh.mId).second)
                    {
                        error(
                            "STRUCT-004", at(path, "id"), "the mesh id '" + mesh.mId + "' is used by an earlier mesh");
                    }
                    const bool pathIsSafe = isSafePackagePath(mesh.mPath);
                    if (!pathIsSafe)
                    {
                        error("STRUCT-006", at(path, "path"), "the mesh path is not a safe relative path");
                    }
                    if (mesh.mLod < 0)
                    {
                        error("STRUCT-005", at(path, "lod"), "the level of detail must not be negative");
                    }
                    const std::string& slotName = mesh.mPartType ? *mesh.mPartType : mesh.mId;
                    const auto slot = std::make_tuple(slotName, static_cast<int>(mesh.mRole), mesh.mLod);
                    const auto inserted = slots.emplace(slot, i);
                    if (!inserted.second)
                    {
                        error("STRUCT-005", at(path, "lod"),
                            "mesh " + std::to_string(inserted.first->second) + " already fills part '" + slotName
                                + "' with role '" + std::string(toString(mesh.mRole)) + "' at level of detail "
                                + std::to_string(mesh.mLod));
                    }

                    // The summary: inline first, else the source.
                    if (mesh.mSummary)
                    {
                        mSummaries[i] = &*mesh.mSummary;
                    }
                    else if (pathIsSafe && mOptions.mMeshSource != nullptr)
                    {
                        mFetched[i] = mOptions.mMeshSource->summarize(mesh.mPath, toString(mesh.mFormat));
                        if (mFetched[i])
                        {
                            mSummaries[i] = &*mFetched[i];
                        }
                    }
                    if (mSummaries[i] == nullptr)
                    {
                        warning("MESH-001", path,
                            "no mesh summary is available: skin, geometry, morph and region rules are not checked for "
                            "it");
                        continue;
                    }
                    checkSummary(i, *mSummaries[i]);
                }
                checkBodyExtent();
            }

            void checkSummary(std::size_t meshIndex, const MeshSummary& summary)
            {
                const std::string path = at(at("/meshes", meshIndex), "summary");
                checkBounds(summary, path);
                if (mProfileKnown)
                {
                    for (std::size_t i = 0; i < summary.mBoneNames.size(); ++i)
                    {
                        if (mMappedBones.count(summary.mBoneNames[i]) == 0)
                        {
                            error("BIND-004", at(at(path, "boneNames"), i),
                                "the skin uses bone '" + summary.mBoneNames[i]
                                    + "', which is neither mapped by the profile nor listed as unmapped");
                        }
                    }
                }
                checkSkin(summary, path);
                checkTriangles(summary, path);
            }

            // Returns true when the bounds are usable. Reports at most one problem.
            static bool boundsAreUsable(const MeshSummary& summary)
            {
                for (int axis = 0; axis < 3; ++axis)
                {
                    const double low = summary.mBoundsMin[static_cast<std::size_t>(axis)];
                    const double high = summary.mBoundsMax[static_cast<std::size_t>(axis)];
                    if (!std::isfinite(low) || !std::isfinite(high) || low > high)
                    {
                        return false;
                    }
                }
                return true;
            }

            void checkBounds(const MeshSummary& summary, const std::string& path)
            {
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    if (!std::isfinite(summary.mBoundsMin[axis]))
                    {
                        error("GEOM-001", at(path, "boundsMin"),
                            "the lower bound is not finite on axis " + std::to_string(axis));
                        return;
                    }
                }
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    if (!std::isfinite(summary.mBoundsMax[axis]))
                    {
                        error("GEOM-001", at(path, "boundsMax"),
                            "the upper bound is not finite on axis " + std::to_string(axis));
                        return;
                    }
                }
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    if (summary.mBoundsMin[axis] > summary.mBoundsMax[axis])
                    {
                        error("GEOM-001", at(path, "boundsMin"),
                            "the lower bound is above the upper bound on axis " + std::to_string(axis));
                        return;
                    }
                }
            }

            void checkSkin(const MeshSummary& summary, const std::string& path)
            {
                // Without bones the mesh is not skinned; without offsets no skin data was given.
                if (summary.mBoneNames.empty())
                {
                    return;
                }
                if (summary.mInfluenceOffsets.empty())
                {
                    warning("MESH-002", at(path, "influenceOffsets"),
                        "the skinned mesh has no influence data: its skin weights are not checked");
                    return;
                }
                const std::vector<std::uint32_t>& offsets = summary.mInfluenceOffsets;
                const std::string offsetsPath = at(path, "influenceOffsets");
                bool offsetsOk = offsets.size() == static_cast<std::uint64_t>(summary.mVertexCount) + 1
                    && offsets.front() == 0 && offsets.back() == summary.mInfluences.size();
                for (std::size_t i = 1; offsetsOk && i < offsets.size(); ++i)
                {
                    offsetsOk = offsets[i - 1] <= offsets[i];
                }
                if (!offsetsOk)
                {
                    error("SKIN-006", offsetsPath,
                        "the influence offsets must start at 0, never decrease, end at the number of influences and "
                        "have "
                        "vertexCount + 1 entries");
                    return;
                }
                const std::string influencesPath = at(path, "influences");
                for (std::size_t vertex = 0; vertex < summary.mVertexCount; ++vertex)
                {
                    const std::size_t begin = offsets[vertex];
                    const std::size_t end = offsets[vertex + 1];
                    const std::string vertexPath = at(offsetsPath, vertex);
                    if (begin == end)
                    {
                        error("SKIN-005", vertexPath,
                            "vertex " + std::to_string(vertex) + " of a skinned mesh has no influence");
                        continue;
                    }
                    if (end - begin > static_cast<std::size_t>(Thresholds::sMaxInfluencesPerVertex))
                    {
                        error("SKIN-003", vertexPath,
                            "vertex " + std::to_string(vertex) + " has " + std::to_string(end - begin)
                                + " influences, at most " + std::to_string(Thresholds::sMaxInfluencesPerVertex)
                                + " are allowed");
                    }
                    double sum = 0;
                    bool weightsUsable = true;
                    for (std::size_t i = begin; i < end; ++i)
                    {
                        const MeshSummary::Influence& influence = summary.mInfluences[i];
                        if (!std::isfinite(influence.mWeight) || influence.mWeight < 0)
                        {
                            error("SKIN-001", at(influencesPath, i),
                                "the weight " + fmt(influence.mWeight) + " is negative or not finite");
                            weightsUsable = false;
                            continue;
                        }
                        if (influence.mBone >= summary.mBoneNames.size())
                        {
                            error("SKIN-004", at(influencesPath, i),
                                "bone index " + std::to_string(influence.mBone) + " is outside the "
                                    + std::to_string(summary.mBoneNames.size()) + " bones of the skin");
                        }
                        sum += influence.mWeight;
                    }
                    if (weightsUsable && !(std::abs(sum - 1.0) <= Thresholds::sWeightSumTolerance))
                    {
                        error("SKIN-002", vertexPath,
                            "the weights of vertex " + std::to_string(vertex) + " sum to " + fmt(sum) + ", not 1");
                    }
                }
            }

            void checkTriangles(const MeshSummary& summary, const std::string& path)
            {
                if (summary.mTriangleIndices.empty())
                {
                    return;
                }
                const std::string indicesPath = at(path, "triangleIndices");
                if (summary.mTriangleIndices.size() != static_cast<std::uint64_t>(summary.mTriangleCount) * 3)
                {
                    error("GEOM-004", indicesPath,
                        "there are " + std::to_string(summary.mTriangleIndices.size())
                            + " triangle indices, expected 3 * triangleCount");
                    return;
                }
                for (std::size_t i = 0; i < summary.mTriangleIndices.size(); ++i)
                {
                    if (summary.mTriangleIndices[i] >= summary.mVertexCount)
                    {
                        error("GEOM-003", at(indicesPath, i),
                            "triangle index " + std::to_string(summary.mTriangleIndices[i])
                                + " is not below the vertex count " + std::to_string(summary.mVertexCount));
                    }
                }
            }

            // The proportion range: the union of the LOD 0 skin meshes' bounds.
            void checkBodyExtent()
            {
                std::array<double, 3> low{};
                std::array<double, 3> high{};
                std::optional<std::size_t> first;
                for (std::size_t i = 0; i < mPackage.mMeshes.size(); ++i)
                {
                    const MeshEntry& mesh = mPackage.mMeshes[i];
                    if (mesh.mRole != MeshRole::Skin || mesh.mLod != 0 || mSummaries[i] == nullptr
                        || !boundsAreUsable(*mSummaries[i]))
                    {
                        continue;
                    }
                    for (std::size_t axis = 0; axis < 3; ++axis)
                    {
                        low[axis] = first ? std::min(low[axis], mSummaries[i]->mBoundsMin[axis])
                                          : mSummaries[i]->mBoundsMin[axis];
                        high[axis] = first ? std::max(high[axis], mSummaries[i]->mBoundsMax[axis])
                                           : mSummaries[i]->mBoundsMax[axis];
                    }
                    if (!first)
                    {
                        first = i;
                    }
                }
                if (!first)
                {
                    return;
                }
                double extent = 0;
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    extent = std::max(extent, high[axis] - low[axis]);
                }
                if (!(extent >= Thresholds::sMinBodyExtentM && extent <= Thresholds::sMaxBodyExtentM))
                {
                    error("GEOM-002", at(at("/meshes", *first), "summary"),
                        "the body is " + fmt(extent) + " m across its largest side, outside "
                            + fmt(Thresholds::sMinBodyExtentM) + " to " + fmt(Thresholds::sMaxBodyExtentM) + " m");
                }
            }

            std::optional<std::size_t> findMesh(const std::string& id) const
            {
                for (std::size_t i = 0; i < mPackage.mMeshes.size(); ++i)
                {
                    if (mPackage.mMeshes[i].mId == id)
                    {
                        return i;
                    }
                }
                return std::nullopt;
            }

            // ---- origin and provenance
            struct PartInfo
            {
                const char* mName;
                bool mPresent;
                Origin mOrigin;
                bool mHasContent;
            };

            std::vector<PartInfo> parts() const
            {
                std::vector<PartInfo> info;
                const BodyPackage& p = mPackage;
                info.push_back(
                    { "morphSet", p.mMorphSet.has_value(), p.mMorphSet ? p.mMorphSet->mOrigin : Origin::Authored,
                        p.mMorphSet && !p.mMorphSet->mMorphs.empty() });
                info.push_back({ "proxy", p.mProxy.has_value(), p.mProxy ? p.mProxy->mOrigin : Origin::Authored,
                    p.mProxy && !p.mProxy->mCapsules.empty() });
                info.push_back({ "physicsProfile", p.mPhysicsProfile.has_value(),
                    p.mPhysicsProfile ? p.mPhysicsProfile->mOrigin : Origin::Authored,
                    p.mPhysicsProfile
                        && (!p.mPhysicsProfile->mShapes.empty() || !p.mPhysicsProfile->mMasses.empty()
                            || !p.mPhysicsProfile->mJointLimits.empty() || p.mPhysicsProfile->mRigPath
                            || p.mPhysicsProfile->mProfilePath || p.mPhysicsProfile->mRestrictedTier) });
                info.push_back({ "measurements", p.mMeasurements.has_value(),
                    p.mMeasurements ? p.mMeasurements->mOrigin : Origin::Authored,
                    p.mMeasurements && !p.mMeasurements->mEntries.empty() });
                info.push_back(
                    { "regionMap", p.mRegionMap.has_value(), p.mRegionMap ? p.mRegionMap->mOrigin : Origin::Authored,
                        p.mRegionMap && !p.mRegionMap->mRegions.empty() });
                info.push_back({ "weldGroups", p.mWeldGroups.has_value(),
                    p.mWeldGroups ? p.mWeldGroups->mOrigin : Origin::Authored,
                    p.mWeldGroups && !p.mWeldGroups->mGroups.empty() });
                info.push_back(
                    { "landmarks", p.mLandmarks.has_value(), p.mLandmarks ? p.mLandmarks->mOrigin : Origin::Authored,
                        p.mLandmarks && !p.mLandmarks->mEntries.empty() });
                info.push_back({ "softTissue", p.mSoftTissue.has_value(),
                    p.mSoftTissue ? p.mSoftTissue->mOrigin : Origin::Authored,
                    p.mSoftTissue && (!p.mSoftTissue->mNodes.empty() || p.mSoftTissue->mRestrictedTier) });
                return info;
            }

            void checkOriginsAndProvenance()
            {
                const std::vector<PartInfo> info = parts();
                for (const PartInfo& part : info)
                {
                    if (!part.mPresent)
                    {
                        continue;
                    }
                    const std::string originPath = "/" + std::string(part.mName) + "/origin";
                    if (part.mOrigin == Origin::Missing && part.mHasContent)
                    {
                        error("ORIGIN-001", originPath,
                            std::string("the part '") + part.mName + "' is marked missing but holds content");
                    }
                    if (part.mOrigin == Origin::Derived)
                    {
                        const bool recorded = std::any_of(mPackage.mProvenance.begin(), mPackage.mProvenance.end(),
                            [&part](const ProvenanceRecord& record) { return record.mPart == part.mName; });
                        if (!recorded)
                        {
                            error("PROV-001", originPath,
                                std::string("the part '") + part.mName + "' is derived but has no provenance record");
                        }
                    }
                }
                for (std::size_t i = 0; i < mPackage.mProvenance.size(); ++i)
                {
                    const ProvenanceRecord& record = mPackage.mProvenance[i];
                    const auto found = std::find_if(info.begin(), info.end(),
                        [&record](const PartInfo& part) { return record.mPart == part.mName; });
                    if (found == info.end())
                    {
                        warning("PROV-002", at("/provenance", i),
                            "the provenance record names '" + record.mPart + "', which is not a part of a package");
                    }
                    else if (!found->mPresent || found->mOrigin != Origin::Derived)
                    {
                        warning("PROV-002", at("/provenance", i),
                            "the provenance record names '" + record.mPart
                                + "', which is not a derived part of this package");
                    }
                }
            }

            // ---- weld groups (task 0069)
            void checkWeldGroups()
            {
                if (!mPackage.mWeldGroups)
                {
                    return;
                }
                const WeldGroups& welds = *mPackage.mWeldGroups;
                if (!std::isfinite(welds.mToleranceM) || welds.mToleranceM < 0 || welds.mToleranceM > 1)
                {
                    error("WELD-006", "/weldGroups/tolerance", "the weld tolerance must be a number from 0 to 1 metre");
                }
                const std::optional<std::size_t> mesh = findMesh(welds.mMesh);
                if (!mesh)
                {
                    error("WELD-001", "/weldGroups/mesh",
                        "the weld groups name mesh '" + welds.mMesh + "', which the package does not have");
                }
                const MeshSummary* summary = mesh ? mSummaries[*mesh] : nullptr;
                std::size_t members = 0;
                for (const std::vector<std::uint32_t>& group : welds.mGroups)
                {
                    members += group.size();
                }
                if (welds.mGroups.size() > Limits::sMaxWeldGroups || members > Limits::sMaxWeldMembers)
                {
                    error("WELD-007", "/weldGroups/groups",
                        "the weld groups hold " + std::to_string(welds.mGroups.size()) + " groups and "
                            + std::to_string(members) + " members, at most " + std::to_string(Limits::sMaxWeldGroups)
                            + " and " + std::to_string(Limits::sMaxWeldMembers) + " are checked");
                    return;
                }
                std::set<std::uint32_t> seen;
                for (std::size_t i = 0; i < welds.mGroups.size(); ++i)
                {
                    const std::vector<std::uint32_t>& group = welds.mGroups[i];
                    const std::string groupPath = at("/weldGroups/groups", i);
                    if (group.size() < 2)
                    {
                        error("WELD-002", groupPath,
                            "a weld group needs at least two vertices, this one has " + std::to_string(group.size()));
                        continue;
                    }
                    bool ordered = true;
                    bool outOfRange = false;
                    bool shared = false;
                    for (std::size_t j = 0; j < group.size(); ++j)
                    {
                        if (j > 0 && group[j] <= group[j - 1])
                        {
                            ordered = false;
                            continue; // an unordered member is not looked at again
                        }
                        if (summary != nullptr && group[j] >= summary->mVertexCount)
                        {
                            outOfRange = true;
                        }
                        if (!seen.insert(group[j]).second)
                        {
                            shared = true;
                        }
                    }
                    if (!ordered)
                    {
                        error("WELD-003", groupPath, "the vertices of a weld group must be strictly increasing");
                    }
                    if (outOfRange)
                    {
                        error("WELD-005", groupPath,
                            "a weld group names a vertex at or beyond the mesh's vertex count "
                                + std::to_string(summary->mVertexCount));
                    }
                    if (shared)
                    {
                        error("WELD-004", groupPath, "a vertex of this weld group is also in an earlier group");
                    }
                }
            }

            // ---- region map
            void checkRegionMap()
            {
                if (!mPackage.mRegionMap)
                {
                    return;
                }
                const RegionMap& map = *mPackage.mRegionMap;
                std::set<std::string> ids;
                for (std::size_t i = 0; i < map.mRegions.size(); ++i)
                {
                    const std::string idPath = at(at("/regionMap/regions", i), "id");
                    if (!isValidPackageId(map.mRegions[i].mId))
                    {
                        error("STRUCT-003", idPath,
                            "the region id is not valid (1 to 64 characters of a-z, 0-9, '_', '.', '-')");
                    }
                    else if (!ids.insert(map.mRegions[i].mId).second)
                    {
                        error("STRUCT-004", idPath,
                            "the region id '" + map.mRegions[i].mId + "' is used by an earlier region");
                    }
                }
                const std::optional<std::size_t> mesh = findMesh(map.mMesh);
                if (!mesh)
                {
                    error("GEOM-008", "/regionMap/mesh",
                        "the region map names mesh '" + map.mMesh + "', which the package does not have");
                    return;
                }
                const MeshSummary* summary = mSummaries[*mesh];
                if (summary == nullptr)
                {
                    return;
                }
                std::size_t totalRanges = 0;
                for (const Region& region : map.mRegions)
                {
                    totalRanges += region.mRanges.size();
                }
                if (totalRanges > Limits::sMaxRegionRanges)
                {
                    error("GEOM-007", "/regionMap/regions",
                        "the region map has " + std::to_string(totalRanges) + " ranges, at most "
                            + std::to_string(Limits::sMaxRegionRanges) + " are checked");
                    return;
                }
                const std::uint64_t triangles = summary->mTriangleCount;
                struct Interval
                {
                    std::uint64_t mBegin;
                    std::uint64_t mEnd;
                    std::size_t mRegion;
                    std::size_t mRange;
                };
                std::vector<Interval> intervals;
                for (std::size_t r = 0; r < map.mRegions.size(); ++r)
                {
                    for (std::size_t m = 0; m < map.mRegions[r].mRanges.size(); ++m)
                    {
                        const TriangleRange& range = map.mRegions[r].mRanges[m];
                        const std::string path = at(at(at("/regionMap/regions", r), "ranges"), m);
                        if (range.mBegin > range.mEnd || range.mEnd > triangles)
                        {
                            error("GEOM-007", path,
                                "the range [" + std::to_string(range.mBegin) + ", " + std::to_string(range.mEnd)
                                    + ") is reversed or reaches past the " + std::to_string(triangles)
                                    + " triangles of the mesh");
                        }
                        if (range.mBegin < range.mEnd && range.mBegin < triangles)
                        {
                            intervals.push_back({ range.mBegin, std::min<std::uint64_t>(range.mEnd, triangles), r, m });
                        }
                    }
                }
                std::sort(intervals.begin(), intervals.end(), [](const Interval& left, const Interval& right) {
                    return std::tie(left.mBegin, left.mRegion, left.mRange)
                        < std::tie(right.mBegin, right.mRegion, right.mRange);
                });
                std::uint64_t frontier = 0;
                std::optional<std::uint64_t> firstGap;
                for (const Interval& interval : intervals)
                {
                    if (interval.mBegin < frontier)
                    {
                        error("GEOM-006", at(at(at("/regionMap/regions", interval.mRegion), "ranges"), interval.mRange),
                            "triangle " + std::to_string(interval.mBegin) + " is already in another range");
                    }
                    else if (interval.mBegin > frontier && !firstGap)
                    {
                        firstGap = frontier;
                    }
                    frontier = std::max(frontier, interval.mEnd);
                }
                if (!firstGap && frontier < triangles)
                {
                    firstGap = frontier;
                }
                if (firstGap)
                {
                    error("GEOM-005", "/regionMap/regions",
                        "triangle " + std::to_string(*firstGap) + " of mesh '" + map.mMesh + "' is in no region");
                }
            }

            // ---- morphs
            void checkMorphs()
            {
                if (!mPackage.mMorphSet)
                {
                    return;
                }
                std::set<std::string> ids;
                const std::vector<Morph>& morphs = mPackage.mMorphSet->mMorphs;
                for (std::size_t m = 0; m < morphs.size(); ++m)
                {
                    const Morph& morph = morphs[m];
                    const std::string path = at("/morphSet/morphs", m);
                    if (!isValidPackageId(morph.mId))
                    {
                        error("STRUCT-003", at(path, "id"),
                            "the morph id is not valid (1 to 64 characters of a-z, 0-9, '_', '.', '-')");
                    }
                    else if (!ids.insert(morph.mId).second)
                    {
                        error("MORPH-004", at(path, "id"),
                            "the morph id '" + morph.mId + "' is used by an earlier morph");
                    }
                    if (!(morph.mRangeMin <= morph.mRangeMax && morph.mDefault >= morph.mRangeMin
                            && morph.mDefault <= morph.mRangeMax))
                    {
                        error("MORPH-003", at(path, "range"),
                            "the slider range [" + fmt(morph.mRangeMin) + ", " + fmt(morph.mRangeMax)
                                + "] must be ordered and contain the default " + fmt(morph.mDefault));
                    }
                    for (std::size_t t = 0; t < morph.mTargets.size(); ++t)
                    {
                        checkMorphTarget(morph.mTargets[t], at(at(path, "targets"), t));
                    }
                }
            }

            void checkMorphTarget(const MorphTarget& target, const std::string& path)
            {
                for (std::size_t i = 0; i < target.mDeltas.size(); ++i)
                {
                    const std::array<double, 3>& delta = target.mDeltas[i];
                    if (!std::isfinite(delta[0]) || !std::isfinite(delta[1]) || !std::isfinite(delta[2]))
                    {
                        error("MORPH-002", at(at(path, "deltas"), i), "the delta is not finite");
                    }
                }
                if (target.mVertices.size() != target.mDeltas.size())
                {
                    error("MORPH-006", at(path, "vertices"),
                        std::to_string(target.mVertices.size()) + " vertices but "
                            + std::to_string(target.mDeltas.size()) + " deltas");
                }
                else
                {
                    for (std::size_t i = 1; i < target.mVertices.size(); ++i)
                    {
                        if (target.mVertices[i - 1] >= target.mVertices[i])
                        {
                            error("MORPH-006", at(path, "vertices"),
                                "the vertex indices must be strictly increasing (entry " + std::to_string(i) + ")");
                            break;
                        }
                    }
                }
                const std::optional<std::size_t> mesh = findMesh(target.mMesh);
                if (!mesh)
                {
                    error("MORPH-005", at(path, "mesh"),
                        "the morph target names mesh '" + target.mMesh + "', which the package does not have");
                    return;
                }
                const MeshSummary* summary = mSummaries[*mesh];
                if (summary == nullptr)
                {
                    return;
                }
                for (std::size_t i = 0; i < target.mVertices.size(); ++i)
                {
                    if (target.mVertices[i] >= summary->mVertexCount)
                    {
                        error("MORPH-001", at(at(path, "vertices"), i),
                            "vertex " + std::to_string(target.mVertices[i]) + " is not below the mesh's vertex count "
                                + std::to_string(summary->mVertexCount));
                    }
                }
            }

            // ---- proxy, physics
            void checkProxy()
            {
                if (!mPackage.mProxy)
                {
                    return;
                }
                const std::vector<ProxyCapsule>& capsules = mPackage.mProxy->mCapsules;
                for (std::size_t i = 0; i < capsules.size(); ++i)
                {
                    const std::string path = at("/proxy/capsules", i);
                    if (!isFinitePositive(capsules[i].mRadius))
                    {
                        error("PROXY-001", at(path, "radius"),
                            "the capsule radius " + fmt(capsules[i].mRadius) + " must be finite and above 0");
                    }
                    if (!boneIsKnown(capsules[i].mBone))
                    {
                        error("PROXY-002", at(path, "bone"),
                            "the capsule is on bone '" + capsules[i].mBone + "', which the package does not have");
                    }
                }
            }

            void checkExternalFile(const std::optional<std::string>& file, const std::string& pointer, bool isRig)
            {
                if (!file)
                {
                    return;
                }
                if (!isSafePackagePath(*file))
                {
                    error("STRUCT-006", pointer, "the path is not a safe relative path");
                    return;
                }
                if (!mOptions.mReadFile)
                {
                    warning("PHYS-010", pointer,
                        "the file '" + *file + "' is named but no file reader was given: not checked");
                    return;
                }
                const std::optional<std::string> text = mOptions.mReadFile(*file);
                if (!text)
                {
                    error("PHYS-005", pointer, "the file '" + *file + "' cannot be read");
                    return;
                }
                if (isRig)
                {
                    const opas::rig::RigLoadResult loaded = opas::rig::loadRig(*text, *file);
                    if (!loaded.ok())
                    {
                        error("PHYS-005", pointer, "the rig file is invalid: " + loaded.error.toString());
                    }
                }
                else
                {
                    const opas::rig::ProfileLoadResult loaded = opas::rig::loadProfile(*text, *file);
                    if (!loaded.ok())
                    {
                        error("PHYS-005", pointer, "the profile file is invalid: " + loaded.error.toString());
                    }
                }
            }

            void checkPhysics()
            {
                if (!mPackage.mPhysicsProfile)
                {
                    return;
                }
                const PhysicsProfile& physics = *mPackage.mPhysicsProfile;
                checkExternalFile(physics.mRigPath, "/physicsProfile/rig", true);
                checkExternalFile(physics.mProfilePath, "/physicsProfile/profile", false);
                for (std::size_t i = 0; i < physics.mShapes.size(); ++i)
                {
                    const PhysicsShape& shape = physics.mShapes[i];
                    const std::string path = at("/physicsProfile/shapes", i);
                    const std::size_t used
                        = shape.mKind == ShapeKind::Sphere ? 1 : (shape.mKind == ShapeKind::Capsule ? 2 : 3);
                    for (std::size_t axis = 0; axis < used; ++axis)
                    {
                        if (!isFinitePositive(shape.mSize[axis]))
                        {
                            error("PHYS-001", at(path, "size"),
                                "size " + std::to_string(axis) + " of the " + std::string(toString(shape.mKind))
                                    + " is " + fmt(shape.mSize[axis]) + ", it must be finite and above 0");
                            break;
                        }
                    }
                    checkBone("PHYS-004", at(path, "bone"), shape.mBone);
                }
                for (std::size_t i = 0; i < physics.mMasses.size(); ++i)
                {
                    const PhysicsMass& mass = physics.mMasses[i];
                    const std::string path = at("/physicsProfile/masses", i);
                    if (!isFinitePositive(mass.mKilograms))
                    {
                        error("PHYS-002", at(path, "kilograms"),
                            "the mass " + fmt(mass.mKilograms) + " kg must be finite and above 0");
                    }
                    checkBone("PHYS-004", at(path, "bone"), mass.mBone);
                }
                for (std::size_t i = 0; i < physics.mJointLimits.size(); ++i)
                {
                    const JointLimit& limit = physics.mJointLimits[i];
                    const std::string path = at("/physicsProfile/jointLimits", i);
                    if (!(limit.mMin <= limit.mMax && limit.mRest >= limit.mMin && limit.mRest <= limit.mMax))
                    {
                        error("PHYS-003", path,
                            "the joint limits [" + fmt(limit.mMin) + ", " + fmt(limit.mMax)
                                + "] must be ordered and contain the rest value " + fmt(limit.mRest));
                    }
                    checkBone("PHYS-004", at(path, "bone"), limit.mBone);
                }
                if (physics.mRestrictedTier && mPackage.mIdentity.mAgeClass != AgeClass::Adult)
                {
                    error("AGE-001", "/physicsProfile/restrictedTier",
                        "a restricted tier needs ageClass \"adult\" (unspecified never counts as adult)");
                }
            }

            void checkBone(const char* code, const std::string& path, const std::string& bone)
            {
                if (!boneIsKnown(bone))
                {
                    error(code, path, "bone '" + bone + "' is not a bone of the package");
                }
            }

            // ---- measurements, landmarks, soft tissue
            void checkMeasurements()
            {
                if (!mPackage.mMeasurements)
                {
                    return;
                }
                std::set<std::string> names;
                const std::vector<Measurement>& entries = mPackage.mMeasurements->mEntries;
                for (std::size_t i = 0; i < entries.size(); ++i)
                {
                    const std::string path = at("/measurements/entries", i);
                    if (!isFiniteNonNegative(entries[i].mValue))
                    {
                        error("MEAS-001", at(path, "value"),
                            "the measurement '" + entries[i].mName + "' is " + fmt(entries[i].mValue)
                                + ", it must be finite and not negative");
                    }
                    if (!names.insert(entries[i].mName).second)
                    {
                        error("MEAS-001", at(path, "name"),
                            "the measurement name '" + entries[i].mName + "' is used twice");
                    }
                }
            }

            void checkLandmarks()
            {
                if (!mPackage.mLandmarks)
                {
                    return;
                }
                std::set<std::string> names;
                const std::vector<Landmark>& entries = mPackage.mLandmarks->mEntries;
                for (std::size_t i = 0; i < entries.size(); ++i)
                {
                    const std::string path = at("/landmarks/entries", i);
                    if (!boneIsKnown(entries[i].mBone))
                    {
                        error("LAND-001", path,
                            "the landmark '" + entries[i].mName + "' is on bone '" + entries[i].mBone
                                + "', which the package does not have");
                    }
                    if (!names.insert(entries[i].mName).second)
                    {
                        error("LAND-001", path, "the landmark name '" + entries[i].mName + "' is used twice");
                    }
                }
            }

            void checkSoftTissue()
            {
                if (!mPackage.mSoftTissue)
                {
                    return;
                }
                const SoftTissue& soft = *mPackage.mSoftTissue;
                std::set<std::string> ids;
                for (std::size_t i = 0; i < soft.mNodes.size(); ++i)
                {
                    const SoftTissueNode& node = soft.mNodes[i];
                    const std::string path = at("/softTissue/nodes", i);
                    if (!isValidPackageId(node.mId))
                    {
                        error("STRUCT-003", at(path, "id"),
                            "the soft tissue node id is not valid (1 to 64 characters of a-z, 0-9, '_', '.', '-')");
                    }
                    else if (!ids.insert(node.mId).second)
                    {
                        error("STRUCT-004", at(path, "id"),
                            "the soft tissue node id '" + node.mId + "' is used by an earlier node");
                    }
                    if (!isFinitePositive(node.mMass))
                    {
                        error("SOFT-001", path,
                            "the mass of node '" + node.mId + "' is " + fmt(node.mMass)
                                + ", it must be finite and above 0");
                    }
                    if (!isFiniteNonNegative(node.mStiffness))
                    {
                        error("SOFT-001", path,
                            "the stiffness of node '" + node.mId + "' is " + fmt(node.mStiffness)
                                + ", it must be finite and not negative");
                    }
                    if (!isFiniteNonNegative(node.mDamping))
                    {
                        error("SOFT-001", path,
                            "the damping of node '" + node.mId + "' is " + fmt(node.mDamping)
                                + ", it must be finite and not negative");
                    }
                    if (!boneIsKnown(node.mBone))
                    {
                        error("SOFT-001", path,
                            "node '" + node.mId + "' is on bone '" + node.mBone + "', which the package does not have");
                    }
                }
                if (soft.mRestrictedTier && mPackage.mIdentity.mAgeClass != AgeClass::Adult)
                {
                    error("AGE-001", "/softTissue/restrictedTier",
                        "a restricted tier needs ageClass \"adult\" (unspecified never counts as adult)");
                }
            }
        };
    }

    bool Report::ok() const
    {
        return errorCount() == 0;
    }

    int Report::errorCount() const
    {
        return static_cast<int>(std::count_if(
            mFindings.begin(), mFindings.end(), [](const Finding& f) { return f.mSeverity == Severity::Error; }));
    }

    int Report::warningCount() const
    {
        return static_cast<int>(mFindings.size()) - errorCount();
    }

    Report validateBodyPackage(const BodyPackage& package, const ValidateOptions& options)
    {
        try
        {
            return Validator(package, options).run();
        }
        catch (const std::exception& e)
        {
            // Not expected (the rules only read the package); a report with one error is better than a crash.
            Report report;
            report.mFindings.push_back(Finding{ "BODY-INTERNAL-001", Severity::Error, "",
                cleanMessage(std::string("the validator failed: ") + e.what()) });
            return report;
        }
    }

    namespace
    {
        std::string_view severityWord(Severity severity)
        {
            return severity == Severity::Error ? "error" : "warning";
        }
    }

    std::string formatReportText(const Report& report)
    {
        std::string text;
        for (const Finding& finding : report.mFindings)
        {
            text += std::string(severityWord(finding.mSeverity)) + " " + finding.mCode + " " + finding.mPath + ": "
                + finding.mMessage + "\n";
        }
        text += std::to_string(report.errorCount()) + " error(s), " + std::to_string(report.warningCount())
            + " warning(s), humanoidGait=" + (report.mHumanoidGait ? "true" : "false") + "\n";
        return text;
    }

    std::string formatReportJson(const Report& report)
    {
        using opas::format::detail::jsonString;
        const std::string tail = "\"humanoidGait\": " + std::string(report.mHumanoidGait ? "true" : "false")
            + ", \"errors\": " + std::to_string(report.errorCount())
            + ", \"warnings\": " + std::to_string(report.warningCount());
        if (report.mFindings.empty())
        {
            return "{\"findings\": [], " + tail + "}\n";
        }
        std::string text = "{\n  \"findings\": [\n";
        for (std::size_t i = 0; i < report.mFindings.size(); ++i)
        {
            const Finding& finding = report.mFindings[i];
            text += "    {\"code\": " + jsonString(finding.mCode)
                + ", \"severity\": " + jsonString(severityWord(finding.mSeverity))
                + ", \"path\": " + jsonString(finding.mPath) + ", \"message\": " + jsonString(finding.mMessage) + "}";
            text += i + 1 < report.mFindings.size() ? ",\n" : "\n";
        }
        text += "  ],\n  \"humanoidGait\": " + std::string(report.mHumanoidGait ? "true" : "false")
            + ",\n  \"errors\": " + std::to_string(report.errorCount())
            + ",\n  \"warnings\": " + std::to_string(report.warningCount()) + "\n}\n";
        return text;
    }
}
