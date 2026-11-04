#include "scenehost.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "camerapose.hpp"

#include <vsg/app/Camera.h>
#include <vsg/app/CommandGraph.h>
#include <vsg/app/CompileManager.h>
#include <vsg/app/ProjectionMatrix.h>
#include <vsg/app/RenderGraph.h>
#include <vsg/app/View.h>
#include <vsg/app/Viewer.h>
#include <vsg/app/Window.h>
#include <vsg/nodes/AbsoluteTransform.h>
#include <vsg/nodes/StateGroup.h>
#include <vsg/nodes/VertexDraw.h>
#include <vsg/state/ArrayState.h>
#include <vsg/state/ViewportState.h>
#include <vsg/utils/FindDynamicObjects.h>
#include <vsg/utils/PropagateDynamicObjects.h>
#include <vsg/utils/SharedObjects.h>
#include <vsg/vk/ResourceRequirements.h>

#include <vsg/ui/PointerEvent.h>
#include <vsgQt/Viewer.h>
#include <vsgQt/Window.h>

#include <components/debug/debuglog.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/mwanimation/partbones.hpp>
#include <components/pipeline/builder.hpp>
#include <components/pipeline/graphics.hpp>
#include <components/pipeline/material.hpp>
#include <components/pipeline/override.hpp>
#include <components/pipeline/sets.hpp>
#include <components/render/engine.hpp>
#include <components/render/screenshot.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/settings.hpp>
#include <components/settings/values.hpp>
#include <components/view/barriers.hpp>
#include <components/view/collectlights.hpp>
#include <components/view/defaultstate.hpp>
#include <components/view/descriptors.hpp>
#include <components/view/env.hpp>
#include <components/view/lightgrid.hpp>
#include <components/view/scene.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/computebin.hpp>
#include <components/vsgutil/setbin.hpp>
#include <components/vsgutil/sharedview.hpp>

#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/record.hpp>

#include "mask.hpp"
#include "rendercontext.hpp"

namespace
{
    // The game's number (apps/openmw/mwrender/bin.hpp), as RenderContext passes to the model readers.
    constexpr int sBinCompute = 1;

    // View::CollectLights' capacity in the game (MWRender::RenderManager::RenderManager).
    constexpr size_t sMaxLights = 256;

    // Straight down, as the OSG editor's lights were (osg::Vec4f(0, 0, 1, 0) in LightingDay and its kin).
    const vsg::vec3 sLightPosition(0.f, 0.f, 1.f);
    // The game's light size without a sky (RenderManager::updateSceneData).
    constexpr float sLightSize = 8.f;
    // The default view of a cell, from outside its objects: the direction from their centre and the smallest distance.
    const vsg::dvec3 sDefaultViewDirection(-0.6, -0.6, 0.5);
    constexpr double sMinimumViewDistance = 500.0;
    constexpr double sViewDistanceScale = 2.0;
    // The OSG editor let OSG compute near and far from what it drew, which VSG does not do. This covers a large
    // selection of exterior cells.
    constexpr double sFarClip = 200000.0;
    // As the OSG editor: half the height of an orthographic view is ten units per step of camera-ortho-size.
    constexpr double sOrthoUnitsPerSize = 10.0;
    // The unlit colour mode of the default shader, as Render::DebugGeometry.
    constexpr uint32_t sUnlitColorMode = 3;
    // Within clip space in any depth convention; the gradient has no depth test.
    constexpr double sGradientNdcDepth = 0.5;

