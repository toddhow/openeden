#ifndef OPENEDEN_VIEW_VOLUMEFOG_H
#define OPENEDEN_VIEW_VOLUMEFOG_H

#include <array>
#include <chrono>

#include <vsg/commands/Commands.h>
#include <vsg/maths/mat4.h>
#include <vsg/nodes/Switch.h>
#include <vsg/core/Array.h>
#include <vsg/io/Options.h>
#include <vsg/state/DescriptorSet.h>
#include <vsg/state/Image.h>

#include <components/render/volumegpu.hpp>
#include <components/render/volumemath.hpp>

namespace View
{
    /*
     * OpenEden (task 0041): the volumetric fog.
     *
     * Owns the 3D images (the fill image and the result the forward shaders read), the parameter block, and the compute
     * commands that fill and integrate the volume. Made only when `[Rendering] volumetrics` is on, so with it off none
     * of this exists. The main thread creates it and calls `setParams` each frame; the record thread reads the
     * parameter block and the images through the descriptors. Ownership: the commands hold the images and the pipelines.
     *
     * The commands use the main view's descriptor set (the scene block, the sun's shadow cascades, the light grid), so
     * they must run after the shadow maps and the light grid are done: see `RenderManager`.
     */
    class VolumeFog
    {
    public:
        // `temporal`: blend each frame's samples with the previous frame's (needs two more images).
        VolumeFog(Render::Volume::Quality quality, bool temporal);

        Render::Volume::Quality quality() const { return mQuality; }
        Render::Volume::GridSize grid() const { return mGrid; }

        // The descriptors the main view's set takes in place of the stand-ins (bindings 14 and 15).
        vsg::ref_ptr<vsg::Descriptor> volumeDescriptor() const { return mVolumeDescriptor; }
        vsg::ref_ptr<vsg::Descriptor> paramsDescriptor() const { return mParamsDescriptor; }

        // Reads the compiled shaders and builds the commands. False (and a log line) if a shader file is missing, for
        // example when the build has no slangc; the caller then leaves the volume off.
        bool createCommands(vsg::ref_ptr<const vsg::Options> shaderOptions, vsg::ref_ptr<vsg::DescriptorSet> viewSet);

        // The commands to run once a frame (null before `createCommands`).
        vsg::ref_ptr<vsg::Node> node() const { return mNode; }

        bool temporal() const { return mTemporal; }

        // Main thread, each frame, with the camera's view and projection matrices of this frame: the history is looked up
        // through the previous frame's pair, and thrown away on the first frame and after a jump of the camera.
        void setParams(Render::Volume::ParamsInput input, const vsg::dmat4& view, const vsg::dmat4& projection);

    private:
        Render::Volume::Quality mQuality;
        bool mTemporal;
        Render::Volume::GridSize mGrid;
        vsg::ref_ptr<vsg::Image> mFillImage;
        vsg::ref_ptr<vsg::Image> mResultImage;
        std::array<vsg::ref_ptr<vsg::Image>, 2> mBlendImages; // the blend and the history, swapping each frame
        // Which of the two sets of commands runs (it follows the frame counter), and which barrier: the first frame puts
        // the blend images into their layout, later ones keep their contents.
        vsg::ref_ptr<vsg::Switch> mParitySwitch;
        vsg::ref_ptr<vsg::Switch> mBarrierSwitch;
        std::uint32_t mFrame = 0;
        bool mHavePrevious = false;
        vsg::dmat4 mPreviousView;
        vsg::dmat4 mPreviousProjection;
        vsg::dvec3 mPreviousEye;
        std::chrono::steady_clock::time_point mPreviousTime;
        float mPreviousReach = 0.f;
        bool mPreviousEnabled = false;
        vsg::ref_ptr<vsg::vec4Array> mParams;
        vsg::ref_ptr<vsg::Descriptor> mVolumeDescriptor;
        vsg::ref_ptr<vsg::Descriptor> mParamsDescriptor;
        vsg::ref_ptr<vsg::Node> mNode;
    };
}

#endif
