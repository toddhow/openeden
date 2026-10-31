#include "scenehost.hpp"

#include <algorithm>
#include <stdexcept>

#include <vsg/app/Camera.h>
#include <vsg/app/CommandGraph.h>
#include <vsg/app/CompileManager.h>
#include <vsg/app/ProjectionMatrix.h>
#include <vsg/app/RenderGraph.h>
#include <vsg/app/View.h>
#include <vsg/app/Viewer.h>
#include <vsg/app/Window.h>
#include <vsg/state/ViewportState.h>
#include <vsg/vk/ResourceRequirements.h>

#include <vsg/ui/PointerEvent.h>
#include <vsgQt/Viewer.h>
#include <vsgQt/Window.h>

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/vsgmath.hpp>
#include <components/mwanimation/color.hpp>
#include <components/mwanimation/create.hpp>
#include <components/pipeline/sets.hpp>
#include <components/render/engine.hpp>
#include <components/render/screenshot.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/values.hpp>
#include <components/view/barriers.hpp>
#include <components/view/collectlights.hpp>
#include <components/view/defaultstate.hpp>
#include <components/view/descriptors.hpp>
#include <components/view/env.hpp>
#include <components/view/lightgrid.hpp>
#include <components/view/scene.hpp>
#include <components/vsgutil/computebin.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/setbin.hpp>
#include <components/vsgutil/sharedview.hpp>

#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/record.hpp>
#include <apps/opencs/model/world/universalid.hpp>

#include "mask.hpp"
#include "rendercontext.hpp"

namespace
{
    // The game's bin numbers (apps/openmw/mwrender/bin.hpp), as RenderContext passes to the model readers.
    constexpr int sBinDepthSorted = 0;
    constexpr int sBinCompute = 1;

    // View::CollectLights' capacity in the game (MWRender::RenderManager::RenderManager).
    constexpr size_t sMaxLights = 256;

    // Directional light of a cell with no lighting of its own: the old editor's "day" mode (lightingday.cpp) and the
    // direction the game gives an interior (RenderManager::configureAmbient).
    constexpr float sDayAmbient = 0.7f;
    const vsg::vec3 sLightPosition(-0.15f, 0.15f, 1.f);
    // The game's light size without a sky (RenderManager::updateSceneData).
    constexpr float sLightSize = 8.f;
    // The old editor's default background, as a flat colour until C3 brings back the gradient.
    const VkClearColorValue sBackground{ { 0.3f, 0.3f, 0.3f, 1.f } };
    // The default view of a cell, from outside its objects: the direction from their centre and the smallest distance.
    const vsg::dvec3 sDefaultViewDirection(-0.6, -0.6, 0.5);
    constexpr double sMinimumViewDistance = 500.0;
    constexpr double sViewDistanceScale = 2.0;