    /// The descriptor pool sizes the game asks for (RenderManager::RenderManager).
    vsg::ResourceRequirements viewResourceRequirements()
    {
        auto hints = vsg::ResourceHints::create();
        const uint32_t setsPerPool = 64;
        hints->numDescriptorSets = setsPerPool;
        // As vsgUtil::createSharedView's views: pipelines take their viewport when drawn, so one compile fits every
        // view.
        hints->viewportStateHint = vsg::DYNAMIC_VIEWPORTSTATE;
        hints->descriptorPoolSizes = { { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }, { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1 },
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1 }, { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * setsPerPool },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 * setsPerPool },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 7 * setsPerPool } };
        return vsg::ResourceRequirements(hints);
    }

    template <class Value>
    double medianOf(const std::vector<vsg::dvec3>& positions, Value value)
    {
        std::vector<double> values;
        values.reserve(positions.size());
        for (const vsg::dvec3& position : positions)
            values.push_back(value(position));
        const auto middle = values.begin() + values.size() / 2;
        std::nth_element(values.begin(), middle, values.end());
        return *middle;
    }

    // The options of MWRender::animContext (apps/openmw/mwrender/animcontext.cpp), which the game's file cannot give
    // the editor: it names game masks and lives in the game's tree.
    vsg::ref_ptr<vsg::Options> sharedReadOptions(const vsg::Options& base)
    {
        auto options = vsg::Options::create(base);
        options->sharedObjects = vsg::SharedObjects::create();
        // VSG 1.1.x clones "dynamic" objects of shared reads by default, which turns Anim::Transform roots into plain
        // groups; the mwanimation code clones what it needs itself.
        options->findDynamicObjects = {};
        options->propagateDynamicObjects = {};
        return options;
    }

    vsg::ref_ptr<vsg::Options> actorOptions(Resource::ResourceSystem* resources)
    {
        auto options = sharedReadOptions(*resources->nodeOptions);
        options->findFileCallback = [resources](const vsg::Path& file, const vsg::Options*) -> vsg::Path {
            VFS::Path::Normalized path(file.string());
            if (!path.value().starts_with("meshes/") && !path.value().starts_with("animations/"))
                path = Misc::ResourceHelpers::correctMeshPath(path);
            return vsg::Path(Misc::ResourceHelpers::correctActorModelPath(path, resources->getVFS()).value());
        };
        return options;
    }

    vsg::ref_ptr<vsg::Options> effectOptions(const vsg::Options& base)
    {
        auto options = sharedReadOptions(base);
        auto override_ = vsg::ref_ptr{ new Pipeline::Override };
        override_->materialData = [](Pipeline::Data::Material& m) {
            // Morrowind has a white ambient light attached to the root VFX node of the scenegraph
            m.emissive += vsg::vec4(m.ambient.x, m.ambient.y, m.ambient.z, 0);
        };
        override_->composite(*options);
        return options;
    }

    std::vector<vsg::ref_ptr<const vsg::Options>> partBoneOptions(const vsg::Options& base)
    {
        std::vector<vsg::ref_ptr<const vsg::Options>> ret(ESM::PRT_Count);
        for (int i = 0; i < ESM::PRT_Count; ++i)
        {
            auto options = sharedReadOptions(base);
            const auto& bonename = MWAnim::sPartBones.at(static_cast<ESM::PartReferenceType>(i));
            if (bonename.find("Left") != std::string::npos)
            {
                auto override_ = vsg::ref_ptr{ new Pipeline::Override };
                override_->pipelineOptions = [](Pipeline::Options& o) {
                    if (!o.shader.hasMode(Pipeline::Mode::SKIN))
                        o.frontFace = VK_FRONT_FACE_CLOCKWISE;
                };
                override_->composite(*options);
            }
            // PRT_Hair uses a filter different from its attachment bone
            options->setValue("skinfilter", (i == ESM::PRT_Hair) ? "hair" : bonename);
            ret[i] = options;
        }
        return ret;
    }
}

