#ifndef OPENEDEN_VIEW_POINTSHADOW_H
#define OPENEDEN_VIEW_POINTSHADOW_H

#include <memory>
#include <vector>

#include <vsg/app/Camera.h>
#include <vsg/app/ProjectionMatrix.h>
#include <vsg/app/RenderGraph.h>
#include <vsg/app/View.h>
#include <vsg/core/Array.h>
#include <vsg/state/Descriptor.h>

namespace vsg
{
    class Context;
}

namespace View
{
    /// The projection of a point light's shadow face (task 0077): `Render::PointShadow::faceProjectionMatrix`. Owned by
    /// the camera of the face that uses it; the main thread changes it between frames.
    class PointShadowProjection : public vsg::Inherit<vsg::ProjectionMatrix, PointShadowProjection>
    {
    public:
        double tanHalf = 1.0;
        double far = 100.0;

        vsg::dmat4 transform() const override;
        void changeExtent(const VkExtent2D& /*prevExtent*/, const VkExtent2D& /*newExtent*/) override {}
    };

    /*
     * The shadow maps of the nearest point lights (task 0077, docs/tasks/0077-point-light-shadows.md).
     *
     * One D32_SFLOAT 2D array of `slotCount * 6` layers of `resolution` squared texels: face f of slot k is layer
     * 6 k + f. Every face has a render graph over its own layer and a view with its own camera. The class owns the
     * image, the framebuffers, the render graphs, the views, the sampler and the data buffer (binding 19); the
     * caller (MWRender::PointShadows) chooses the lights and says where each slot's cameras stand.
     *
     * Thread: everything on the main thread, between frames.
     */
    class PointShadow
    {
    public:
        PointShadow(vsg::Context& ctx, int slotCount, std::uint32_t resolution, std::size_t maxLights);
        ~PointShadow();

        const int slotCount;
        const std::uint32_t resolution;
        /// The face field of view with the guard band (Render::PointShadow::tanHalfFov).
        const double tanHalf;

        vsg::Descriptor* shadowMap() { return mShadowMap; }
        vsg::Descriptor* shadowSampler() { return mShadowSampler; }
        vsg::Descriptor* dataDescriptor() { return mDataDescriptor; }
        /// The data buffer's elements (Render::PointShadow::bufferVec4s); the caller packs into it and calls dirty().
        vsg::ref_ptr<vsg::vec4Array> data() { return mData; }

        vsg::ref_ptr<vsg::RenderGraph> renderGraph(int slot, int face) const;
        vsg::ref_ptr<vsg::View> faceView(int slot, int face) const;

        /// Puts the six cameras of a slot at the light (world position) with the given far plane.
        void setSlotLight(int slot, const vsg::dvec3& position, double far);

    private:
        struct Face;
        std::vector<std::unique_ptr<Face>> mFaces;
        vsg::ref_ptr<vsg::Descriptor> mShadowMap;
        vsg::ref_ptr<vsg::Descriptor> mShadowSampler;
        vsg::ref_ptr<vsg::Descriptor> mDataDescriptor;
        vsg::ref_ptr<vsg::vec4Array> mData;
        vsg::ref_ptr<vsg::View> mBaseView;
    };
}

#endif
