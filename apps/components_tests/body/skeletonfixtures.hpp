#ifndef OPENEDEN_TESTS_BODY_SKELETONFIXTURES_H
#define OPENEDEN_TESTS_BODY_SKELETONFIXTURES_H

// Synthetic skeletons for the tests of the skeleton mapping (task 0070, stages b to d). No game or mod data: every
// bone is built here. Names follow the Morrowind-style "Bip01 ..." convention of the profile humanoid-to-eden, and the
// extras are invented names of the kinds research note 0033 lists.

#include <components/body/skeletonmap.hpp>
#include <components/body/skeletonmapping.hpp>
#include <components/body/skeletonsuggest.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace SkeletonFixtures
{
    using namespace Body;

    inline RestBone makeBone(const std::string& name, int parent, double x, double y, double z)
    {
        RestBone bone;
        bone.mName = name;
        bone.mParent = parent;
        bone.mTranslation = { x, y, z };
        return bone;
    }

    /// The pack's role table: the 17 core roles with Morrowind-style native names.
    inline std::vector<RoleBinding> nativeRoles()
    {
        return {
            { "Pelvis", "Bip01 Pelvis", {} },
            { "Spine", "Bip01 Spine", {} },
            { "Chest", "Bip01 Spine1", {} },
            { "Neck", "Bip01 Neck", {} },
            { "Head", "Bip01 Head", {} },
            { "LeftUpperArm", "Bip01 L UpperArm", {} },
            { "LeftForearm", "Bip01 L Forearm", {} },
            { "LeftHand", "Bip01 L Hand", {} },
            { "RightUpperArm", "Bip01 R UpperArm", {} },
            { "RightForearm", "Bip01 R Forearm", {} },
            { "RightHand", "Bip01 R Hand", {} },
            { "LeftThigh", "Bip01 L Thigh", {} },
            { "LeftShin", "Bip01 L Calf", {} },
            { "LeftFoot", "Bip01 L Foot", {} },
            { "RightThigh", "Bip01 R Thigh", {} },
            { "RightShin", "Bip01 R Calf", {} },
            { "RightFoot", "Bip01 R Foot", {} },
        };
    }

    /// The 17 role bones; a person about 1.8 units tall (metres), Y up, arms along X.
    inline std::vector<RestBone> baseSkeleton()
    {
        return {
            makeBone("Bip01 Pelvis", -1, 0, 1.0, 0), // 0
            makeBone("Bip01 Spine", 0, 0, 0.10, 0), // 1
            makeBone("Bip01 Spine1", 1, 0, 0.15, 0), // 2 (Chest)
            makeBone("Bip01 Neck", 2, 0, 0.20, 0), // 3
            makeBone("Bip01 Head", 3, 0, 0.10, 0), // 4
            makeBone("Bip01 L UpperArm", 2, 0.15, 0.10, 0), // 5
            makeBone("Bip01 L Forearm", 5, 0.28, 0, 0), // 6
            makeBone("Bip01 L Hand", 6, 0.25, 0, 0), // 7
            makeBone("Bip01 R UpperArm", 2, -0.15, 0.10, 0), // 8
            makeBone("Bip01 R Forearm", 8, -0.28, 0, 0), // 9
            makeBone("Bip01 R Hand", 9, -0.25, 0, 0), // 10
            makeBone("Bip01 L Thigh", 0, 0.08, -0.05, 0), // 11
            makeBone("Bip01 L Calf", 11, 0, -0.42, 0), // 12
            makeBone("Bip01 L Foot", 12, 0, -0.40, 0), // 13
            makeBone("Bip01 R Thigh", 0, -0.08, -0.05, 0), // 14
            makeBone("Bip01 R Calf", 14, 0, -0.42, 0), // 15
            makeBone("Bip01 R Foot", 15, 0, -0.40, 0), // 16
        };
    }

    inline int indexOf(const std::vector<RestBone>& skeleton, const std::string& name)
    {
        for (std::size_t i = 0; i < skeleton.size(); ++i)
        {
            if (skeleton[i].mName == name)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    /// Adds one bone under the named parent and returns the new skeleton.
    inline std::vector<RestBone> withBone(std::vector<RestBone> skeleton, const std::string& name,
        const std::string& parent, double x = 0.05, double y = 0.05, double z = 0.05)
    {
        skeleton.push_back(makeBone(name, indexOf(skeleton, parent), x, y, z));
        return skeleton;
    }

    /// The base skeleton plus one extra bone under `parent` (default the chest).
    inline std::vector<RestBone> baseWith(
        const std::string& name, const std::string& parent = "Bip01 Spine1", double x = 0.05, double y = 0.05)
    {
        return withBone(baseSkeleton(), name, parent, x, y, 0.05);
    }

    /// The base skeleton and seven extras of every category (the "extended skeleton" of the tests).
    inline std::vector<RestBone> extendedSkeleton()
    {
        std::vector<RestBone> s = baseSkeleton();
        s = withBone(s, "Breast_L_01", "Bip01 Spine1", 0.06, 0.05, 0.08);
        s = withBone(s, "Breast_R_01", "Bip01 Spine1", -0.06, 0.05, 0.08);
        s = withBone(s, "L Forearm Twist", "Bip01 L Forearm", 0.10, 0, 0);
        s = withBone(s, "WeaponBack", "Bip01 Spine1", 0, 0.0, -0.10);
        s = withBone(s, "IK_Helper_01", "Bip01 Pelvis", 0, 0, 0.3);
        s = withBone(s, "Jaw", "Bip01 Head", 0, -0.04, 0.05);
        s = withBone(s, "Cape_01", "Bip01 Spine1", 0, 0.10, -0.08);
        return s;
    }

    inline MapExtra extra(const std::string& bone, MapAction action)
    {
        MapExtra e;
        e.mBone = bone;
        e.mAction = action;
        e.mAccepted = true;
        return e;
    }

    /// A valid, accepted map for extendedSkeleton() with the native role table.
    inline SkeletonMap extendedMap()
    {
        const std::vector<RestBone> skeleton = extendedSkeleton();
        SkeletonMap map;
        map.mName = "test-extended";
        map.mSource.mBoneCount = static_cast<std::uint32_t>(skeleton.size());
        map.mSource.mDigest = skeletonDigest(skeleton);
        map.mProfile = "humanoid-to-eden";
        for (const RoleBinding& role : nativeRoles())
        {
            map.mRoles.push_back({ role.mRole, role.mBone, {} });
        }
        for (const char* side : { "L", "R" })
        {
            MapExtra breast = extra(std::string("Breast_") + side + "_01", MapAction::Secondary);
            breast.mParent = "Chest";
            breast.mKind = SecondaryKind::Breast;
            map.mExtra.push_back(breast);
        }
        MapExtra twist = extra("L Forearm Twist", MapAction::Fold);
        twist.mRule = FoldRule::NearestMappedAncestor;
        twist.mSuggested = MapSuggestion{ "name.twist", MapConfidence::High };
        map.mExtra.push_back(twist);
        MapExtra weapon = extra("WeaponBack", MapAction::Attach);
        weapon.mParent = "Chest";
        weapon.mAttachPoint = "weapon.back";
        map.mExtra.push_back(weapon);
        MapExtra helper = extra("IK_Helper_01", MapAction::Drop);
        helper.mReason = "no weights, no children";
        map.mExtra.push_back(helper);
        MapExtra jaw = extra("Jaw", MapAction::Fold);
        jaw.mRule = FoldRule::Named;
        jaw.mTo = "Head";
        map.mExtra.push_back(jaw);
        MapExtra cape = extra("Cape_01", MapAction::Secondary);
        cape.mParent = "Chest";
        cape.mKind = SecondaryKind::Cloth;
        cape.mUseClipTrack = true;
        map.mExtra.push_back(cape);
        return map;
    }

    inline const MapExtra* findExtra(const SkeletonMap& map, const std::string& bone)
    {
        for (const MapExtra& e : map.mExtra)
        {
            if (e.mBone == bone)
            {
                return &e;
            }
        }
        return nullptr;
    }

    inline const MapRole* findRoleEntry(const SkeletonMap& map, const std::string& role)
    {
        for (const MapRole& r : map.mRoles)
        {
            if (r.mRole == role)
            {
                return &r;
            }
        }
        return nullptr;
    }

    /// A deterministic pseudo random generator (a fixed LCG): the tests must not depend on the library's engine.
    class Lcg
    {
    public:
        explicit Lcg(std::uint64_t seed)
            : mState(seed * 6364136223846793005ull + 1442695040888963407ull)
        {
        }
        std::uint32_t next()
        {
            mState = mState * 6364136223846793005ull + 1442695040888963407ull;
            return static_cast<std::uint32_t>(mState >> 33);
        }
        std::uint32_t below(std::uint32_t n) { return n == 0 ? 0 : next() % n; }
        double unit() { return (next() & 0xFFFFFF) / static_cast<double>(0x1000000); }

    private:
        std::uint64_t mState;
    };

    /// A random order of the skeleton's bones in which every parent still comes before its children.
    inline std::vector<RestBone> shuffledTopologically(const std::vector<RestBone>& skeleton, Lcg& random)
    {
        const std::size_t n = skeleton.size();
        std::vector<int> remaining(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            remaining[i] = static_cast<int>(i);
        }
        std::vector<int> order;
        std::vector<bool> placed(n, false);
        while (order.size() < n)
        {
            std::vector<int> ready;
            for (const int i : remaining)
            {
                if (!placed[static_cast<std::size_t>(i)]
                    && (skeleton[static_cast<std::size_t>(i)].mParent < 0
                        || placed[static_cast<std::size_t>(skeleton[static_cast<std::size_t>(i)].mParent)]))
                {
                    ready.push_back(i);
                }
            }
            const int pick = ready[random.below(static_cast<std::uint32_t>(ready.size()))];
            placed[static_cast<std::size_t>(pick)] = true;
            order.push_back(pick);
        }
        std::vector<int> newIndex(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            newIndex[static_cast<std::size_t>(order[i])] = static_cast<int>(i);
        }
        std::vector<RestBone> result;
        for (const int old : order)
        {
            RestBone bone = skeleton[static_cast<std::size_t>(old)];
            bone.mParent = bone.mParent < 0 ? -1 : newIndex[static_cast<std::size_t>(bone.mParent)];
            result.push_back(bone);
        }
        return result;
    }
}

#endif