namespace CSVRender
{
    SceneHost::SceneHost(RenderContext& context, vsgQt::Window& window, CSMWorld::Data& data)
        : mContext(context)
        , mWindow(window)
        , mData(data)
        , mLastUpdate(std::chrono::steady_clock::now())
    {
        // Lay out first: Engine refuses a window with no size.
        window.initializeWindow();
        mEngine = std::make_unique<Render::Engine>(vsg::ref_ptr<vsg::Viewer>(context.viewer()), window.windowAdapter);

        // As the game asks for them (apps/openmw/vsgengine.cpp), before the first window's device exists.
        auto& features = window.windowAdapter->traits()->deviceFeatures->get();
        if (!mEngine->getDevice())
        {
            const auto& supported = mEngine->getOrCreatePhysicalDevice()->getFeatures();
            features.samplerAnisotropy = supported.samplerAnisotropy;
            features.occlusionQueryPrecise = supported.occlusionQueryPrecise;
            features.depthClamp = supported.depthClamp;
            features.fillModeNonSolid = VK_TRUE;
        }
        auto device = mEngine->getOrCreateDevice();
        if (context.device() && context.device() != device)
            throw std::logic_error("CSVRender::SceneHost: the window was created before the first view had its device");
        context.deviceReady(device, features.samplerAnisotropy);

        mEngine->setRenderPass(context.renderPass(device, mEngine->framebufferSamples(), mEngine->getPresentLayout()));

        mResources = context.resourcesFor(data);

        mAnimContext.files.wolfskin = Settings::Manager::getString("wolfskin", "Models");
        mAnimContext.files.baseanim = Settings::Manager::getString("baseanim", "Models");
        mAnimContext.files.baseanimkna = Settings::Manager::getString("baseanimkna", "Models");
        mAnimContext.files.baseanimfemale = Settings::Manager::getString("baseanimfemale", "Models");
        mAnimContext.mask = { Mask_ParticleSystem };
        mAnimContext.nodeOptions = mResources->nodeOptions;
        mAnimContext.effectOptions = effectOptions(*mResources->nodeOptions);
        mAnimContext.actorOptions = actorOptions(mResources.get());
        mAnimContext.textureOptions = mResources->textureOptions;
        mAnimContext.partBoneOptions = partBoneOptions(*mResources->nodeOptions);
        mAnimContext.animOptions = mResources->animationOptions;
        mAnimContext.compileContext = context.compileContext();

        // The view's descriptors, in the game's order but with the stand-ins of its shadow pass for shadows and the
        // water's reflections, which an editor view does not have (RenderManager::createCastShadowState).
        mSceneData = std::make_unique<View::Scene>();
        auto collectLights = vsg::ref_ptr{ new View::CollectLights(sMaxLights) };
        vsg::Descriptors descriptors = View::createLightingDescriptors(collectLights->data(), true);
        descriptors.emplace_back(View::readEnv(mResources->textureOptions));
        descriptors.emplace_back(View::dummyShadowMap());
        descriptors.emplace_back(View::dummyShadowSampler());
        descriptors.emplace_back(View::dummyReflectDepth());
        descriptors.emplace_back(View::dummyProbes());
        descriptors.emplace_back(View::dummyReflections());
        descriptors.emplace_back(View::dummySkyVis());
        descriptors.emplace_back(View::dummySkyVisSky());
        descriptors.emplace_back(View::dummyVolume());
        descriptors.emplace_back(View::dummyVolumeParams());
        descriptors.emplace_back(View::dummyCloudShadow());
        descriptors.emplace_back(mSceneData->descriptor());
        auto viewDescriptorSet = View::createViewDescriptorSet(descriptors);
        mSceneRoot = View::createDefaultState(viewDescriptorSet);

        mContentRoot = vsg::Group::create();
        createGradient();
        mSceneRoot->children = { mGradientSwitch, mContentRoot };

        mLightGrid = std::make_unique<View::LightGrid>(*mResources->shaderOptions, viewDescriptorSet);
        auto computeGroup = vsg::Group::create();
        computeGroup->children = { View::preComputeBarrier(), mLightGrid->node() };
        auto viewRoot = vsg::Group::create();
        viewRoot->children = { vsgUtil::createBinSetter(sBinCompute, computeGroup), mSceneRoot };

        mExtent = mEngine->extent2D();
        mProjection = vsg::Perspective::create();
        mViewMatrix = vsg::LookAt::create();
        mViewport = vsg::ViewportState::create(mExtent);
        mCamera = vsg::Camera::create(mProjection, mViewMatrix, mViewport);

        mView = vsgUtil::createSharedView(mCamera, viewRoot);
        mView->viewDependentState = collectLights;
        mView->bins.push_back(vsg::Bin::create(sBinDepthSorted, vsg::Bin::DESCENDING));
        auto computeBin = vsgUtil::ComputeBin::create(); // VUID-vkCmdDispatch-renderpass
        computeBin->binNumber = sBinCompute;
        computeBin->commandGraph = mEngine->createCommandGraph(Pipeline::COMPUTE_SET);
        computeBin->commandGraph->submitOrder = -1;
        mView->bins.push_back(computeBin);
        mView->bins.push_back(vsg::Bin::create(sBinOverlay, vsg::Bin::NO_SORT));

        mRenderGraph = mEngine->createRenderGraph();
        mRenderGraph->children = { mView };

        auto commandGraph = mEngine->createCommandGraph(Pipeline::COMPUTE_SET);
        commandGraph->children = { View::postComputeBarrier(), mRenderGraph };

        // Captures copy this window's image at the end of its only graph, inside the frame, before present
        // (Render::Engine::drawFrameAndCapture).
        mEngine->setScreenshotGraph(commandGraph);
        // threading = false: the viewer's owner is a Qt timer on the main thread, and it decides for all of its
        // windows.
        mEngine->setup({ commandGraph }, false);
        mEngine->compile();
        // After setup, so that the compile view gets a transfer task (research note 0003, section 5.4).
        context.addCompileTarget(*window.windowAdapter, viewResourceRequirements());

        updateExtent();
        context.add(*this);
    }

