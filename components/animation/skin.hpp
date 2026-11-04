#ifndef VSGOPENMW_ANIMATION_SKIN_H
#define VSGOPENMW_ANIMATION_SKIN_H

#include <optional>

#include <vsg/core/Array.h>

#include "updatedata.hpp"

namespace Anim
{
    struct BoneData
    {
        vsg::mat4 invBindMatrix;
        /*vsg::sphere*/ vsg::dsphere bounds;
        std::string name;
    };
    class Transform;

    /*
     * Controller class for accumulating bone matrices for use by skinning shaders.
     * The link(..) method will find the parent Cull/DepthSorted and StateGroup nodes, for updating the bounds
     * automatically, and setting up a SoftwareSkin object for the StateGroup to use for intersections on the CPU.
     */
    class Skin : public MUpdateData<Skin, vsg::mat4Array>
    {
        void optimizePaths();
        /*
         * Automatically updates bounds of untransformed parent Cull/DepthSorted nodes.
         */
        void updateBounds();
        class PrivateBoneData;
        std::vector<PrivateBoneData> mBones;
        std::vector<const Transform*> mReferenceFrame;
        std::vector<vsg::dsphere*> mDynamicBounds;
        bool mWrittenOnce = false;

    public:
        Skin();
        Skin(const Skin&);
        ~Skin();
        /*
         * Bone information set by users.
         */
        std::vector<BoneData> bones;
        vsg::ref_ptr<vsg::vec4Array> boneIndices;
        vsg::ref_ptr<vsg::vec4Array> boneWeights;
        std::optional<vsg::mat4> transform;

        /// Task 0026 (F3c): when set before skinned models are loaded, each bone matrix array is twice as long as the
        /// skin has bones: the first half is this frame's matrices, as always, the second half the previous frame's
        /// (the G-buffer's motion output needs them). Set once at start, by RenderManager, only with the G-buffer on.
        static inline bool keepPreviousMatrices = false;

        void apply(vsg::mat4Array& array, float);

        /// Task 0069 stage e review, finding 5: for a skin that did not run for a while (the merged body leaves out the
        /// skin of a draw equipment hides entirely), the next apply writes this frame's matrices to both halves, as the
        /// first one does: one frame without motion instead of the motion from the pose of when it stopped.
        void forgetPreviousMatrices() { mWrittenOnce = false; }

        /*
         * Links to parent cull node and StateGroup.
         */
        void link(Context& context, vsg::Object&) override;
    };
}

#endif
