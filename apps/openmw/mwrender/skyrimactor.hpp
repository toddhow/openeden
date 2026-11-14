#ifndef OPENMW_MWRENDER_SKYRIMACTOR_HPP
#define OPENMW_MWRENDER_SKYRIMACTOR_HPP

// Task 0121 (neutral world stage N5-2, decision 0017): the render object of a neutral actor. A game pack describes an
// actor (openeden::sdk::game::ActorAppearance: skeleton model, skinned models to wear); this file reads those files
// as Nif::Scene, builds the skeleton as a hierarchy of named Anim::Transform nodes in its rest pose and each skinned
// shape as a skinned draw (SkyrimModel::buildSkinnedNode), and assembles ONE scene graph node in the shape the engine's
// animation system already links: bones and skinned geometry under one root with an Anim::Meta, exactly what a
// Morrowind creature NIF gives MWAnim::create. Linking (Anim::Skin::link against Anim::Bones) is done by that create.
// Nothing here animates: the skeleton stays in the pose the file stores.
//
// Failure policy: a skeleton that cannot be read gives no node (the caller draws nothing and logs); a part that cannot
// be read, or has no skinned shape, is skipped and counted; a skin whose bone the skeleton lacks is left to
// Anim::Skin::link, which logs it and leaves the bone at its default.
//
// Threads: main thread (skyrimmodel.hpp builds descriptor sets and bone buffers that are the actor's own).

#include <cstdint>
#include <string>

#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Node.h>

#include <components/nifscene/skeleton.hpp>
#include <components/vfs/manager.hpp>

#include <openeden/game/actor_appearance.hpp>

#include "skyrimlip.hpp"
#include "skyrimmodel.hpp"

namespace MWRender::SkyrimActor
{
    /// What building one actor did; logged by the caller and summed into the totals.
    struct Report
    {
        std::uint32_t mPartsAsked = 0;
        std::uint32_t mPartsDrawn = 0; // parts with at least one skinned draw
        std::uint32_t mPartsUnreadable = 0; // the file is missing or is not a Skyrim scene
        std::uint32_t mPartsWithoutSkin = 0; // read, but no skinned shape (a static head part, for example)
        std::uint32_t mBones = 0; // nodes of the skeleton
        std::uint32_t mDraws = 0;
        std::uint32_t mTriangles = 0;
        std::uint32_t mSkeletonProblems = 0; // unreachable, non-finite and duplicate-name nodes of the skeleton plan
        std::string mFailure; // set when no node was made
        /// Task 0146: with `[Game] neutral lip sync`, the morph of the head's mouth (null when off, or when no face
        /// shape or no race head file matched). The caller keeps it alive with the actor.
        vsg::ref_ptr<SkyrimLip::HeadMorph> mLip;
    };

    /// The hierarchy of named Anim::Transform nodes for a skeleton plan, in the rest pose. Roots become children of the
    /// returned group. Pure scene graph work (no GPU object): testable without a device.
    [[nodiscard]] vsg::ref_ptr<vsg::Group> buildSkeletonNodes(const NifScene::Skeleton::Plan& plan);

    /// The whole actor: skeleton plus every wearable part that has skinned shapes, under one group with Anim::Meta
    /// (Skins | Controllers | DynamicBounds). Null with report.mFailure set when the skeleton cannot be used.
    [[nodiscard]] vsg::ref_ptr<vsg::Node> buildActorNode(const openeden::sdk::game::ActorAppearance& appearance,
        const VFS::Manager& vfs, const SkyrimModel::BuildContext& context, Report& report);

    /// "Skyrim actors: 5 built, 31 parts drawn ..." for the log.
    std::string describeTotals();
}

#endif