    SceneHost::~SceneHost()
    {
        mContext.remove(*this);
        // Takes the window's graphs out of the viewer's tasks, and leaves the other views' alone. vsgQt did it already
        // if the surface went first.
        if (mWindow.windowAdapter)
            mContext.viewer()->removeWindow(mWindow.windowAdapter);
    }

    vsg::ref_ptr<vsgUtil::CompileContext> SceneHost::compileContext() const
    {
        return mContext.compileContext();
    }

    ESM::RefId SceneHost::findCell(const CSMWorld::Data& data, const std::string& nameOrId)
    {
        const auto& cells = data.getCells();
        const int size = cells.getSize();
        for (int i = 0; i < size; ++i)
            if (Misc::StringUtils::ciEqual(cells.getRecord(i).get().mId.getRefIdString(), nameOrId))
                return cells.getRecord(i).get().mId;
        for (int i = 0; i < size; ++i)
            if (Misc::StringUtils::ciEqual(cells.getRecord(i).get().mName, nameOrId))
                return cells.getRecord(i).get().mId;
        return {};
    }

    vsg::ref_ptr<vsg::Switch> SceneHost::maskNode(unsigned int mask, vsg::ref_ptr<vsg::Node> child)
    {
        auto node = vsg::Switch::create();
        node->addChild(vsg::Mask(mask), child);
        return node;
    }

    void SceneHost::setVisibilityMask(unsigned int mask)
    {
        // Mask_Lighting has no node of its own here. A vsg::Switch shows a child if its mask meets the view's.
        mView->mask = vsg::Mask(mask | Mask_ParticleSystem | Mask_Lighting);
        requestRedraw();
    }

    void SceneHost::setCamera(const vsg::dvec3& eye, const vsg::dvec3& center, const vsg::dvec3& up)
    {
        mViewMatrix->eye = eye;
        mViewMatrix->center = center;
        mViewMatrix->up = up;
        requestRedraw();
    }

    vsg::dmat4 SceneHost::getViewMatrix() const
    {
        return mViewMatrix->transform();
    }

    void SceneHost::setViewMatrix(const vsg::dmat4& view)
    {
        // A view that did not change asks for no frame. A controller normalizes every tick, so rounding does not count.
        if (CameraPose::sameView(view, mViewMatrix->transform()))
            return;
        const CameraPose::LookAt lookAt = CameraPose::lookAtFrom(view);
        setCamera(lookAt.mEye, lookAt.mCenter, lookAt.mUp);
    }

    std::vector<Picking::Result> SceneHost::pickAll(double x, double y, unsigned int mask) const
    {
        return Picking::pickAll(
            *mContentRoot, Picking::viewRay(mProjection->transform(), mViewMatrix->transform(), x, y), vsg::Mask(mask));
    }

    Picking::Result SceneHost::missAt(double x, double y, double placementDistance) const
    {
        const Picking::Ray ray = Picking::viewRay(mProjection->transform(), mViewMatrix->transform(), x, y);
        Picking::Result result;
        result.mWorldPos = ray.mStart + vsg::normalize(ray.mEnd - ray.mStart) * placementDistance;
        return result;
    }

    vsg::dmat4 SceneHost::getProjectionMatrix() const
    {
        return mProjection->transform();
    }

    std::optional<vsg::dvec3> SceneHost::pickNearest(double x, double y, unsigned int mask) const
    {
        vsg::dvec3 position;
        if (Picking::pickNearest(*mContentRoot,
                Picking::viewRay(mProjection->transform(), mViewMatrix->transform(), x, y), mask, position))
            return position;
        return std::nullopt;
    }

    Picking::Result SceneHost::pick(double x, double y, unsigned int mask, double placementDistance) const
    {
        return Picking::pick(*mContentRoot, Picking::viewRay(mProjection->transform(), mViewMatrix->transform(), x, y),
            mask, placementDistance);
    }

    vsg::dvec3 SceneHost::getCameraEye() const
    {
        return mViewMatrix->eye;
    }

    vsg::dvec3 SceneHost::getCameraCenter() const
    {
        return mViewMatrix->center;
    }

    double SceneHost::worldPerPixel(const vsg::dvec3& position) const
    {
        const double height = std::max(mExtent.height, 1u);
        if (mOrtho)
            return 2.0 * mOrthoSize * sOrthoUnitsPerSize / height;
        const vsg::dvec3 forward = vsg::normalize(mViewMatrix->center - mViewMatrix->eye);
        const double depth = std::max(vsg::dot(position - mViewMatrix->eye, forward), 1.0);
        return 2.0 * depth * std::tan(vsg::radians(mFieldOfView) / 2.0) / height;
    }

