#ifndef VSGOPENMW_MWANIMATION_CLONE_H
#define VSGOPENMW_MWANIMATION_CLONE_H

#include <string>
#include <vector>

#include <vsg/core/Value.h>
#include <vsg/core/ref_ptr.h>

#include <components/animation/animation.hpp>
#include <components/animation/contents.hpp>
#include <components/pipeline/material.hpp>

namespace vsg
{
    class Node;
    class Group;
    class Descriptor;
}
namespace Anim
{
    class Transform;
}
namespace MWAnim
{
    struct Placeholders
    {
        vsg::Group* attachLight{};
        Anim::Transform* attachAmmo{};
        const Anim::Transform* boneOffset{};
    };

    struct CloneResult : public Anim::Animation
    {
        Anim::Contents contents;
    };
    struct PlaceholderResult : public CloneResult
    {
        Placeholders placeholders;
    };

    /*
     * Implements clone callbacks.
     */
    CloneResult cloneIfRequired(vsg::ref_ptr<vsg::Node>& node);
    PlaceholderResult clonePlaceholdersIfRequired(vsg::ref_ptr<vsg::Node>& node);
    CloneResult cloneIfRequired(vsg::ref_ptr<vsg::Node>& node, const std::string& removeNodesStartingWith);
    CloneResult cloneAndReplace(vsg::ref_ptr<vsg::Node>& node, vsg::ref_ptr<vsg::Descriptor> texture = {},
        bool overrideAllTextures = true, const std::vector<vsg::ref_ptr<vsg::Node>>& replaceDummyNodesWith = {});

    using MaterialValue = vsg::Value<Pipeline::Data::Material>;
    /*
     * openeden-vsg: clones like cloneIfRequired, and gives the copy its own dynamic copy of every material value
     * (MaterialValue) it uses, appended to materials, so the owner can change this instance's materials without
     * touching models that share them (task 0012: the weather effects' fade, OpenMW's AlphaFader). A material value
     * used several times in the model gets one copy, used everywhere the original was. Copies keep the original's
     * material controller, and every other descriptor is cloned as cloneIfRequired would. Call before compiling.
     */
    CloneResult cloneWithOwnMaterials(
        vsg::ref_ptr<vsg::Node>& node, std::vector<vsg::ref_ptr<MaterialValue>>& materials);
}

#endif
