#ifndef VSGOPENMW_ANIMATION_BONES_H
#define VSGOPENMW_ANIMATION_BONES_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <vsg/core/ref_ptr.h>

namespace vsg
{
    class Node;
}

namespace Anim
{
    /// openeden-vsg: the value set on a skeleton file's node that a mod marks as a new bone.
    inline constexpr const char* sCustomBoneKey = "CustomBone";

    class Transform;
    using BonePath = std::vector<vsg::ref_ptr<Transform>>;
    struct Bone
    {
        BonePath path;
        uint32_t group = 0;
        Transform* node() { return path.back(); }
    };

    /*
     * Bones is a container class providing access to a hierarchy of bones by name.
     */
    class Bones
    {
        friend class InitBones;
        friend class CopyBones;
        friend class FindCustomBones;
        std::unordered_map<std::string, Bone> mByName;

    public:
        Bones();
        ~Bones();

        /*
         * Specifies whether bone nodes are initially attached to each other. If set to false, all bones' children are
         * initially empty and their hierarchy can only be obtained from the BonePath member.
         * @note Attaching only required bones as needed can reduce the size of the scene graph that needs to be
         * traversed each frame.
         */
        const bool attached;

        /*
         * Optionally supports case-insensitive searches.
         */
        using NormalizeFunc = std::function<std::string(const std::string&)>;
        NormalizeFunc normalizeFunc = [](auto& s) { return s; };

        /*
         * Creates detached copy of bones from provided graph.
         */
        Bones(NormalizeFunc f, const vsg::Node& graphToCopy, const std::map<std::string, uint32_t>& groups = {});

        /*
         * Reads attached bones from provided graph.
         */
        Bones(NormalizeFunc f, vsg::Node& scene);

        Bone* search(const std::string& name);

        /// openeden-vsg: adds detached copies of the custom bones in \p graph, each under the existing bone named as
        /// its parent in that graph. A bone already present stays as it is (task 0012).
        void addCustomBones(const vsg::Node& graph);

        /// openeden-vsg: visits every bone with its normalized name, in no particular order (task 0011).
        template <class Func>
        void forEach(Func&& func)
        {
            for (auto& [name, bone] : mByName)
                func(name, bone);
        }
    };
}

#endif