    vsg::dvec3 SceneHost::cameraRight() const
    {
        return vsg::normalize(vsg::cross(mViewMatrix->center - mViewMatrix->eye, mViewMatrix->up));
    }

    vsg::dvec3 SceneHost::cameraUp() const
    {
        return vsg::normalize(vsg::cross(cameraRight(), mViewMatrix->center - mViewMatrix->eye));
    }

    void SceneHost::frame(const std::vector<vsg::dvec3>& positions)
    {
        if (positions.empty())
            return;
        // Medians, not the bounds: many Morrowind interiors keep a few objects far outside the room.
        const vsg::dvec3 center(medianOf(positions, [](const vsg::dvec3& p) { return p.x; }),
            medianOf(positions, [](const vsg::dvec3& p) { return p.y; }),
            medianOf(positions, [](const vsg::dvec3& p) { return p.z; }));
        const double distance = std::max(
            sViewDistanceScale * medianOf(positions, [&](const vsg::dvec3& p) { return vsg::length(p - center); }),
            sMinimumViewDistance);
        setCamera(center + sDefaultViewDirection * distance, center, vsg::dvec3(0, 0, 1));
    }

    void SceneHost::setProjection(double fieldOfView, bool ortho, int orthoSize)
    {
        mFieldOfView = fieldOfView;
        mOrtho = ortho;
        mOrthoSize = orthoSize;
        updateProjection();
    }

    void SceneHost::updateProjection()
    {
        const double aspect = static_cast<double>(mExtent.width) / static_cast<double>(mExtent.height);
        const double nearClip = Settings::camera().mNearClip;
        if (mOrtho)
        {
            const double halfHeight = mOrthoSize * sOrthoUnitsPerSize;
            mProjection = vsg::Orthographic::create(
                -halfHeight * aspect, halfHeight * aspect, -halfHeight, halfHeight, nearClip, sFarClip);
        }
        else
            mProjection = vsg::Perspective::create(mFieldOfView, aspect, nearClip, sFarClip);
        mCamera->projectionMatrix = mProjection;

        mSceneData->setDepthRange(static_cast<float>(nearClip), static_cast<float>(sFarClip));
        auto& scene = mSceneData->value();
        scene.projection = vsg::mat4(mProjection->transform());
        scene.invProjection = vsg::inverse(scene.projection);
        updateGradient();
        requestRedraw();
    }

    void SceneHost::setLighting(const LightingValues& values)
    {
        mLighting = values;
        requestRedraw();
    }

    void SceneHost::setBackground(const vsg::vec4& top, const vsg::vec4& bottom, bool gradient)
    {
        mTopColour = top;
        mBottomColour = bottom;
        mRenderGraph->setClearValues(VkClearColorValue{ { top.r, top.g, top.b, 1.f } });
        mGradientSwitch->setAllChildren(gradient);
        updateGradient();
        requestRedraw();
    }

    void SceneHost::setNightDayMode(unsigned int index, bool apply)
    {
        mNightDayIndex = index;
        mNightDayApply = apply;
        requestRedraw();
    }

    void SceneHost::addListener(FrameListener& listener)
    {
        mListeners.push_back(&listener);
    }

    void SceneHost::removeListener(FrameListener& listener)
    {
        mListeners.erase(std::remove(mListeners.begin(), mListeners.end(), &listener), mListeners.end());
    }

    void SceneHost::createGradient()
    {
        // The OSG editor's camera of its own, with a quad filling the view. Here the quad's corners go through the
        // inverse of the projection, so one pipeline and no second camera do. It is the first thing the view draws.
        mGradientVertices = vsg::vec3Array::create(4);
        mGradientColours = vsg::vec4Array::create(4);
        mGradientVertices->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
        mGradientColours->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;

        Pipeline::Options options;
        options.shader.addMode(Pipeline::Mode::VERTEX);
        options.shader.addMode(Pipeline::Mode::COLOR);
        options.colorMode = sUnlitColorMode;
        options.primitiveTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        options.cullMode = VK_CULL_MODE_NONE;
        options.depthTest = false;
        options.depthWrite = false;

        auto draw = vsg::VertexDraw::create();
        draw->assignArrays({ mGradientVertices, mGradientColours });
        draw->vertexCount = 4;
        draw->instanceCount = 1;

        auto state = vsg::StateGroup::create();
        state->stateCommands = { mResources->builder->graphics->getOrCreate(options) };
        state->prototypeArrayState = vsg::NullArrayState::create();
        state->addChild(draw);

        auto absolute = vsg::AbsoluteTransform::create();
        absolute->addChild(state);

        mGradientSwitch = vsg::Switch::create();
        mGradientSwitch->addChild(vsg::MASK_ALL, absolute);
    }