    /// The descriptor pool sizes the game asks for (RenderManager::RenderManager).
    vsg::ResourceRequirements viewResourceRequirements()
    {
        auto hints = vsg::ResourceHints::create();
        const uint32_t setsPerPool = 64;
        hints->numDescriptorSets = setsPerPool;
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

    /// As makeInversedOrderObjectOsgQuat in apps/openmw/mwworld/scene.cpp.
    vsg::quat objectRotation(const ESM::Position& position)
    {
        return vsg::quat(Misc::VsgMath::makeRotateFromAxis(position.rot[0], vsg::vec3(-1, 0, 0))
            * Misc::VsgMath::makeRotateFromAxis(position.rot[1], vsg::vec3(0, -1, 0))
            * Misc::VsgMath::makeRotateFromAxis(position.rot[2], vsg::vec3(0, 0, -1)));
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

        // Enough of MWRender::animContext for objects. Actors' files and options come with them in C4.
        mAnimContext.mask = { Mask_ParticleSystem };
        mAnimContext.nodeOptions = mResources->nodeOptions;
        mAnimContext.textureOptions = mResources->textureOptions;
        mAnimContext.animOptions = mResources->animationOptions;
        mAnimContext.compileContext = context.compileContext();

        // The view's descriptors, in the game's order but with the stand-ins of its shadow pass for shadows and the water's
        // reflections, which an editor view does not have (RenderManager::createCastShadowState).
        mSceneData = std::make_unique<View::Scene>();
        auto collectLights = vsg::ref_ptr{ new View::CollectLights(sMaxLights) };
        vsg::Descriptors descriptors = View::createLightingDescriptors(collectLights->data(), true);
        descriptors.emplace_back(View::readEnv(mResources->textureOptions));
        descriptors.emplace_back(View::dummyShadowMap());
        descriptors.emplace_back(View::dummyShadowSampler());
        descriptors.emplace_back(View::dummyReflectDepth());
        descriptors.emplace_back(mSceneData->descriptor());
        auto viewDescriptorSet = View::createViewDescriptorSet(descriptors);
        mSceneRoot = View::createDefaultState(viewDescriptorSet);

        mObjectRoot = vsg::Group::create();
        mSceneRoot->children = { mObjectRoot };

        mLightGrid = std::make_unique<View::LightGrid>(*mResources->shaderOptions, viewDescriptorSet);
        auto computeGroup = vsg::Group::create();
        computeGroup->children = { View::preComputeBarrier(), mLightGrid->node() };
        auto viewRoot = vsg::Group::create();
        viewRoot->children = { vsgUtil::createBinSetter(sBinCompute, computeGroup), mSceneRoot };

        mExtent = mEngine->extent2D();
        mProjection = vsg::Perspective::create();
        mViewMatrix = vsg::LookAt::create();
        mViewport = vsg::ViewportState::create(mExtent);
        auto camera = vsg::Camera::create(mProjection, mViewMatrix, mViewport);

        auto view = vsgUtil::createSharedView(camera, viewRoot);
        view->viewDependentState = collectLights;
        view->bins.push_back(vsg::Bin::create(sBinDepthSorted, vsg::Bin::DESCENDING));
        auto computeBin = vsgUtil::ComputeBin::create(); // VUID-vkCmdDispatch-renderpass
        computeBin->binNumber = sBinCompute;
        computeBin->commandGraph = mEngine->createCommandGraph(Pipeline::COMPUTE_SET);
        computeBin->commandGraph->submitOrder = -1;
        view->bins.push_back(computeBin);

        mRenderGraph = mEngine->createRenderGraph();
        mRenderGraph->children = { view };
        mRenderGraph->setClearValues(sBackground);

        auto commandGraph = mEngine->createCommandGraph(Pipeline::COMPUTE_SET);
        commandGraph->children = { View::postComputeBarrier(), mRenderGraph };

        // threading = false: the viewer's owner is a Qt timer on the main thread, and it decides for all of its windows.
        mEngine->setup({ commandGraph }, false);
        mEngine->compile();
        // After setup, so that the compile view gets a transfer task (research note 0003, section 5.4).
        context.compileContext()->compileManager->add(*window.windowAdapter, view, viewResourceRequirements());

        updateExtent();
        context.add(*this);
    }

    SceneHost::~SceneHost()
    {
        mContext.remove(*this);
        mContext.viewer()->deviceWaitIdle();
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

    std::size_t SceneHost::showCell(const ESM::RefId& cellId)
    {
        const auto& cell = mData.getCells().getRecord(mData.getCells().searchId(cellId)).get();
        if (cell.isExterior())
        {
            // The old editor's day mode (lightingday.cpp), as an exterior has no weather here.
            mAmbient = vsg::vec4(sDayAmbient, sDayAmbient, sDayAmbient, 1.f);
            mLightDiffuse = vsg::vec4(1.f, 1.f, 1.f, 1.f);
            mLightSpecular = vsg::vec4(0.f, 0.f, 0.f, 0.f);
        }
        else
        {
            // As RenderManager::configureAmbient, so a cell looks as it does in the game.
            mAmbient = vsg::vec4(MWAnim::rgbColor(cell.mAmbi.mAmbient), 1.f);
            if (!Settings::shaders().mClassicFalloff)
                MWAnim::brighten(mAmbient, Settings::shaders().mMinimumInteriorBrightness);
            mLightDiffuse = vsg::vec4(MWAnim::rgbColor(cell.mAmbi.mSunlight), 1.f);
            mLightSpecular = mLightDiffuse;
        }
        mLightPosition = sLightPosition;

        for (auto& child : mObjectRoot->children)
            mContext.compileContext()->detach(child);
        mObjectRoot->children.clear();
        mObjects.clear();

        const auto& references = mData.getReferences();
        const auto& referenceables = mData.getReferenceables();
        const int typeIndex = referenceables.findColumnIndex(CSMWorld::Columns::ColumnId_RecordType);
        const int modelIndex = referenceables.findColumnIndex(CSMWorld::Columns::ColumnId_Model);

        auto group = vsg::Group::create();
        const std::string cellString = cellId.getRefIdString();
        std::vector<vsg::dvec3> positions;
        std::vector<std::unique_ptr<MWAnim::Object>> objects;
        std::size_t missing = 0;
        std::size_t actors = 0;

        for (int i = 0; i < references.getSize(); ++i)
        {
            const auto& record = references.getRecord(i);
            if (record.isDeleted() || record.get().mCell.toString() != cellString)
                continue;
            const CSMWorld::CellRef& reference = record.get();

            const int index = referenceables.searchId(reference.mRefID);
            if (index == -1)
            {
                ++missing;
                continue;
            }
            const int type = referenceables.getData(index, typeIndex).toInt();
            if (type == CSMWorld::UniversalId::Type_Npc || type == CSMWorld::UniversalId::Type_Creature)
            {
                ++actors;
                continue;
            }

            std::string model = referenceables.getData(index, modelIndex).toString().toUtf8().constData();
            const ESM::Light* light = nullptr;
            // Stand-ins as CSVRender::Object::update.
            if (type == CSMWorld::UniversalId::Type_Light)
            {
                light = &dynamic_cast<const CSMWorld::Record<ESM::Light>&>(referenceables.getRecord(index)).get();
                if (model.empty())
                    model = "marker_light.nif";
            }
            else if (type == CSMWorld::UniversalId::Type_CreatureLevelledList && model.empty())
                model = "marker_creature.nif";
            if (model.empty())
                continue;

            try
            {
                auto object = MWAnim::createObject(model, false, light, mAnimContext);
                if (!object)
                    continue;
                const float* position = reference.mPos.pos;
                object->transform()->translation = { position[0], position[1], position[2] };
                const float scale = reference.mScale;
                object->transform()->scale = { scale, scale, scale };
                object->transform()->setAttitude(objectRotation(reference.mPos));
                positions.emplace_back(position[0], position[1], position[2]);
                group->addChild(object->node());
                objects.push_back(std::move(object));
            }
            catch (const std::exception& e)
            {
                ++missing;
                Log(Debug::Error) << "Can't show " << reference.mRefID.getRefIdString() << ": " << e.what();
            }
        }

        if (!mContext.compileContext()->compile(group))
        {
            Log(Debug::Error) << "Failed to compile the objects of cell " << cellString;
            return 0;
        }
        mObjectRoot->children = { group };
        mObjects = std::move(objects);

        Log(Debug::Info) << "Showing " << mObjects.size() << " objects in cell " << cellString << ", skipping " << actors
                         << " actors until stage C4, " << missing << " objects could not be shown";

        if (!mObjects.empty())
        {
            // Medians, not the bounds: many Morrowind interiors keep a few objects far outside the room.
            const vsg::dvec3 center(medianOf(positions, [](const vsg::dvec3& p) { return p.x; }),
                medianOf(positions, [](const vsg::dvec3& p) { return p.y; }),
                medianOf(positions, [](const vsg::dvec3& p) { return p.z; }));
            const double distance = std::max(
                sViewDistanceScale * medianOf(positions, [&](const vsg::dvec3& p) { return vsg::length(p - center); }),
                sMinimumViewDistance);
            setCamera(center + sDefaultViewDirection * distance, center, vsg::dvec3(0, 0, 1));
        }
        return mObjects.size();
    }

    void SceneHost::setCamera(const vsg::dvec3& eye, const vsg::dvec3& center, const vsg::dvec3& up)
    {
        mViewMatrix->eye = eye;
        mViewMatrix->center = center;
        mViewMatrix->up = up;
    }

    bool SceneHost::capture(const std::filesystem::path& file)
    {
        auto operation = mEngine->screenshotOperation();
        if (!operation)
            return false;
        // No frame may be drawing into the image: this is no hot path.
        mContext.viewer()->deviceWaitIdle();
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

        // As RenderManager::updateProjectionMatrix.
        mProjection->fieldOfViewY = Settings::camera().mFieldOfView;
        mProjection->aspectRatio = static_cast<double>(mExtent.width) / static_cast<double>(mExtent.height);
        mProjection->nearDistance = Settings::camera().mNearClip;
        mProjection->farDistance = Settings::camera().mViewingDistance;
        mSceneData->setDepthRange(Settings::camera().mNearClip, Settings::camera().mViewingDistance);
        auto& scene = mSceneData->value();
        scene.projection = vsg::mat4(mProjection->transform());
        scene.invProjection = vsg::inverse(scene.projection);
    }

    void SceneHost::update()
    {
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

        for (auto& object : mObjects)
            object->update(dt);

        // As RenderManager::updateSceneData, without sky, shadows and water. No fog: the old editor drew none.
        const float far = Settings::camera().mViewingDistance;
        mSceneData->setFogRange(far, far);
        auto& scene = mSceneData->value();
        scene.time += dt;
        scene.fogColor = vsg::vec4(sBackground.float32[0], sBackground.float32[1], sBackground.float32[2], 1.f);
        scene.ambient = mAmbient;
        scene.skyColor = scene.fogColor;
        scene.lightDiffuse = mLightDiffuse;
        scene.lightSpecular = mLightSpecular;
        scene.groundcoverParams.w = static_cast<float>(mEngine->framebufferSamples());
        scene.invView = vsg::mat4(vsg::inverse_4x3(mViewMatrix->transform()));
        mSceneData->setLightPosition(mLightPosition, scene.invView);
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
