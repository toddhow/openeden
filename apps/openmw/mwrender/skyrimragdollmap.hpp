#ifndef OPENMW_MWRENDER_SKYRIMRAGDOLLMAP_HPP
#define OPENMW_MWRENDER_SKYRIMRAGDOLLMAP_HPP

// Task 0174 (Skyrim combat S5-5, death ragdolls; decision 0018): which bone of a Skyrim skeleton each body of
// CreativeMotion's humanoid ragdoll rig drives. The answer is a BAKED skeleton map (task 0070, `openeden.skeletonmap`
// v1): read from the user's own skeleton NIF by the suggestion engine (`Body::suggestSkeletonMap`), written for
// inspection (and for a person to correct) as `<folder>/<skeleton digest>.skeletonmap.json`, and read back on the next
// run when its digest still matches the skeleton. Only the map's ROLES are used here: they are accepted by
// construction; the map's extra entries stay suggestions (nothing is accepted silently, nothing consumes them).
//
// Pure apart from the file: no VSG, no physics. Never throws (a failure is a null table and a message).
// Ownership: the table owns its strings and the RagdollBoneMapping span points into them, so a table is neither
// copied nor moved; share it by shared_ptr. Threads: any (the table is immutable once built).

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <components/body/bodypackage.hpp>
#include <components/body/skeletonmap.hpp>
#include <components/nifscene/skeleton.hpp>

#include "actorragdolldriver.hpp"

namespace MWRender::SkyrimRagdoll
{
    /// The rig bones with an engine bone below which the ragdoll is not worth driving (the Morrowind driver's rule).
    constexpr std::size_t kMinResolvedBones = 10;

    class BoneTable
    {
    public:
        BoneTable() = default;
        BoneTable(const BoneTable&) = delete;
        BoneTable& operator=(const BoneTable&) = delete;

        /// One entry per rig bone, in ragdollBoneMappings() order; mEngineBone is "" where the skeleton has none.
        [[nodiscard]] RagdollBoneSet set() const { return RagdollBoneSet{ mMappings, mRootBone.c_str(), true }; }
        [[nodiscard]] std::size_t resolved() const { return mResolved; }
        [[nodiscard]] const std::string& digest() const { return mDigest; }
        [[nodiscard]] const std::vector<std::string>& missingRoles() const { return mMissing; }
        /// "baked" (made now by the suggestion engine) or "file" (read back).
        [[nodiscard]] const std::string& origin() const { return mOrigin; }
        /// The engine bone name (lower case) of rig bone i, or "".
        [[nodiscard]] const std::string& engineBone(std::size_t i) const { return mEngineBones[i]; }

        /// Builds the table from a map's roles and the skeleton it was made for. False (with message) when the
        /// pelvis has no bone or fewer than kMinResolvedBones rig bones resolve.
        bool build(const Body::SkeletonMap& map, const NifScene::Skeleton::Plan& plan, std::string& message);
        void setOrigin(std::string digest, std::string origin)
        {
            mDigest = std::move(digest);
            mOrigin = std::move(origin);
        }

    private:
        std::vector<std::string> mEngineBones;
        std::vector<RagdollBoneMapping> mMappings;
        std::string mRootBone;
        std::string mDigest;
        std::string mOrigin;
        std::vector<std::string> mMissing;
        std::size_t mResolved = 0;
    };

    /// The skeleton plan as the map's rest bones (parents first, as the plan is).
    [[nodiscard]] std::vector<Body::RestBone> restBonesOf(const NifScene::Skeleton::Plan& plan);

    /// The map the suggestion engine makes from the skeleton alone (no skin weights, no role table).
    [[nodiscard]] Body::SkeletonMap suggestMap(const NifScene::Skeleton::Plan& plan);

    /// Reads `<folder>/<digest>.skeletonmap.json` when it is there and its digest and profile match, else bakes the
    /// map and writes it (an empty folder: bakes only; a folder that cannot be written is a warning, not a failure).
    /// Null with `message` when no usable table comes out.
    [[nodiscard]] std::shared_ptr<const BoneTable> makeBoneTable(
        const NifScene::Skeleton::Plan& plan, const std::filesystem::path& folder, std::string& message);

    /// `<user data>/openeden/skeletonmaps`.
    [[nodiscard]] std::filesystem::path mapFolder(const std::filesystem::path& userData);

    /// The folder the game bakes into (set once by World from its user data folder; empty before: bake without a file).
    void setConfiguredMapFolder(const std::filesystem::path& folder);
    [[nodiscard]] std::filesystem::path configuredMapFolder();
}

#endif