    void SceneHost::updateGradient()
    {
        if (!mProjection)
            return;
        const vsg::dmat4 inverseProjection = vsg::inverse(mProjection->transform());
        // Vulkan's clip space has y down, so the first two corners are the top of the view.
        const double corners[4][2] = { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
        for (int i = 0; i < 4; ++i)
        {
            const vsg::dvec4 view
                = inverseProjection * vsg::dvec4(corners[i][0], corners[i][1], sGradientNdcDepth, 1.0);
            mGradientVertices->at(i) = vsg::vec3(vsg::dvec3(view.x, view.y, view.z) / view.w);
            mGradientColours->at(i) = i < 2 ? mTopColour : mBottomColour;
        }
        mGradientVertices->dirty();
        mGradientColours->dirty();
    }

    bool SceneHost::capture(const std::filesystem::path& file)
    {
        if (!hasWindow())
            return false;
        // OpenEden (screenshots, third review round): one more frame of the viewer is drawn here with the copy recorded
        // in it before present; the image presented last, which this read before, must not be used until it is
        // acquired again. The frame skips RenderContext::tick's deletion queue and endFrame: the next tick catches up.
        auto operation = mEngine->drawFrameAndCapture();
        if (!operation)
            return false;
        operation->writeOptions = mResources->imageOptions;
        operation->writeFilename = file;
        operation->operate();
        return operation->downloadData != nullptr;
    }

    void SceneHost::updateExtent()
    {
        mExtent = mEngine->extent2D();
        // Engine's render graphs have no resize handler: a render area larger than the swapchain draws nothing at all.
        mRenderGraph->renderArea = { { 0, 0 }, mExtent };
        mViewport->set(0, 0, mExtent.width, mExtent.height);
        updateProjection();
    }

    bool SceneHost::hasWindow() const
    {
        return mWindow.windowAdapter.valid();
    }

    bool SceneHost::needsFrame()
    {
        if (!hasWindow())
            return false;
        // A view in a hidden tab or a minimised window is not exposed, and need not draw for what moves by itself.
        const bool needed = mRedraw
            || (mWindow.isExposed()
                && std::any_of(mListeners.begin(), mListeners.end(),
                    [](const FrameListener* listener) { return listener->isAnimated(); }));
        mRedraw = false;
        return needed;
    }

    void SceneHost::update()
    {
        if (!hasWindow())
            return;
        const auto extent = mEngine->extent2D();
        if (extent.width != mExtent.width || extent.height != mExtent.height)
        {
            if (extent.width == 0 || extent.height == 0)
                return;
            updateExtent();
        }

        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - mLastUpdate).count();
        mLastUpdate = now;

        for (FrameListener* listener : mListeners)
            listener->frameUpdate(dt);

        // As RenderManager::updateSceneData, without sky, shadows and water. No fog: the OSG editor set none
        // (unpagedworldspacewidget.cpp: "\todo deal with mSunlight and mFog/mForDensity").
        const float far = static_cast<float>(sFarClip);
        mSceneData->setFogRange(far, far);
        auto& scene = mSceneData->value();
        scene.time += dt;
        scene.fogColor = mTopColour;
        scene.ambient = mLighting.mAmbient;
        scene.skyColor = scene.fogColor;
        scene.lightDiffuse = mLighting.mDiffuse;
        // osg::Light's specular in every mode of the OSG editor.
        scene.lightSpecular = vsg::vec4(0.f, 0.f, 0.f, 0.f);
        scene.groundcoverParams.w = static_cast<float>(mEngine->framebufferSamples());
        scene.invView = vsg::mat4(vsg::inverse_4x3(mViewMatrix->transform()));
        mSceneData->setLightPosition(sLightPosition, scene.invView);
        scene.lightViewPos.a = sLightSize;
        scene.resolution = { static_cast<float>(mExtent.width), static_cast<float>(mExtent.height) };
        mSceneData->dirty();
        mLightGrid->update();
    }

    void SceneHost::endFrame()
    {
        mEngine->endFrame();
    }
}
