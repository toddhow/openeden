#ifndef OPENEDEN_VIEW_CLOUDVOLUME_H
#define OPENEDEN_VIEW_CLOUDVOLUME_H

#include <array>
#include <chrono>

#include <vsg/commands/Commands.h>
#include <vsg/core/Array.h>
#include <vsg/io/Options.h>
#include <vsg/maths/mat4.h>
#include <vsg/nodes/Switch.h>
#include <vsg/state/DescriptorSet.h>
#include <vsg/state/Image.h>

#include <components/render/cloudvolumegpu.hpp>
#include <components/render/cloudvolumemath.hpp>

namespace View
{
    /*
     * OpenEden (task 0042): the ray-marched volumetric clouds.
     *
     * Owns the noise textures (made once at start by a compute pass from a fixed seed), the marched image at a fraction
     * of the screen size, the full size resolved picture and its history, the parameter block, and the compute commands
     * that march, resolve and keep the history. Made only when `[Rendering] volumetric clouds` is on, so with it off
     * none of this exists. The main thread creates it and calls `setParams` each frame; the record thread reads the
     * parameter block and the images through the descriptors. Ownership: the commands hold the images and the
     * pipelines; `resultDescriptor` shares the resolved image with the sky's cloud dome.
     *
     * The commands use the main view's descriptor set (the scene block: the camera, the sun, the ambient and fog
     * colours). They read nothing another graph writes, so their command graph runs first of the frame (see
     * `RenderManager`).
     */
    class CloudVolume
    {
    public:
        // `fullSize` is the size of the resolved picture (the render target's, at start).
        CloudVolume(Render::CloudVolume::Quality quality, std::array<std::uint32_t, 2> fullSize);

        Render::CloudVolume::Quality quality() const { return mQuality; }
        std::array<std::uint32_t, 2> marchSize() const { return mMarchSize; }
        std::array<std::uint32_t, 2> fullSize() const { return mFullSize; }

        // True if every compute shader file is there. Checked before the object is made, since its ground shadow map is
        // put into the view set before the commands are built and would then be read without ever being written.
        static bool shadersAvailable(vsg::ref_ptr<const vsg::Options> shaderOptions);

        // Reads the compiled shaders and builds the commands. False (and a log line) if a shader file is missing, for
        // example when the build has no slangc; the caller then leaves the clouds off.
        bool createCommands(vsg::ref_ptr<const vsg::Options> shaderOptions, vsg::ref_ptr<vsg::DescriptorSet> viewSet);

        // The commands to run once a frame (null before `createCommands`).
        vsg::ref_ptr<vsg::Node> node() const { return mNode; }

        // The ground shadow map as a combined image sampler at view binding 16 (the main view's set takes it in place
        // of the stand-in). Available before `createCommands`.
        vsg::ref_ptr<vsg::Descriptor> shadowDescriptor() const { return mShadowDescriptor; }

        // The resolved picture as a combined image sampler at the diffuse binding, for the sky's cloud dome.
        vsg::ref_ptr<vsg::Descriptor> resultDescriptor() const { return mResultDescriptor; }

        // Main thread, each frame, with the camera's view and projection matrices of this frame: the history is looked
        // up through the previous frame's pair (rotation only: the clouds are too far for a move to shift them), and
        // thrown away on the first frame, after a jump of the camera, a pause, or a jump in the coverage.
        void setParams(Render::CloudVolume::ParamsInput input, const vsg::dmat4& view, const vsg::dmat4& projection);

        // Main thread: stop running the passes while the sky shows no volume (a weather that keeps the vanilla clouds,
        // an interior). The history is dropped; the noise is built on the first frames that run.
        void setEnabled(bool enabled);
        bool enabled() const { return mEnabled; }

        // Frames since the start (for the debug log).
        std::uint32_t frame() const { return mFrame; }

    private:
        Render::CloudVolume::Quality mQuality;
        std::array<std::uint32_t, 2> mMarchSize;
        std::array<std::uint32_t, 2> mFullSize;
        vsg::ref_ptr<vsg::Image> mShapeImage;
        vsg::ref_ptr<vsg::Image> mDetailImage;
        vsg::ref_ptr<vsg::Image> mCurlImage;
        vsg::ref_ptr<vsg::Image> mMarchImage;
        vsg::ref_ptr<vsg::Image> mResolveImage;
        vsg::ref_ptr<vsg::Image> mHistoryImage;
        vsg::ref_ptr<vsg::Image> mShadowImage;
        vsg::ref_ptr<vsg::Descriptor> mShadowDescriptor;
        // The last parameters, kept for the frames when the clouds are not drawn (the shadow map then says "no
        // shadow").
        std::array<float, Render::CloudVolume::paramsFloats> mLastParams{};
        // The extra vec4 after the main block (the planet radius, the variety flag).
        std::array<float, Render::CloudVolume::curveParamsFloats> mLastCurve{};
        // The first frames put the images into their layout and build the noise; later ones keep their contents.
        vsg::ref_ptr<vsg::Switch> mFirstSwitch;
        vsg::ref_ptr<vsg::Switch> mEnableSwitch;
        bool mEnabled = true;
        std::uint32_t mFrame = 0;
        bool mHavePrevious = false;
        vsg::dmat4 mPreviousView;
        vsg::dmat4 mPreviousProjection;
        vsg::dvec3 mPreviousEye;
        std::chrono::steady_clock::time_point mPreviousTime;
        float mPreviousCoverage = 0.f;
        vsg::ref_ptr<vsg::vec4Array> mParams;
        vsg::ref_ptr<vsg::Descriptor> mResultDescriptor;
        vsg::ref_ptr<vsg::Node> mNode;
    };
}

#endif
