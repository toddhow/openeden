#include "rendermanager.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

#include <vsg/app/CommandGraph.h>
#include <vsg/app/CompileManager.h>
#include <vsg/app/RenderGraph.h>
#include <vsg/core/Objects.h>
#include <vsg/io/write.h>
#include <vsg/threading/OperationThreads.h>
#include <vsg/utils/ComputeBounds.h>
#include <vsg/vk/Context.h>

#include <components/animation/skin.hpp>
#include <components/animation/transform.hpp>
#include <components/debug/debuglog.hpp>
#include <components/detournavigator/agentbounds.hpp>
#include <components/detournavigator/navigator.hpp>
#include <components/detournavigator/navmeshcacheitem.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/fx/technique.hpp>
#include <components/misc/pathhelpers.hpp>
#include <components/misc/vsgmath.hpp>
#include <components/mwanimation/color.hpp>
#include <components/mwanimation/object.hpp>
#include <components/nif/niffile.hpp>
#include <components/pipeline/builder.hpp>
#include <components/pipeline/gbufferblend.hpp>
#include <components/pipeline/sets.hpp>
#include <components/render/ambientocclusion.hpp>
#include <components/render/aomath.hpp>
#include <components/render/atmospheremath.hpp>
#include <components/render/bloom.hpp>
#include <components/render/download.hpp>
#include <components/render/engine.hpp>
#include <components/render/gbuffermath.hpp>
#include <components/render/hdrmath.hpp>
#include <components/render/hdrtarget.hpp>
#include <components/render/materialmath.hpp>
#include <components/render/materialoverrides.hpp>
#include <components/render/probedebug.hpp>
#include <components/render/probegpu.hpp>
#include <components/render/reflectiongpu.hpp>
#include <components/render/screen.hpp>
#include <components/render/skyvisgpu.hpp>
#include <components/render/sssmath.hpp>
#include <components/render/tonemap.hpp>
#include <components/render/velocity.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/values.hpp>
#include <components/terrain/paging.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/recursivedirectoryiterator.hpp>
#include <components/view/barriers.hpp>
#include <components/view/collectlights.hpp>
#include <components/view/defaultstate.hpp>
#include <components/view/descriptors.hpp>
#include <components/view/lightgrid.hpp>
#include <components/view/scene.hpp>
#include <components/vsgadapters/nif/nif.hpp>
#include <components/vsgutil/bounds.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/compileop.hpp>
#include <components/vsgutil/computebin.hpp>
#include <components/vsgutil/nullbin.hpp>
#include <components/vsgutil/projection.hpp>
#include <components/vsgutil/readnode.hpp>
#include <components/vsgutil/setbin.hpp>
#include <components/vsgutil/sharedview.hpp>
#include <components/vsgutil/suspendrenderpass.hpp>
#include <components/vsgutil/updatethreads.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwmechanics/actorutil.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "extentsync.hpp"

#include "animcontext.hpp"
#include "bin.hpp"
#include "camera.hpp"
#include "distantobjects.hpp"
#include "effects.hpp"
#include "env.hpp"
#include "fogmanager.hpp"
#include "groundcover.hpp"
#include "intersect.hpp"
#include "map.hpp"
#include "mask.hpp"
#include "navmesh.hpp"
#include "player.hpp"
#include "postprocess.hpp"
#include "precipitationoccluder.hpp"
#include "preview.hpp"
#include "projectiles.hpp"
#include "reflect.hpp"
#include "renderpass.hpp"
#include "scene.hpp"
#include "screenshotinterface.hpp"
#include "shadow.hpp"
#include "terrainstorage.hpp"
#include "water.hpp"
#include "weather.hpp"
#include "weathereffects.hpp"
#include "worldmap.hpp"
#include <components/render/debuggeometry.hpp>
#include <components/render/gputiming.hpp>
#include <components/render/simplewater.hpp>
#include <components/view/env.hpp>

namespace MWRender
{
    namespace
    {
        vsg::ref_ptr<vsg::Group> createCastShadowState()
        {
            vsg::Descriptors descriptors = View::createLightingDescriptors(vsg::vec4Array::create(2), false);
            descriptors.emplace_back(View::dummyEnvMap());
            descriptors.emplace_back(View::dummyShadowMap());
            descriptors.emplace_back(View::dummyShadowSampler());
            descriptors.emplace_back(View::dummyReflectDepth());
            descriptors.emplace_back(View::dummyProbes());
            descriptors.emplace_back(View::dummyReflections());
            descriptors.emplace_back(View::dummySkyVis());
            descriptors.emplace_back(View::dummySkyVisSky());

            View::Scene scene;
            descriptors.push_back(scene.descriptor());

            auto viewDescriptorSet = View::createViewDescriptorSet(descriptors);
            return View::createDefaultState(viewDescriptorSet);
        }

        // OpenEden (task 0025, F2): the tonemap's settings. `steppedAside`: a post-processing technique tonemaps for
        // itself (Render::Hdr::resolveTonemap).
        Render::Tonemap::Parameters tonemapParameters(bool steppedAside)
        {
            const auto& rendering = Settings::rendering();
            const auto curve = Render::Hdr::parseTonemap(rendering.mHdrTonemap.get());
            if (!curve)
                Log(Debug::Warning) << "[Rendering] hdr tonemap: unknown curve '" << rendering.mHdrTonemap.get()
                                    << "', using vanilla";
            const auto choice = Render::Hdr::resolveTonemap(
                curve.value_or(Render::Hdr::Tonemap::Vanilla), rendering.mHdrExposure, steppedAside);
            Render::Tonemap::Parameters parameters;
            parameters.curve = choice.curve;
            parameters.exposureStops = choice.exposureStops;
            parameters.encode = true; // the scene is lit in linear space (stage F2b)
            if (rendering.mHdrBloom)
                parameters.bloomStrength = rendering.mHdrBloomStrength;
            parameters.aoStrength = rendering.mAoStrength;
            parameters.sssStrength = rendering.mSssStrength;
            parameters.sssWidth = rendering.mSssWidth;
            // Task 0026: the G-buffer's debug view (only with the G-buffer on; checked where the setting is read).
            if (rendering.mGBuffer)
            {
                const auto view = Render::GBuffer::parseDebugView(rendering.mGBufferDebugView.get());
                if (!view)
                    Log(Debug::Warning) << "[Rendering] gbuffer debug view: unknown view '"
                                        << rendering.mGBufferDebugView.get() << "', showing the scene";
                parameters.debugView = view.value_or(Render::GBuffer::DebugView::None);
                if (parameters.debugView == Render::GBuffer::DebugView::Sss && !rendering.mSss)
                    Log(Debug::Warning) << "[Rendering] gbuffer debug view sss needs sss on; the view will be black";
                parameters.depthRange = Settings::camera().mViewingDistance / 8.f;
            }
            return parameters;
        }

        // OpenEden (task 0027, tier 2): reads every `openeden/materials/*.json` of the VFS into the table, in file name
        // order. A bad file is skipped whole, with one located line in the log.
        void loadMaterialFiles(const VFS::Manager& vfs, Render::Pbr::OverrideTable& table)
        {
            for (const VFS::Path::Normalized& name : vfs.getRecursiveDirectoryIterator("openeden/materials/"))
            {
                if (Misc::getFileExtension(name) != "json")
                    continue;
                std::string text;
                {
                    const auto stream = vfs.get(name);
                    std::ostringstream buffer;
                    buffer << stream->rdbuf();
                    text = buffer.str();
                }
                Render::Pbr::OverrideError error;
                const auto set = Render::Pbr::parseOverrides(text, name.value(), error);
                if (!set)
                {
                    Log(Debug::Warning) << "[Rendering] pbr: skipped material file " << error.describe();
                    continue;
                }
                table.add(*set);
                Log(Debug::Info) << "PBR: " << set->entries.size() << " material override(s) from " << name.value();
            }
        }

        // The images the tonemap's debug view can show.
        Render::Tonemap::DebugSources debugSources(
            const Render::HdrTarget& target, const vsg::ref_ptr<vsg::ImageView>& velocity)
        {
            return { target.normalView(), target.linearDepthView(), velocity, target.materialView() };
        }

        // OpenEden (task 0023, F0a): puts a GPU timestamp before and after `node` in `children`, as one named pass.
        // Does nothing without a timer. The begin node resets the pass's queries, which is not allowed inside a render
        // pass instance, so `node` must be a child of a command graph, not of a render graph.
        void timeChild(
            Render::GpuTimer* timer, vsg::Group::Children& children, const vsg::Node* node, const std::string& name)
        {
            if (!timer || !node)
                return;
            auto it = std::find_if(
                children.begin(), children.end(), [node](const auto& child) { return child.get() == node; });
            if (it == children.end())
                return;
            const std::size_t pass = timer->addPass(name);
            it = children.insert(it, timer->beginNode(pass));
            children.insert(it + 2, timer->endNode(pass));
        }
    }

    RenderManager::RenderManager(Render::Engine& engine, vsg::ref_ptr<vsg::Node> gui,
        Resource::ResourceSystem* resourceSystem /*, DetourNavigator::Navigator& navigator*/)
        : mEngine(engine)
        , mResourceSystem(resourceSystem)
        //, mNavigator(navigator)
        , mFog(new FogManager)

        , mMinimumAmbientLuminance(Settings::shaders().mMinimumInteriorBrightness)
        , mNearClip(Settings::camera().mNearClip)
        , mViewDistance(Settings::camera().mViewingDistance)
        , mFieldOfView(Settings::camera().mFieldOfView)
    {
        vsg::ref_ptr<vsg::Context> context = vsg::Context::create(mEngine.getOrCreateDevice());
        mEngine.setRenderPass(
            createMainRenderPass(context->device, mEngine.framebufferSamples(), mEngine.getPresentLayout()));

        const int numLoadingThreads = 1;
        // if (compileManager->numCompileTraversals > 1)
        //     numLoadingThreads = std::min(nunLoadingThreads, 1);
        mOperationThreads = vsg::OperationThreads::create(numLoadingThreads);

        // OpenEden (task 0023, F0a): GPU timing. Null, and nothing below does anything, unless the setting is on.
        mGpuTimer = Settings::rendering().mGpuTiming ? mEngine.enableGpuTiming() : nullptr;
        // Registered now and not when first used: the frame stats file's header lists the passes when the loop starts.
        if (mGpuTimer)
            mReflectPass = mGpuTimer->addPass("reflection and depth copy");

        mRenderModes.resize(Render_Count);
        // As OpenMW's RenderingManager (task 0012).
        Nif::Reader::setLoadUnsupportedFiles(Settings::models().mLoadUnsupportedNifFiles);
        mCamera.reset(new Camera);

        mShadow = std::make_unique<Shadow>(*context, mEngine.getEnabledFeatures().depthClamp);
        // OpenEden (task 0025, F2): the linear HDR path. The scene then draws into a float target (made here, because
        // the water's copy of the scene is made in its format) and a pass on the window turns it into the swapchain
        // picture.
        const bool wantHdr = Settings::rendering().mHdr;
        const bool hdr = Render::Hdr::useHdrPath(wantHdr, mEngine.hasWindow());
        if (wantHdr && !hdr)
            Log(Debug::Warning) << "[Rendering] hdr is on but this engine has no window to draw to; HDR is off";
        if (Settings::rendering().mPbr && !hdr)
            Log(Debug::Warning) << "[Rendering] pbr needs hdr (it lights in linear space); the material model is off";
        if (Settings::rendering().mGBuffer && !hdr)
            Log(Debug::Warning) << "[Rendering] gbuffer needs hdr (the HDR path owns the render pass that holds its "
                                   "outputs); the G-buffer is off";
        if (hdr)
        {
            mHdrContext = context;
            // OpenEden (task 0026, F3): the G-buffer is extra colour outputs of this target's render pass, which must
            // exist before any pipeline of the scene is compiled.
            const bool gbuffer = Render::GBuffer::useGBuffer(Settings::rendering().mGBuffer, true);
            // OpenEden (task 0027, F4): the material model; its values are stored only with the G-buffer.
            const bool pbr = Render::Pbr::usePbr(Settings::rendering().mPbr, true);
            const bool materialOutput = Render::Pbr::useMaterialOutput(pbr, gbuffer);
            // OpenEden (task 0029, R2): ambient occlusion also stores the ambient part of the colour.
            const bool wantAo = Settings::rendering().mAo;
            const bool ao = Render::Ao::useAo(wantAo, gbuffer, materialOutput);
            if (wantAo && !ao)
                Log(Debug::Warning)
                    << "[Rendering] ao needs hdr, gbuffer and pbr (it reads the stored depth, normals and "
                       "the material output); ambient occlusion is off";
            // OpenEden (task 0033, R7): subsurface scattering reads the skin mark of the material output.
            const bool wantSss = Settings::rendering().mSss;
            const bool sss = Render::Sss::useSss(wantSss, gbuffer, materialOutput);
            if (wantSss && !sss)
                Log(Debug::Warning) << "[Rendering] sss needs hdr, gbuffer and pbr (it reads the skin mark of the "
                                       "material output); subsurface scattering is off";
            mSssEnabled = sss;
            if (sss)
                Log(Debug::Info) << "SSS: subsurface scattering of skin in the tonemap";
            const auto gbufferFormats
                = gbuffer ? Render::GBuffer::extraColorFormats(true, materialOutput, ao) : std::vector<VkFormat>{};
            mHdrTarget = std::make_unique<Render::HdrTarget>(
                *context, mEngine.framebufferSamples(), mEngine.extent2D(), gbufferFormats);
            mGBufferOutputs = static_cast<unsigned int>(gbufferFormats.size());
            mAoEnabled = ao;
            if (ao)
                Log(Debug::Info) << "AO: ambient occlusion at half resolution";
            if (gbuffer)
            {
                resourceSystem->builder->graphics->gbufferOutputs = mGBufferOutputs;
                // Skinned models keep the previous frame's bone matrices (stage c); before any is loaded.
                Anim::Skin::keepPreviousMatrices = true;
                Log(Debug::Info) << "G-buffer: normals and linear depth are stored beside the scene";
            }
            if (pbr)
            {
                const auto look = Render::Pbr::parseLook(Settings::rendering().mPbrLook.get());
                if (!look)
                    Log(Debug::Warning) << "[Rendering] pbr look: unknown look '"
                                        << Settings::rendering().mPbrLook.get() << "', using vanilla";
                resourceSystem->builder->graphics->pbrLook
                    = look.value_or(Render::Pbr::Look::Vanilla) == Render::Pbr::Look::Physical ? 2 : 1;
                // The overrides and skin marks; the loaders look them up (tier 2, task 0027). Before any model loads.
                resourceSystem->materialTable = std::make_shared<Render::Pbr::OverrideTable>();
                resourceSystem->nifReader->materialTable = resourceSystem->materialTable;
                resourceSystem->nifReader->autoMaps.autoUseMaterialMaps = Settings::rendering().mPbrMaterialMaps;
                loadMaterialFiles(*resourceSystem->getVFS(), *resourceSystem->materialTable);
                Log(Debug::Info) << "PBR: the material model is on"
                                 << (materialOutput ? ", stored in the G-buffer" : "");
            }
            // Task 0028: baked light probes; the lighting needs the material model's ambient scale, so pbr too.
            if (Settings::rendering().mGi)
            {
                if (pbr)
                {
                    resourceSystem->builder->graphics->giEnabled = true;
                    mGiEnabled = true;
                    mProbeStrength = Settings::rendering().mGiStrength;
                    mReflectionStrength = Settings::rendering().mGiReflectionStrength;
                    Log(Debug::Info) << "GI: ambient light from baked light probes";
                }
                else
                    Log(Debug::Warning) << "[Rendering] gi needs hdr and pbr; baked light probes are off";
            }
            // Task 0036: the exterior sky-visibility field. Its sky colours come from the atmosphere model, so that
            // too.
            if (Settings::rendering().mGiExterior)
            {
                if (pbr && Render::Atmosphere::useAtmosphere(Settings::rendering().mAtmosphere, true))
                {
                    resourceSystem->builder->graphics->giSkyEnabled = true;
                    mGiSkyEnabled = true;
                    mSkyVisStrength = Settings::rendering().mGiExteriorStrength;
                    Log(Debug::Info) << "GI: exterior ambient light from the baked sky-visibility field";
                }
                else
                    Log(Debug::Warning) << "[Rendering] gi exterior needs hdr, pbr and atmosphere; it is off";
            }
            Log(Debug::Info) << "HDR: rendering the scene in " << static_cast<int>(mHdrTarget->samples())
                             << " sample(s) to a 16-bit float target";
            mReflect = std::make_unique<Reflect>(
                mEngine, Render::HdrTarget::colorFormat, Render::HdrTarget::colorFinalLayout);
        }
        else
            mReflect = std::make_unique<Reflect>(mEngine);
        mScreenshotInterface = std::make_unique<ScreenshotInterface>();

        mSceneData = std::make_unique<View::Scene>();
        auto collectLights = vsg::ref_ptr{ new View::CollectLights(256) };
        collectLights->setLinear(mHdrTarget != nullptr);
        vsg::Descriptors descriptors = View::createLightingDescriptors(collectLights->data(), true);
        descriptors.emplace_back(View::readEnv(resourceSystem->textureOptions));
        if (auto shadowMap = mShadow->shadowMap())
        {
            descriptors.emplace_back(shadowMap);
            descriptors.emplace_back(mShadow->shadow->shadowSampler());
        }
        else
        {
            descriptors.emplace_back(View::dummyShadowMap());
            descriptors.emplace_back(View::dummyShadowSampler());
        }
        descriptors.emplace_back(mReflect->reflectColor());
        descriptors.emplace_back(mReflect->reflectDepth());
        // Task 0028: the light probe buffer when gi is on and the pipelines use it, else a stand-in without a volume.
        if (mGiEnabled)
            descriptors.emplace_back(View::probeBuffer(mProbeData));
        else
            descriptors.emplace_back(View::dummyProbes());
        // Task 0028, stage d: the same for the reflection probes.
        if (mGiEnabled)
            descriptors.emplace_back(View::reflectionBuffer(mReflectionData));
        else
            descriptors.emplace_back(View::dummyReflections());
        // Task 0036: the exterior sky-visibility field (cells' data, then its header), or stand-ins that switch it off.
        if (mGiSkyEnabled)
        {
            descriptors.emplace_back(View::skyVisBuffer(mSkyVisPool));
            descriptors.emplace_back(View::skyVisSkyBuffer(mSkyVisHeader));
        }
        else
        {
            descriptors.emplace_back(View::dummySkyVis());
            descriptors.emplace_back(View::dummySkyVisSky());
        }
        if (Settings::shaders().mWeatherParticleOcclusion)
        {
            mOccluder = std::make_unique<PrecipitationOccluder>(*context);
            mResourceSystem->setOccludedParticleMap(mOccluder->mapDescriptor());
        }
        descriptors.emplace_back(mSceneData->descriptor());

        auto viewDescriptorSet = View::createViewDescriptorSet(descriptors);
        mSceneRoot = View::createDefaultState(viewDescriptorSet);

        mPlayerGroup = vsg::Switch::create();

        mProjectionMatrix = vsg::Perspective::create();
        mViewMatrix = vsg::LookAt::create();
        mSceneCamera
            = vsg::Camera::create(mProjectionMatrix, mViewMatrix, vsg::ViewportState::create(mEngine.extent2D()));

        auto resourceHints = vsg::ResourceHints::create();
        uint32_t setsPerPool = 64;
        resourceHints->numDescriptorSets = setsPerPool;
        resourceHints->descriptorPoolSizes = {
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }, { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1 },
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1 },
            //, builder->graphics->descriptorSetLayout {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * setsPerPool },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 * setsPerPool },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 7 * setsPerPool }
            // }
        };
        vsg::ResourceRequirements resourceRequirements(resourceHints);
        mCompileContext = mEngine.createCompileContext();

        mScene = std::make_unique<Scene>(Settings::cells().mAnimationThreads);
        mScene->context = animContext(resourceSystem, mCompileContext);
        mScene->noParticlesContext = mScene->context;
        mScene->noParticlesContext.mask.particle = vsg::MASK_OFF;

        mEffects = std::make_unique<Effects>(mScene->context);
        mWeatherEffects = std::make_unique<WeatherEffects>(mScene->context);
        mRain = std::make_unique<Rain>(*mResourceSystem->builder, mCompileContext, mResourceSystem->textureOptions);
        if (mOccluder)
            mRain->setOcclusionMap(mOccluder->mapDescriptor());
        mWaterRipples = std::make_unique<WaterRipples>(
            *mResourceSystem->builder, mCompileContext, mResourceSystem->textureOptions);
        mProjectiles = std::make_unique<Projectiles>(mScene->context);
        // Jolt's debug draw: lines only, refilled every frame; enough for Balmora's colliders.
        constexpr std::size_t joltLineVertices = std::size_t(1) << 21;
        mDebugGeometry
            = std::make_unique<Render::DebugGeometry>(*mResourceSystem->builder, mCompileContext, joltLineVertices, 0);
        // The navmesh debug draw: tile boundaries as lines, polygons as translucent triangles.
        constexpr std::size_t navMeshLineVertices = std::size_t(1) << 20;
        constexpr std::size_t navMeshTriangleVertices = std::size_t(1) << 21;
        mNavMeshGeometry = std::make_unique<Render::DebugGeometry>(
            *mResourceSystem->builder, mCompileContext, navMeshLineVertices, navMeshTriangleVertices);
        mNavMesh = std::make_unique<NavMesh>(
            *mNavMeshGeometry, Settings::navigator().mEnableNavMeshRender, Settings::navigator().mNavMeshRenderMode);
        if (mGiEnabled && Settings::rendering().mGiDebug)
        {
            // The probe debug view (`gi debug`): one octahedron per light probe and per reflection probe.
            constexpr std::size_t probeTriangleVertices
                = Render::Gi::debugMarkerVertices * (Render::Gi::maxVolumeProbes + Render::Gi::maxReflectionProbes);
            mProbeGeometry = std::make_unique<Render::DebugGeometry>(
                *mResourceSystem->builder, mCompileContext, 0, probeTriangleVertices);
        }
        mSky = std::make_unique<Sky>(
            *resourceSystem, mCompileContext->clone(Mask_Sky), mEngine.getEnabledFeatures().occlusionQueryPrecise);
        mWater = std::make_unique<Water>(resourceSystem);
        mSimpleWater = std::make_unique<Render::SimpleWater>(
            Constants::CellSizeInUnits, *mResourceSystem->builder, mResourceSystem->textureOptions);

        mTerrainStorage = std::make_unique<TerrainStorage>(mResourceSystem);
        // openeden-vsg: terrain normal and specular maps, off by default (task 0012, V4i).
        Terrain::TerrainMapSettings terrainMaps;
        terrainMaps.gbufferOutputs = mGBufferOutputs;
        terrainMaps.giSky = mGiSkyEnabled;
        const auto& shaders = Settings::shaders();
        terrainMaps.autoMaps.autoUseNormalMaps = shaders.mAutoUseTerrainNormalMaps;
        terrainMaps.autoMaps.autoUseSpecularMaps = shaders.mAutoUseTerrainSpecularMaps;
        terrainMaps.autoMaps.normalMapPattern = shaders.mNormalMapPattern;
        terrainMaps.autoMaps.normalHeightMapPattern = shaders.mNormalHeightMapPattern;
        terrainMaps.autoMaps.specularMapPattern = shaders.mTerrainSpecularMapPattern;
        const VFS::Manager* vfs = mResourceSystem->getVFS();
        terrainMaps.exists = [vfs](std::string_view path) { return vfs->exists(VFS::Path::Normalized(path)); };
        mTerrain
            = std::make_unique<Terrain::Paging>(mCompileContext->clone(Mask_Terrain), mResourceSystem->textureOptions,
                mResourceSystem->shaderOptions, mResourceSystem->builder->createSampler(), std::move(terrainMaps));
        mTerrain->lodFactor = Settings::terrain().mLodFactor;
        mTerrain->vertexLodMod = Settings::terrain().mVertexLodMod;

        mTerrainView = mTerrain->createView();
        mPreview = std::make_unique<Preview>(*context, mEngine.framebufferSamples(), mScene->context, mResourceSystem);
        mWorldMap = std::make_unique<WorldMap>(mCompileContext, mResourceSystem->shaderOptions, mOperationThreads);
        mMap = std::make_unique<Map>(*context, mEngine.framebufferSamples(), mCompileContext, createMapScene(),
            mTerrain.get(), mViewDistance, Settings::map().mLocalMapResolution, mResourceSystem->shaderOptions);
        auto guiRenderTextures = vsg::Group::create();
        guiRenderTextures->children.emplace_back(mPreview->node());
        guiRenderTextures->children.emplace_back(mMap->getNode());
        guiRenderTextures->children.emplace_back(mMap->getFogNode());
        guiRenderTextures->children.emplace_back(mWorldMap->node());

        mLightGrid = std::make_unique<View::LightGrid>(*resourceSystem->shaderOptions, viewDescriptorSet);
        auto computeGroup = vsg::Group::create();
        computeGroup->children = { View::preComputeBarrier(), mLightGrid->node(), mSky->resetNode() };
        auto computeBinContents = vsgUtil::createBinSetter(Bin_Compute, computeGroup);
        auto viewRoot = vsg::Group::create();
        viewRoot->children = { computeBinContents, mSceneRoot };
        mView = vsgUtil::createSharedView(mSceneCamera, viewRoot);
        mView->viewDependentState = collectLights;
        mView->mask = ~Mask_GUI;
        mView->bins.push_back(vsg::Bin::create(Bin_DepthSorted, vsg::Bin::DESCENDING));

        mScreenshotSwitch = vsg::Switch::create();

        if (mHdrTarget)
        {
            // The scene draws into the float target. The render pass is the target's, so the scene's pipelines are
            // compiled for it (the compile view is added on the framebuffer, below).
            mRenderGraph = vsg::RenderGraph::create();
            mRenderGraph->framebuffer = mHdrTarget->framebuffer();
            mRenderGraph->renderArea = { { 0, 0 }, mHdrTarget->extent() };
            mRenderGraph->windowResizeHandler = nullptr;
            mRenderGraph->setClearValues(VkClearColorValue{ { 0.f, 0.f, 0.f, 1.f } });
            mTonemapGraph = mEngine.createRenderGraph();
            mTonemap = std::make_unique<Render::Tonemap>(mCompileContext->clone(~vsg::Mask(0)), mTonemapGraph);
            if (Settings::rendering().mHdrBloom)
                mBloom = std::make_unique<Render::Bloom>(*context, mCompileContext->clone(~vsg::Mask(0)));
            // Task 0026, stage b: screen motion from the stored depth, with the G-buffer.
            if (mGBufferOutputs > 0)
                mVelocity = std::make_unique<Render::Velocity>(*context, mCompileContext->clone(~vsg::Mask(0)));
            // Task 0029: the occlusion pass, after the scene (it reads the scene's depth and normals).
            if (mAoEnabled)
            {
                Render::AmbientOcclusion::Settings aoSettings;
                aoSettings.radius = Settings::rendering().mAoRadius;
                aoSettings.slices = Settings::rendering().mAoSlices;
                aoSettings.steps = Settings::rendering().mAoSteps;
                mAmbientOcclusion = std::make_unique<Render::AmbientOcclusion>(
                    *context, mCompileContext->clone(~vsg::Mask(0)), aoSettings);
            }
        }
        else
            mRenderGraph = mEngine.createRenderGraph();
        // The graph that has the window: the one the GUI pass and the extents follow.
        const vsg::ref_ptr<vsg::RenderGraph> windowGraph = mTonemapGraph ? mTonemapGraph : mRenderGraph;
        mRenderGraph->children = { mView };
        mSuspend = vsg::ref_ptr{ new vsgUtil::SuspendRenderPass(mRenderGraph) };

        mGuiRenderGraph = vsg::RenderGraph::create();
        // Like Engine::createRenderGraph: syncExtents sets the render area. vsg::RenderGraph's own resize handler would
        // scale it again by new/old when the swapchain changes, to new*new/old (task 0012, resize fix).
        mGuiRenderGraph->windowResizeHandler = nullptr;
        mGuiRenderGraph->framebuffer = windowGraph->framebuffer;
        mGuiRenderGraph->window = windowGraph->window;
        mGuiRenderGraph->renderPass
            = createGuiRenderPass(context->device, mEngine.framebufferSamples(), mEngine.getPresentLayout());
        mGuiRenderGraph->renderArea = windowGraph->renderArea;
        mAppliedExtent = windowGraph->renderArea.extent;
        mGuiRenderGraph->children = { gui };

        vsg::CommandGraphs threads;

        auto guiCommandGraph = mEngine.createCommandGraph(Pipeline::COMPUTE_SET);
        guiCommandGraph->children = { guiRenderTextures, mGuiRenderGraph };
        timeChild(mGpuTimer, guiCommandGraph->children, guiRenderTextures.get(), "gui render textures");
        timeChild(mGpuTimer, guiCommandGraph->children, mGuiRenderGraph.get(), "gui");
        guiCommandGraph->submitOrder = 1;
        threads.push_back(guiCommandGraph);

        auto computeBin = vsgUtil::ComputeBin::create(); // VUID-vkCmdDispatch-renderpass
        computeBin->binNumber = Bin_Compute;
        computeBin->commandGraph = mEngine.createCommandGraph(Pipeline::COMPUTE_SET);
        computeBin->commandGraph->submitOrder = -1;
        if (mGpuTimer)
        {
            const std::size_t pass = mGpuTimer->addPass("compute");
            computeBin->timingBegin = mGpuTimer->beginNode(pass);
            computeBin->timingEnd = mGpuTimer->endNode(pass);
        }
        mView->bins.push_back(computeBin);

        // openeden-vsg: in VSG 1.1.x a compile context uploads vertex, index and image data only through a
        // vsg::TransferTask, taken from device->transferTask when the view is added. That task is created by
        // mEngine.setup() (Viewer::assignRecordAndSubmitTaskAndPresentation), so the views are added after it;
        // added earlier, compiled scene objects got no transfer task and their data was dropped
        // ("Context::record() ... Need to implement copying of data").
        std::vector<std::function<void()>> addCompileViews;
        // The scene view is compiled on the window, or, with the float target, on that target's framebuffer.
        if (mRenderGraph->window)
            addCompileViews.emplace_back([this, resourceRequirements] {
                mCompileContext->compileManager->add(*mRenderGraph->window, mView, resourceRequirements);
            });
        else
            addCompileViews.emplace_back([this, resourceRequirements] {
                mCompileContext->compileManager->add(*mRenderGraph->framebuffer, mView, resourceRequirements);
            });

        mCommandGraph = mEngine.createCommandGraph(Pipeline::COMPUTE_SET);
        // OpenEden: post-processing runs after the scene, on the finished image, when the setting is on (task 0012,
        // V4p).
        if (Settings::postProcessing().mEnabled)
            mPostProcess = std::make_unique<PostProcess>(mEngine, context, mCompileContext, *mResourceSystem->getVFS(),
                mResourceSystem->textureOptions, mRenderGraph->renderArea.extent.width,
                mRenderGraph->renderArea.extent.height);
        mCommandGraph->children = { View::postComputeBarrier(), mRenderGraph, mScreenshotSwitch };
        // The first index after the scene (and its velocity and occlusion passes).
        const int afterScene = 2 + (mVelocity ? 1 : 0) + (mAmbientOcclusion ? 1 : 0);
        if (mTonemapGraph)
        {
            // Scene, then bloom, then the tonemap into the swapchain, then everything that works on the 8-bit picture.
            // The velocity pass follows the scene: it reads the scene's linear depth.
            if (mVelocity)
            {
                mCommandGraph->children.insert(mCommandGraph->children.begin() + 2, mVelocity->graph());
                addCompileViews.emplace_back([this, context, resourceRequirements] {
                    mCompileContext->compileManager->add(
                        *mVelocity->registrationFramebuffer(*context), mVelocity->view(), resourceRequirements);
                });
            }
            if (mAmbientOcclusion)
            {
                mCommandGraph->children.insert(
                    mCommandGraph->children.begin() + 2 + (mVelocity ? 1 : 0), mAmbientOcclusion->graph());
                addCompileViews.emplace_back([this, context, resourceRequirements] {
                    mCompileContext->compileManager->add(*mAmbientOcclusion->registrationFramebuffer(*context),
                        mAmbientOcclusion->view(), resourceRequirements);
                });
            }
            if (mBloom)
            {
                mCommandGraph->children.insert(mCommandGraph->children.begin() + afterScene, mBloom->node());
                addCompileViews.emplace_back([this, context, resourceRequirements] {
                    mCompileContext->compileManager->add(
                        *mBloom->registrationFramebuffer(*context), mBloom->view(), resourceRequirements);
                });
            }
            mCommandGraph->children.insert(
                mCommandGraph->children.begin() + afterScene + (mBloom ? 1 : 0), mTonemapGraph);
            addCompileViews.emplace_back([this, resourceRequirements] {
                mCompileContext->compileManager->add(*mTonemapGraph->window, mTonemap->view(), resourceRequirements);
            });
        }
        if (mPostProcess)
        {
            // The GUI pass's render pass is the window's, so the present pipeline is compiled for the tonemap's view
            // when the scene has a render pass of its own.
            mPostProcess->setMainView(mTonemap ? mTonemap->view() : mView);
            mGuiRenderGraph->children.insert(mGuiRenderGraph->children.begin(), mPostProcess->presentNode());
            mCommandGraph->children.insert(
                mCommandGraph->children.begin() + (mTonemapGraph ? (mBloom ? 1 : 0) + afterScene + 1 : 2),
                mPostProcess->node());
        }
        timeChild(mGpuTimer, mCommandGraph->children, mRenderGraph.get(), "main scene");
        if (mVelocity)
            timeChild(mGpuTimer, mCommandGraph->children, mVelocity->graph().get(), "velocity");
        if (mAmbientOcclusion)
            timeChild(mGpuTimer, mCommandGraph->children, mAmbientOcclusion->graph().get(), "ao");
        if (mBloom)
            timeChild(mGpuTimer, mCommandGraph->children, mBloom->node().get(), "bloom");
        if (mTonemapGraph)
            timeChild(mGpuTimer, mCommandGraph->children, mTonemapGraph.get(), "tonemap");
        if (mPostProcess)
            timeChild(mGpuTimer, mCommandGraph->children, mPostProcess->node().get(), "post-process");
        threads.push_back(mCommandGraph);

        for (int i = 0; mShadow->shadow && i < mShadow->shadow->numCascades; ++i)
        {
            auto rg = mShadow->shadow->renderGraph(i);
            auto castShadowGraph = mEngine.createCommandGraph(Pipeline::COMPUTE_SET);
            castShadowGraph->submitOrder = -1;
            auto view = mShadow->shadow->cascadeView(i);
            // Note, createCastShadowScene(..) will omit root nodes that are not required so the record traversal won't
            // have to traverse them. The View's mask is set to allow the vsg::CompileManager::compile(..)'s
            // ContextSelectionFunction to skip shadow compile contexts when models/subgraphs are added to the scene
            // that don't require shadow casting.
            view->mask = mShadow->viewMask();
            view->bins
                = { vsg::Bin::create(Bin_DepthSorted, vsg::Bin::NO_SORT), vsgUtil::NullBin::create(Bin_Compute) };
            auto root = createCastShadowState();
            root->children = { view };
            rg->children = { root };
            castShadowGraph->children = { rg };
            timeChild(mGpuTimer, castShadowGraph->children, rg.get(), "shadow cascade " + std::to_string(i));
            threads.push_back(castShadowGraph);
            mRecordShadow.push_back(castShadowGraph->recordTraversal);
            if (i == 0)
                addCompileViews.emplace_back(
                    [this, rg, view] { mCompileContext->compileManager->add(*rg->framebuffer, view); });
        }

        if (mOccluder)
        {
            auto rg = mOccluder->renderGraph();
            auto occlusionGraph = mEngine.createCommandGraph(Pipeline::COMPUTE_SET);
            occlusionGraph->submitOrder = -1;
            auto view = mOccluder->view();
            view->mask = Mask_Object; // OpenMW's occluder does not draw terrain.
            view->bins
                = { vsg::Bin::create(Bin_DepthSorted, vsg::Bin::NO_SORT), vsgUtil::NullBin::create(Bin_Compute) };
            auto root = createCastShadowState();
            root->children = { view };
            rg->children = { root };
            occlusionGraph->children = { rg };
            timeChild(mGpuTimer, occlusionGraph->children, rg.get(), "sun occlusion");
            threads.push_back(occlusionGraph);
            mRecordShadow.push_back(occlusionGraph->recordTraversal);
            addCompileViews.emplace_back(
                [this, rg, view] { mCompileContext->compileManager->add(*rg->framebuffer, view); });
        }

        mRecord = mCommandGraph->recordTraversal;

        mEngine.setup(threads, getenv("VSGOPENMW_NO_VIEWER_THREADING") == 0);
        if (mPostProcess)
        {
            mPostProcess->compileContext()->compileManager->add(
                *mPostProcess->registrationFramebuffer(), mPostProcess->baseView());
        }
        for (const auto& addCompileView : addCompileViews)
            addCompileView();
        if (mTonemap)
        {
            mTonemap->setParameters(tonemapParameters(false));
            if (mVelocity
                && !mVelocity->setSource(*mHdrContext, mHdrTarget->linearDepthView(), mHdrTarget->objectMotionView(),
                    mHdrTarget->extent(),
                    [this](vsg::ref_ptr<vsg::Object> retired) { mCompileContext->detach(retired); }))
                throw std::runtime_error("The velocity pass could not be built");
            if (mAmbientOcclusion
                && !mAmbientOcclusion->setSource(*mHdrContext, mHdrTarget->linearDepthView(), mHdrTarget->normalView(),
                    mHdrTarget->extent(),
                    [this](vsg::ref_ptr<vsg::Object> retired) { mCompileContext->detach(retired); }))
                throw std::runtime_error("The ambient occlusion pass could not be built");
            if (mBloom
                && !mBloom->setSource(*mHdrContext, mHdrTarget->colorView(), mHdrTarget->extent(),
                    [this](vsg::ref_ptr<vsg::Object> retired) { mCompileContext->detach(retired); }))
                throw std::runtime_error("The bloom passes could not be built");
            if (!mTonemap->setSource(mHdrTarget->colorView(), mBloom ? mBloom->result() : nullptr, mHdrTarget->extent(),
                    debugSources(*mHdrTarget, mVelocity ? mVelocity->result() : nullptr), aoSources(), sssSources()))
                throw std::runtime_error("The tonemap pass could not be built");
        }
        if (mPostProcess)
            mPostProcess->setChain(Settings::postProcessing().mChain.get());

        updateProjectionMatrix();

        /*
         * Asynchronously compiles pipeline-heavy objects.
         */
        // updateSceneChildren();
        auto objectsToCompile = vsg::Objects::create();
        objectsToCompile->children = { mWater->node(), mSky->node() };
        mCompileOp = new vsgUtil::CompileOp(objectsToCompile, mCompileContext->clone(Mask_Water | Mask_Sky));
        mOperationThreads->add(mCompileOp);
        createIntersectorScene();
        setupScreenshotInterface();
    }

    RenderManager::~RenderManager()
    {
        if (mCompileOp)
            mCompileOp->wait();
    }

    void RenderManager::setStore(const MWWorld::ESMStore& store)
    {
        mTerrainStorage->setStore(&store);
        mTerrain->setStorage(mTerrainStorage.get());
    }

    RenderingInterface& RenderManager::getObjects()
    {
        return *mScene;
    }

    vsg::ref_ptr<vsg::Node> RenderManager::createMapScene()
    {
        auto group = vsg::Group::create();
        group->children = { mScene->getObjects(), mSimpleWater->node() };
        return group;
    }

    vsg::Group::Children RenderManager::createCastShadowScene(bool exterior)
    {
        vsg::Group::Children children;
        if (!exterior && !Settings::shadows().mEnableIndoorShadows)
            return children;

        if (Settings::shadows().mTerrainShadows)
            children.push_back(mTerrainView->node());
        if (exterior && Settings::shadows().mObjectShadows)
            children.push_back(mScene->getObjects());
        if (Settings::shadows().mActorShadows)
            children.push_back(mScene->getActors());
        if (Settings::shadows().mPlayerShadows)
            children.push_back(mPlayerGroup);
        return children;
    }

    void RenderManager::ensureCompiled()
    {
        if (mCompileOp)
        {
            mCompileOp->ensure();
            mCompileOp = {};
        }
    }

    void RenderManager::updateSceneChildren()
    {
        ensureCompiled();
        mSceneRoot->children.clear();
        if (mRenderModes[Render_Sky])
            mSceneRoot->addChild(mSky->node());
        if (!mRenderModes[Render_Scene])
            return;
        if (mRenderModes[Render_Terrain])
            mSceneRoot->addChild(mTerrainView->node());
        mSceneRoot->addChild(mPlayerGroup);
        mSceneRoot->addChild(mScene->getObjects());
        if (mGroundcover)
            mSceneRoot->addChild(mGroundcover->node());
        if (mDistantObjects)
            mSceneRoot->addChild(mDistantObjects->node());
        mSceneRoot->addChild(mScene->getActors());
        // The opaque scene's colour and depth are copied here, before the water and the depth-sorted bins: the water
        // reflects and refracts with them, and soft particles fade against the depth (task 0012, V4k).
        if (mRenderModes[Render_Water] || Settings::shaders().mSoftParticles || mPostProcess)
        {
            mSuspend->children = { mReflect->node() };
            if (mPostProcess)
                mSuspend->addChild(mPostProcess->depthCopy());
            // OpenEden (task 0023): time the copies and the reflection, outside the suspended render pass.
            if (mGpuTimer)
            {
                mSuspend->children.insert(mSuspend->children.begin(), mGpuTimer->beginNode(mReflectPass));
                mSuspend->addChild(mGpuTimer->endNode(mReflectPass));
            }
            mSceneRoot->addChild(mSuspend);
        }
        if (mRenderModes[Render_Water])
        {
            mSceneRoot->addChild(mWater->node());
            mSceneRoot->addChild(mWaterRipples->node());
        }
        mSceneRoot->addChild(mProjectiles->node());
        mSceneRoot->addChild(mEffects->node());
        if (mRenderModes[Render_Sky])
            mSceneRoot->addChild(mWeatherEffects->node());
        if (mRenderModes[Render_Sky])
            mSceneRoot->addChild(mRain->node());
        mSceneRoot->addChild(mDebugGeometry->node());
        mSceneRoot->addChild(mNavMeshGeometry->node());
        if (mProbeGeometry)
            mSceneRoot->addChild(mProbeGeometry->node());
    }

    void RenderManager::createIntersectorScene()
    {
        mIntersectorScene = vsg::Group::create();
        mIntersectorScene->children = { mTerrainView->node(), mPlayerGroup, mScene->getObjects(), mScene->getActors() };
    }

    void RenderManager::setViewMode(ViewMode mode, bool enable)
    {
        vsg::Mask mask = vsg::MASK_OFF;
        if (mode == ViewMode::Scene)
            mask = ~Mask_GUI;

        auto& traversalMask = mRecord->traversalMask;
        if (enable)
            traversalMask |= mask;
        else
            traversalMask &= ~mask;
        for (auto& traversal : mRecordShadow)
            traversal->traversalMask = enable ? mask : vsg::MASK_OFF;
        if (mPostProcess && mode == ViewMode::Scene)
            mPostProcess->setSceneVisible(enable);
    }

    bool RenderManager::toggleRenderMode(RenderMode mode)
    {
        if (mode == Render_NavMesh)
            return mNavMesh->toggle();
        bool enabled = mRenderModes[mode].toggled = !mRenderModes[mode].toggled;
        updateSceneChildren();
        return enabled;
    }

    void RenderManager::setRenderModeActive(RenderMode mode, bool active)
    {
        if (mRenderModes[mode].active != active || mSceneRoot->children.empty())
        {
            mRenderModes[mode].active = active;
            updateSceneChildren();
        }

        if (!active && mode == Render_Terrain)
            mTerrain->clearView(*mTerrainView);
    }

    Resource::ResourceSystem* RenderManager::getResourceSystem()
    {
        return mResourceSystem;
    }

    const MWAnim::Context& RenderManager::getAnimContext()
    {
        return mScene->context;
    }

    void RenderManager::setNightEyeFactor(float factor)
    {
        mNightEyeFactor = factor;
    }

    void RenderManager::setAmbientColour(const vsg::vec4& colour)
    {
        mAmbientColor = colour;
    }

    void RenderManager::skySetDate(int day, int month)
    {
        mSky->setDate(day, month);
    }

    int RenderManager::skyGetMasserPhase() const
    {
        return mSky->getMasserPhase();
    }

    int RenderManager::skyGetSecundaPhase() const
    {
        return mSky->getSecundaPhase();
    }

    void RenderManager::skySetMoonColour(bool red)
    {
        mSky->setMoonColour(red);
    }

    vsg::vec3 RenderManager::getLinearAmbient() const
    {
        return Render::Hdr::toLinear(vsg::vec3(mAmbientColor.x, mAmbientColor.y, mAmbientColor.z));
    }

    void RenderManager::disableProbes()
    {
        if (!mProbeData)
            return;
        // The header's "enabled" entry; the shader then returns the flat ambient.
        (*mProbeData)[1].w = 0.f;
        mProbeData->dirty();
        disableReflections();
        mLoadedVolume.reset();
        // Also called alone for exteriors, so the markers must be cleared here too.
        updateProbeDebug();
    }

    void RenderManager::updateProbeDebug()
    {
        if (!mProbeGeometry)
            return;
        // Enabled here and not in the constructor: its vertex storage is compiled on first use, which needs the
        // running engine (as the other debug geometry, enabled from the console).
        mProbeGeometry->setEnabled(true);
        // Refilled whenever a cell's probes change; empty when it has none.
        mProbeGeometry->begin();
        if (mLoadedVolume)
            Render::Gi::drawProbes(*mProbeGeometry, *mLoadedVolume);
        if (mLoadedReflections)
            Render::Gi::drawReflectionProbes(*mProbeGeometry, *mLoadedReflections);
        mProbeGeometry->end();
    }

    void RenderManager::disableReflections()
    {
        if (!mReflectionData)
            return;
        // The header's "enabled" entry; the shader then adds no reflection.
        (*mReflectionData)[0].w = 0.f;
        mReflectionData->dirty();
        mLoadedReflections.reset();
    }

    void RenderManager::loadReflections(const Render::Gi::CellId& id)
    {
        if (!mReflectionData)
            return;
        const std::string path = Render::Gi::reflectionFilePath(id);
        const VFS::Manager& vfs = *mResourceSystem->getVFS();
        if (!vfs.exists(path))
            return;
        std::string text;
        {
            const auto stream = vfs.get(path);
            std::ostringstream buffer;
            buffer << stream->rdbuf();
            text = buffer.str();
        }
        Render::Gi::ProbeError error;
        const auto set = Render::Gi::parseReflections(text, path, error);
        if (!set)
        {
            Log(Debug::Warning) << "[Rendering] gi: skipped reflection file " << error.describe();
            return;
        }
        if (set->cell.interior != id.interior || set->cell.name != id.name)
        {
            Log(Debug::Warning) << "[Rendering] gi: " << path << " is the bake of another cell (" << set->cell.name
                                << "), skipped";
            return;
        }
        const auto packed = Render::Gi::packReflections(*set, mReflectionStrength);
        if (packed.size() > mReflectionData->size())
            return; // cannot happen (the parser limits a set), but never write past the buffer
        std::copy(packed.begin(), packed.end(), mReflectionData->begin());
        mReflectionData->dirty();
        if (mProbeGeometry)
            mLoadedReflections = std::make_unique<Render::Gi::ReflectionSet>(*set);
        Log(Debug::Info) << "GI: " << set->probes.size() << " reflection probe(s) for " << id.name;
    }

    void RenderManager::loadProbes(const MWWorld::Cell& cell)
    {
        if (!mProbeData)
            return;
        disableProbes();
        if (cell.isExterior())
            return;
        const Render::Gi::CellId id{ true, std::string(cell.getNameId()), 0, 0 };
        const std::string path = Render::Gi::probeFilePath(id);
        const VFS::Manager& vfs = *mResourceSystem->getVFS();
        // The reflection probes have a file of their own, and work without light probes and the other way round.
        loadReflections(id);
        if (!vfs.exists(path))
            return;
        std::string text;
        {
            const auto stream = vfs.get(path);
            std::ostringstream buffer;
            buffer << stream->rdbuf();
            text = buffer.str();
        }
        Render::Gi::ProbeError error;
        const auto set = Render::Gi::parseProbes(text, path, error);
        if (!set)
        {
            Log(Debug::Warning) << "[Rendering] gi: skipped probe file " << error.describe();
            return;
        }
        if (set->cell.interior != id.interior || set->cell.name != id.name)
        {
            Log(Debug::Warning) << "[Rendering] gi: " << path << " is the bake of another cell (" << set->cell.name
                                << "), skipped";
            return;
        }
        const auto packed = Render::Gi::packVolume(set->volumes.front(), set->ambient, mProbeStrength);
        if (packed.size() > mProbeData->size())
            return; // cannot happen (the parser limits a volume), but never write past the buffer
        std::copy(packed.begin(), packed.end(), mProbeData->begin());
        mProbeData->dirty();
        Log(Debug::Info) << "GI: " << set->volumes.front().probes.size() << " probe(s) for " << id.name;
        if (mProbeGeometry)
            mLoadedVolume = std::make_unique<Render::Gi::Volume>(set->volumes.front());
    }

    void RenderManager::configureAmbient(const MWWorld::Cell& cell)
    {
        auto ambient = vsg::vec4(MWAnim::rgbColor(cell.getMood().mAmbiantColor), 1.f);
        // OpenEden: as OpenMW's configureAmbient at 442eec6e2f, which skips it with classic falloff.
        if (!cell.isExterior() && !cell.isQuasiExterior() && !Settings::shaders().mClassicFalloff)
            MWAnim::brighten(ambient, mMinimumAmbientLuminance);
        setAmbientColour(ambient);
        loadProbes(cell);
        updateProbeDebug();

        auto diffuse = vsg::vec4(MWAnim::rgbColor(cell.getMood().mDirectionalColor), 1.f);
        mLightDiffuse = diffuse;
        mLightSpecular = diffuse;
        mLightPosition = vsg::vec3(-0.15f, 0.15f, 1.f);
    }

    void RenderManager::exteriorCellLoaded(int x, int y)
    {
        if (!mGiSkyEnabled)
            return;
        std::lock_guard<std::mutex> lock(mSkyVisMutex);
        mSkyCells.insert({ x, y });
        mSkyCellsChanged = true;
    }

    void RenderManager::exteriorCellUnloaded(int x, int y)
    {
        if (!mGiSkyEnabled)
            return;
        std::lock_guard<std::mutex> lock(mSkyVisMutex);
        mSkyCells.erase({ x, y });
        mSkyCellsChanged = true;
    }

    void RenderManager::updateSkyVisibility()
    {
        if (!mGiSkyEnabled)
            return;
        bool rewritten = false;
        // The scene may load and unload cells on another thread: take the wanted set under the lock, and read and
        // parse files without it (the files and the packed buffers are only touched here, on the render thread).
        std::set<std::pair<int, int>> wanted;
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(mSkyVisMutex);
            changed = mSkyCellsChanged;
            mSkyCellsChanged = false;
            if (changed)
                wanted = mSkyCells;
        }
        if (changed)
        {
            // Cells no longer loaded are forgotten; newly loaded ones are read from the data folders, once.
            for (auto it = mSkyFields.begin(); it != mSkyFields.end();)
                it = wanted.count(it->first) ? std::next(it) : mSkyFields.erase(it);
            const VFS::Manager& vfs = *mResourceSystem->getVFS();
            for (const auto& cell : wanted)
            {
                if (mSkyFields.count(cell))
                    continue;
                std::unique_ptr<Render::Gi::SkyVisField> field;
                const std::string path = Render::Gi::skyVisFilePath(cell.first, cell.second);
                if (vfs.exists(path))
                {
                    std::string text;
                    {
                        const auto stream = vfs.get(path);
                        std::ostringstream buffer;
                        buffer << stream->rdbuf();
                        text = buffer.str();
                    }
                    Render::Gi::ProbeError error;
                    auto parsed = Render::Gi::parseSkyVis(text, path, error);
                    if (!parsed)
                        Log(Debug::Warning) << "[Rendering] gi exterior: skipped file " << error.describe();
                    else if (parsed->cellX != cell.first || parsed->cellY != cell.second)
                        Log(Debug::Warning)
                            << "[Rendering] gi exterior: " << path << " is the bake of another cell, skipped";
                    else
                        field = std::make_unique<Render::Gi::SkyVisField>(std::move(*parsed));
                }
                mSkyFields[cell] = std::move(field);
            }
            // The map is ordered by cell, so the pool is laid out the same way every time.
            std::vector<const Render::Gi::SkyVisField*> fields;
            for (const auto& entry : mSkyFields)
                if (entry.second)
                    fields.push_back(entry.second.get());
            const auto result = Render::Gi::packSkyVisibility(
                fields, mSkyVisHeader->data(), mSkyVisPool->data(), mSkyVisPool->size());
            mSkyVisPool->dirty();
            if (result.skipped > 0)
                Log(Debug::Warning) << "[Rendering] gi exterior: " << result.skipped
                                    << " cell file(s) do not fit the field and are not used";
            if (!fields.empty())
                Log(Debug::Info) << "GI: sky-visibility field for " << result.packedCells << " exterior cell(s)";
            rewritten = true;
        }
        // The sky's colours change slowly with the hour and the weather; the header is written only when they move.
        const vsg::vec4 skyColour = mDisplaySkyColor;
        const vsg::vec4 horizon = mFog->getFogColor(false);
        const vsg::vec3 sky(skyColour.x, skyColour.y, skyColour.z);
        const vsg::vec3 horizonColour(horizon.x, horizon.y, horizon.z);
        if (!rewritten && mSkyVisHeaderWritten && sky == mSkyVisLastSky && horizonColour == mSkyVisLastHorizon)
            return;
        mSkyVisLastSky = sky;
        mSkyVisLastHorizon = horizonColour;
        mSkyVisHeaderWritten = true;
        Render::Gi::writeSkyHeader(
            mSkyVisHeader->data(), true, mSkyVisStrength, Render::Gi::skyFromColours(sky, horizonColour));
        mSkyVisHeader->dirty();
    }

    void RenderManager::setSunColour(const vsg::vec4& diffuse, const vsg::vec4& specular)
    {
        mLightDiffuse = diffuse;
        mLightSpecular = specular;
    }

    void RenderManager::setSunDirection(const vsg::vec3& direction)
    {
        vsg::vec3 position = direction * -1;
        mLightPosition = position;
        mSunLightPosition = vsg::vec4(position, 0.f);
        mSky->setSunDirection(position);
    }

    void RenderManager::addCell(const MWWorld::CellStore* store)
    {
        /*
        mPathgrid->addCell(store);
        */
    }
    void RenderManager::removeCell(const MWWorld::CellStore* store)
    {
        /*
        mPathgrid->removeCell(store);
        mActorsPaths->removeCell(store);
        */
        mScene->removeCell(store);
        mWaterRipples->removeCell(store);
    }

    void RenderManager::updateTerrain(const vsg::dvec3& pos)
    {
        if (auto compile = mTerrain->updateView(*mTerrainView, pos, mViewDistance))
            compile->compile();
    }

    void RenderManager::setGroundcoverStore(const MWWorld::GroundcoverStore* store)
    {
        // Called by the World: with its store from the loading thread, where update() then acts on it on the main
        // thread; with nullptr from its destructor on the main thread (the Game state is popped there), which must
        // not return while a chunk build still reads the store.
        mGroundcoverStore = store;
        if (!store && mGroundcover)
            mGroundcover->waitForBuilds();
        // Distant objects read the World's store too; going now waits for their builds.
        if (!store && mDistantObjects)
        {
            mDistantObjects.reset();
            updateSceneChildren();
        }
    }

    void RenderManager::updateGroundcover(const vsg::vec3& cameraPosition)
    {
        const MWWorld::GroundcoverStore* store = mGroundcoverStore;
        if (!store && mGroundcover)
        {
            mGroundcover.reset();
            updateSceneChildren();
        }
        else if (store && !mGroundcover && Settings::groundcover().mEnabled)
        {
            mGroundcover = std::make_unique<Groundcover>(
                mScene->context, *store, Settings::groundcover().mDensity, Settings::groundcover().mRenderingDistance);
            updateSceneChildren();
        }
        const MWWorld::Ptr player = MWMechanics::getPlayer();
        if (!player.isEmpty())
            mGroundcoverPlayerPosition = player.getRefData().getPosition().asVec3();
        if (!mGroundcover)
            return;
        // Groundcover belongs to Morrowind's exterior; an interior's or another worldspace's coordinates would select
        // its chunks.
        if (!player.isEmpty() && player.isInCell() && player.getCell()->isExterior()
            && player.getCell()->getCell()->getWorldSpace() == ESM::Cell::sDefaultWorldspaceId)
            mGroundcover->update(cameraPosition);
        else
            mGroundcover->hide();
    }

    void RenderManager::setActiveGrid(int x, int y, int halfGrid)
    {
        mActiveGrid = { { x, y }, halfGrid };
        if (mDistantObjects)
            mDistantObjects->setActiveGrid(mActiveGrid);
    }

    void RenderManager::updateDistantObjects(const vsg::vec3& cameraPosition)
    {
        if (!Settings::terrain().mDistantTerrain || !Settings::terrain().mObjectPaging)
            return;
        const MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.isEmpty() || !player.isInCell())
            return;
        if (!mDistantObjects)
        {
            mDistantObjects = std::make_unique<DistantObjects>(mScene->noParticlesContext,
                MWBase::Environment::get().getWorld()->getStore(), static_cast<float>(ESM::Land::REAL_SIZE),
                Settings::terrain().mObjectPagingMinSize);
            mDistantObjects->setActiveGrid(mActiveGrid);
            updateSceneChildren();
        }
        // Morrowind's exterior only: another worldspace's coordinates would select its chunks.
        if (player.getCell()->isExterior()
            && player.getCell()->getCell()->getWorldSpace() == ESM::Cell::sDefaultWorldspaceId)
            mDistantObjects->update(cameraPosition, mViewDistance);
        else
            mDistantObjects->hide();
    }

    namespace
    {
        // A chunk is keyed by the cell a reference is stored in, which need not be where it stands.
        DistantObjects::Cells referenceCells(const MWWorld::ConstPtr& ptr)
        {
            const float* pos = ptr.getCellRef().getPosition().pos;
            return { { ptr.getCell()->getCell()->getGridX(), ptr.getCell()->getCell()->getGridY() },
                { static_cast<int>(std::floor(pos[0] / ESM::Land::REAL_SIZE)),
                    static_cast<int>(std::floor(pos[1] / ESM::Land::REAL_SIZE)) } };
        }
    }

    bool RenderManager::pagingEnableObject(int type, const MWWorld::ConstPtr& ptr, bool enabled)
    {
        if (!mDistantObjects || !ptr.isInCell() || !ptr.getCell()->isExterior() || !isDistantObjectType(type))
            return false;
        return mDistantObjects->setDisabled(ptr.getCellRef().getRefNum(), !enabled, referenceCells(ptr));
    }

    void RenderManager::pagingBlacklistObject(int type, const MWWorld::ConstPtr& ptr)
    {
        if (!mDistantObjects || !ptr.isInCell() || !ptr.getCell()->isExterior() || !isDistantObjectType(type))
            return;
        const ESM::RefNum refnum = ptr.getCellRef().getRefNum();
        if (refnum.hasContentFile())
            mDistantObjects->blacklist(refnum, referenceCells(ptr));
    }

    void RenderManager::changeCellGrid(const vsg::vec3& pos)
    {
        updateTerrain(vsg::dvec3(pos));
    }

    void RenderManager::pruneCache()
    {
        getOperationThreads()->add(vsg::ref_ptr{ new vsgUtil::OperationFunc([this]() {
            mResourceSystem->updateCache(0);
            mTerrain->pruneCache();
            mScene->context.prune();
            // mPreview->mContext->prune();
        }) });
    }

    Sky* RenderManager::getSky()
    {
        ensureCompiled();
        return mSky.get();
    }

    void RenderManager::setSkyEnabled(bool enabled)
    {
        setRenderModeActive(Render_Sky, enabled);

        auto& shadow = mShadow->shadow;
        for (int i = 0; shadow && i < shadow->numCascades; ++i)
            shadow->cascadeView(i)->children = createCastShadowScene(enabled);
    }

    bool RenderManager::toggleBorders()
    {
        /*
        bool borders = !mTerrain->getBordersVisible();
        mTerrain->setBordersVisible(borders);
        return borders;
        */
        return false;
    }

    void RenderManager::updateSceneData()
    {
        bool isUnderwater
            = mRenderModes[Render_Water].active && mWater->isUnderwater(vsg::vec3(mCamera->getPosition()));
        auto fogColor = mFog->getFogColor(isUnderwater);
        // OpenEden (task 0025): with the HDR path the main view lights in linear space (lib/view/linear.glsl), so the
        // scene's colours are converted here, once. The display-encoded values are kept for what runs on the 8-bit
        // picture, the post-processing uniforms.
        const bool linear = mHdrTarget != nullptr;
        const auto toView = [linear](const vsg::vec4& color) { return linear ? Render::Hdr::toLinear(color) : color; };
        const vsg::vec4 clearColor = toView(vsg::vec4(fogColor[0], fogColor[1], fogColor[2], 1.f));
        mRenderGraph->setClearValues(VkClearColorValue{ { clearColor.x, clearColor.y, clearColor.z, 1 } });
        if (mHdrTarget)
            mHdrTarget->clearExtraOutputs(*mRenderGraph);

        mSceneData->setFogRange(mFog->getFogStart(isUnderwater), mFog->getFogEnd(isUnderwater));

        auto& sceneData = mSceneData->value();
        mDisplayFogColor = vsg::vec4(fogColor[0], fogColor[1], fogColor[2], 1.f);
        sceneData.fogColor = toView(mDisplayFogColor);
        // The marker of the linear view; 1 without HDR, as it always was (lib/view/linear.glsl).
        sceneData.fogColor.w = linear ? 2.f : 1.f;

        auto ambient = mAmbientColor;
        ambient += vsg::vec4(0.7, 0.7, 0.7, 0.0) * mNightEyeFactor;
        mDisplayAmbient = ambient;
        sceneData.ambient = toView(ambient);
        mDisplaySkyColor = mRenderModes[Render_Sky]
            ? mSky->getSkyColor()
            : Misc::VsgMath::componentMultiply(mFog->getFogColor(false), vsg::vec4(1.3, 1.3, 1.3, 1));
        sceneData.skyColor = toView(mDisplaySkyColor);
        sceneData.rainIntensity = mRenderModes[Render_Sky] ? mSky->getRainIntensity() : 0;
        // Groundcover's wind and stomping (task 0012), as OpenMW's SharedUniformStateUpdater set them. The player
        // position comes from update(), which runs only in game: this also runs at the main menu, without a player.
        sceneData.groundcoverPlayer = vsg::vec4(mGroundcoverPlayerPosition, mSky->getBaseWindSpeed());
        sceneData.groundcoverParams = vsg::vec4(Settings::groundcover().mRenderingDistance,
            static_cast<float>(Settings::groundcover().mStompMode),
            static_cast<float>(Settings::groundcover().mStompIntensity),
            // w: the main view's sample count, for alpha to coverage's sample mask (task 0012, V4j).
            static_cast<float>(mEngine.framebufferSamples()));
        sceneData.lightDiffuse = toView(mLightDiffuse);
        sceneData.lightSpecular = toView(mLightSpecular);
        if (mOccluder)
        {
            sceneData.lightDiffuse.w = mOccluder->boxSize();
            sceneData.lightSpecular.w = mOccluder->boxHeight();
        }
        sceneData.invView = vsg::mat4(vsg::inverse_4x3(mViewMatrix->transform()));
        mSceneData->setLightPosition(mLightPosition, sceneData.invView);
        sceneData.lightViewPos.a = mRenderModes[Render_Sky] ? mSky->getLightSize() : 8.f;

        const auto& vp = mSceneCamera->getViewport();
        sceneData.resolution = { vp.width, vp.height };
        mShadow->updateCascades(*mSceneCamera, sceneData, mLightPosition);
        mSceneData->dirty();
        mLightGrid->update();
        updatePostProcess();
    }

    void RenderManager::updateOccluder(const vsg::vec3& cameraPosition)
    {
        if (!mOccluder)
            return;
        // OpenMW occludes rain and the snow effect, the two it wraps around the camera (SkyManager::update at
        // 442eec6e2f).
        const bool snow
            = mRenderModes[Render_Sky] && mSky->getParticleEffect() == Settings::models().mWeathersnow.get().value();
        const bool active = mRainParameters.enabled || snow;
        if (active != mOccluderActive)
        {
            mOccluderActive = active;
            if (active)
                mOccluder->view()->children = { mScene->getObjects() };
            else
                mOccluder->view()->children.clear();
        }
        if (active)
        {
            const vsg::vec3 rainRange(mRainParameters.diameter, mRainParameters.diameter,
                (mRainParameters.minHeight + mRainParameters.maxHeight) / 2.f);
            // OpenMW's defaultWrapRange for the snow effect.
            const vsg::vec3 snowRange(1024.f, 1024.f, 800.f);
            mOccluder->update(cameraPosition, mRainParameters.enabled ? rainRange : snowRange);
        }
    }

    void RenderManager::updatePostProcess()
    {
        if (!mPostProcess)
            return;
        const Pipeline::Data::Scene& scene = mSceneData->value();
        Fx::OmwData data;
        // The images have OpenGL's bottom-left origin, where VSG's projection has Vulkan's.
        const vsg::mat4 flipY = vsg::scale(1.f, -1.f, 1.f);
        data.projectionMatrix = flipY * vsg::mat4(mProjectionMatrix->transform());
        data.invProjectionMatrix = scene.invProjection * flipY;
        data.prevViewMatrix = mPostProcessLastView;
        data.viewMatrix = vsg::mat4(mViewMatrix->transform());
        data.invViewMatrix = scene.invView;
        mPostProcessLastView = data.viewMatrix;
        data.eyePos = vsg::vec4(vsg::vec3(mViewMatrix->eye), 0.f);
        const vsg::dvec3 eyeVec = vsg::normalize(mViewMatrix->center - mViewMatrix->eye);
        data.eyeVec = vsg::vec4(vsg::vec3(eyeVec), 0.f);
        // The display-encoded colours: the chain runs on the 8-bit picture, as it does without HDR.
        data.fogColor = mDisplayFogColor;
        data.ambientColor = mDisplayAmbient;
        data.skyColor = mDisplaySkyColor;
        data.sunColor = mLightDiffuse;
        // OpenMW's setSunDirection and StateUpdater::setSunPos: the sun's visible orbit, mirrored at night.
        vsg::vec3 sunPosition = mLightPosition;
        sunPosition.z = 400.f - std::abs(sunPosition.x);
        sunPosition = vsg::normalize(sunPosition);
        if (mPostProcessNight)
            sunPosition.z *= -1.f;
        data.sunPos = vsg::vec4(sunPosition, 0.f);
        data.sunVec = vsg::vec4(vsg::normalize(mLightPosition) * -1.f, 0.f);
        data.resolution = scene.resolution;
        data.rcpResolution = vsg::vec2(1.f / scene.resolution.x, 1.f / scene.resolution.y);
        const bool underwater = mRenderModes[Render_Water].active && mWater->isUnderwater(vsg::vec3(mViewMatrix->eye));
        data.isUnderwater = underwater ? 1 : 0;
        data.isWaterEnabled = mRenderModes[Render_Water].active ? 1 : 0;
        data.waterHeight = mPostProcessWaterHeight;
        data.fogNear = mFog->getFogStart(underwater);
        data.fogFar = mFog->getFogEnd(underwater);
        data.near = mNearClip;
        data.far = mViewDistance;
        data.fov = getFieldOfView();
        data.simulationTime = scene.time;
        data.deltaSimulationTime = scene.time - mPostProcessLastTime;
        mPostProcessLastTime = scene.time;
        const MWBase::World* world = MWBase::Environment::get().getWorld();
        if (world)
        {
            data.gameHour = world->getTimeStamp().getHour();
            data.sunVis = world->getSunVisibility();
            data.windSpeed = world->getWindSpeed();
            // OpenMW's StateUpdater::setWeatherTransition.
            const float transition = world->getWeatherTransition();
            data.weatherTransition = transition > 0.f ? 1.f - transition : 0.f;
            data.weatherID = world->getCurrentWeatherScriptId();
            data.nextWeatherID = world->getNextWeatherScriptId();
        }
        data.isInterior = mRenderModes[Render_Sky].active ? 0 : 1;
        data.frameNumber = static_cast<std::int32_t>(mPostProcessFrame++);
        mPostProcess->update(data);
    }

    void RenderManager::configureFog(const MWWorld::Cell& cell)
    {
        mFog->configure(mViewDistance, cell);
    }

    void RenderManager::configureFog(
        float fogDepth, float underwaterFog, float dlFactor, float dlOffset, const vsg::vec4& color)
    {
        mFog->configure(mViewDistance, fogDepth, underwaterFog, dlFactor, dlOffset, color);
    }

    Render::Tonemap::SssSources RenderManager::sssSources() const
    {
        Render::Tonemap::SssSources sources;
        if (!mSssEnabled || !mHdrTarget)
            return sources;
        sources.material = mHdrTarget->materialView();
        sources.linearDepth = mHdrTarget->linearDepthView();
        sources.normal = mHdrTarget->normalView();
        return sources;
    }

    Render::Tonemap::AoSources RenderManager::aoSources() const
    {
        Render::Tonemap::AoSources sources;
        if (!mAmbientOcclusion || !mHdrTarget)
            return sources;
        sources.ambient = mHdrTarget->ambientView();
        sources.ao = mAmbientOcclusion->result();
        sources.normal = mHdrTarget->normalView();
        sources.linearDepth = mHdrTarget->linearDepthView();
        sources.aoExtent = mAmbientOcclusion->resultExtent();
        return sources;
    }

    void RenderManager::updateVelocity()
    {
        if (!mVelocity)
            return;
        // This frame's camera against the previous frame's (Render::Velocity). The first frame, and a jump of the
        // camera (a teleport or a cell change), compare the camera with itself, so that no frame of nonsense motion
        // reaches a consumer.
        const vsg::dmat4 view = mViewMatrix->transform();
        const vsg::dmat4 projection = mProjectionMatrix->transform();
        const vsg::dvec3 eye = mViewMatrix->eye;
        // onFrame does not run while a menu or loading screen is up, so a long gap since the last call means the camera
        // may have been replaced (a loaded save, a new game): a quarter of a second is far longer than any frame.
        const auto now = std::chrono::steady_clock::now();
        const bool resumed = now - mPreviousCameraTime > std::chrono::milliseconds(250);
        mPreviousCameraTime = now;
        if (!mHavePreviousCamera || resumed || Render::Velocity::cameraJumped(mPreviousEye, eye))
        {
            mPreviousView = view;
            mPreviousProjection = projection;
            mPreviousEye = eye;
            mHavePreviousCamera = true;
        }
        mVelocity->setMatrix(Render::Velocity::toPreviousClip(mPreviousProjection, mPreviousView, vsg::inverse(view)),
            static_cast<float>(projection(0, 0)), static_cast<float>(projection(1, 1)));
        mPreviousView = view;
        mPreviousProjection = projection;
        mPreviousEye = eye;
    }

    void RenderManager::onFrame(float dt)
    {
        updateSceneData();
        updateSkyVisibility();
        if (mHdrTarget)
        {
            // The scene is in the float target, and its single-sample copies are what the copies and the depth pass
            // read.
            mReflect->setSource(mHdrTarget->colorView(), mHdrTarget->depthView());
            if (mPostProcess)
                mPostProcess->setDepthSource(mHdrTarget->depthView());
            // A technique that sets `hdr` tonemaps for itself: the built-in curve then only clips and encodes
            // (Render::Hdr::resolveTonemap). Parameters change between frames, here.
            const bool steppedAside = mPostProcess
                && std::any_of(mPostProcess->getTechniques().begin(), mPostProcess->getTechniques().end(),
                    [this](const auto& technique) {
                        // Only one that runs: a technique that failed to build draws nothing and tonemaps nothing.
                        return technique->getHDR() && mPostProcess->getRenderError(technique).empty();
                    });
            if (steppedAside != mTonemapSteppedAside)
            {
                mTonemapSteppedAside = steppedAside;
                mTonemap->setParameters(tonemapParameters(steppedAside));
                if (steppedAside)
                    Log(Debug::Info) << "HDR: a post-processing technique sets hdr, so the built-in tonemap only clips";
            }
        }
        updateVelocity();
        if (mSssEnabled && mTonemap && mHdrTarget)
        {
            // The blur's size on the screen follows the projection and the target's height; set only when it moves.
            const vsg::dmat4 projection = mProjectionMatrix->transform();
            auto parameters = mTonemap->parameters();
            const float focal = static_cast<float>(0.5 * mHdrTarget->extent().height * projection(1, 1));
            if (parameters.sssFocalPixels != focal)
            {
                parameters.sssFocalPixels = focal;
                mTonemap->setParameters(parameters);
            }
        }
        if (mAmbientOcclusion)
        {
            const vsg::dmat4 projection = mProjectionMatrix->transform();
            mAmbientOcclusion->setProjection(
                static_cast<float>(projection(0, 0)), static_cast<float>(projection(1, 1)));
        }
        mReflect->update();
        if (mPostProcess)
            mPostProcess->updateImages();

        if (mRenderModes[Render_Terrain])
            updateTerrain(mViewMatrix->eye);
    }

    void RenderManager::setupScreenshotInterface()
    {
        mScreenshotInterface->request = [&](int w, int h) {
            auto device = mCompileContext->device;
            auto pair = Render::createDownloadCommands(device, mEngine.colorImageView(0)->image,
                { { 0, 0 }, mEngine.swapchainExtent() }, Render::compatibleColorFormat, mEngine.getPresentLayout(),
                VkExtent2D{ static_cast<uint32_t>(w), static_cast<uint32_t>(h) });
            auto commands = pair.first;
            auto dstImage = pair.second;
            mScreenshotSwitch->children = { { true, commands } };
            mScreenshotInterface->framesUntilReady = mEngine.numFrames();
            mScreenshotInterface->retrieve = [this, device, dstImage]() -> std::pair<vsg::ref_ptr<vsg::Data>, bool> {
                mScreenshotSwitch->children.clear();
                if (--mScreenshotInterface->framesUntilReady > 0)
                    return { {}, false };
                auto out_data = vsg::ubvec4Array2D::create(
                    dstImage->extent.width, dstImage->extent.height, vsg::Data::Properties(dstImage->format));
                if (!Render::mapAndCopy(device, dstImage, out_data))
                    out_data = {};
                return { out_data, true };
            };
        };
    }

    void RenderManager::update(float dt, bool paused)
    {
        if (paused)
            dt = 0; // return;
        else
            mSceneData->value().time += dt;

        mEffects->update(dt);
        mProjectiles->update(dt);
        static const std::string noEffect;
        mWeatherEffects->setEffect(mRenderModes[Render_Sky] ? mSky->getParticleEffect() : noEffect);
        // Weather particles stop at the water surface, as OpenMW's underwater switch did.
        const vsg::vec3 cameraPosition(mCamera->getPosition());
        const bool aboveWater = !(mRenderModes[Render_Water].active && mWater->isUnderwater(cameraPosition));
        mWeatherEffects->update(
            dt, cameraPosition, mSky->getParticleOrientation(), mSky->getPrecipitationAlpha(), aboveWater);
        mSky->getRainParameters(mRainParameters);
        mRainParameters.enabled = mRainParameters.enabled && mRenderModes[Render_Sky];
        mRain->update(dt, cameraPosition, mRainParameters, aboveWater);
        mWaterRipples->update(dt, paused);
        updateGroundcover(cameraPosition);
        updateDistantObjects(cameraPosition);
        if (mRenderModes[Render_Sky])
            mSky->update(dt);

        mPlayer->update(dt);
        setFirstPersonMode(mCamera->getMode() == Camera::Mode::FirstPerson);

        mScene->setNightDayMode(MWBase::Environment::get().getWorld()->getNightDayMode());
        mScene->update(dt);
        updateNavMesh();
        /*
        updateRecastMesh();
        */
        mCamera->update(dt, paused);
        mCamera->updateCamera(*mViewMatrix);
        // After the camera moves: the particle shader centres the box on this frame's view.
        updateOccluder(vsg::vec3(mViewMatrix->eye));
    }

    void RenderManager::setNavMeshNumber(const std::size_t value)
    {
        mNavMeshNumber = value;
    }

    void RenderManager::setNavMeshMode(Settings::NavMeshRenderMode value)
    {
        mNavMesh->setMode(value);
    }

    void RenderManager::updateNavMesh()
    {
        if (!mNavMesh->isEnabled() || mNavigator == nullptr)
            return;

        const auto navMeshes = mNavigator->getNavMeshes();

        auto it = navMeshes.begin();
        for (std::size_t i = 0; it != navMeshes.end() && i < mNavMeshNumber; ++i)
            ++it;
        if (it == navMeshes.end())
        {
            mNavMesh->reset();
        }
        else
        {
            try
            {
                mNavMesh->update(it->second, mNavMeshNumber, mNavigator->getSettings());
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "NavMesh render update exception: " << e.what();
            }
        }
    }

    void RenderManager::updatePlayerPtr(const MWWorld::Ptr& ptr)
    {
        if (mPlayer.get())
        {
            setupPlayer(ptr);
            mPlayer->updatePtr(ptr);
        }
        mCamera->attachTo(ptr);
    }

    void RenderManager::rotateObject(const MWWorld::Ptr& ptr, const vsg::dquat& rot)
    {
        if (ptr == mCamera->getTrackingPtr() && !mCamera->isVanityOrPreviewModeEnabled())
            mCamera->rotateCameraToTrackingPtr();
        if (auto obj = getObject(ptr))
            obj->transform()->setAttitude(vsg::quat(rot));
    }

    void RenderManager::moveObject(const MWWorld::Ptr& ptr, const vsg::vec3& pos)
    {
        if (auto obj = getObject(ptr))
            obj->transform()->translation = pos;
    }

    void RenderManager::scaleObject(const MWWorld::Ptr& ptr, const vsg::vec3& scale)
    {
        if (auto obj = getObject(ptr))
            obj->transform()->scale = scale;
        if (ptr == mCamera->getTrackingPtr()) // update height of camera
            mCamera->processViewChange();
    }

    void RenderManager::removeObject(const MWWorld::Ptr& ptr)
    {
        /*
        mActorsPaths->remove(ptr);
        */
        mWaterRipples->removeEmitter(ptr);
        mScene->removeObject(ptr);
    }

    void RenderManager::setWaterEnabled(bool enabled)
    {
        setRenderModeActive(Render_Water, enabled);
        mSimpleWater->setEnabled(enabled);
    }

    void RenderManager::setWaterHeight(float height)
    {
        mPostProcessWaterHeight = height;
        mWater->setHeight(height);
        mSimpleWater->setHeight(height);
        mWaterRipples->setWaterHeight(height);
    }

    vsg::vec4 RenderManager::getScreenBounds(const vsg::dbox& worldbb)
    {
        if (!Misc::VsgMath::valid(worldbb))
            return {};
        auto viewProj = mProjectionMatrix->transform() * mViewMatrix->transform();
        float min_x = 1.0f, max_x = 0.0f, min_y = 1.0f, max_y = 0.0f;
        for (int i = 0; i < 8; ++i)
        {
            auto screen = Render::worldToScreen(viewProj, vsg::dvec3(Misc::VsgMath::corner(worldbb, i)));
            min_x = std::min(min_x, screen.x);
            max_x = std::max(max_x, screen.x);
            min_y = std::min(min_y, screen.y);
            max_y = std::max(max_y, screen.y);
        }
        return { min_x, min_y, max_x, max_y };
    }

    vsg::vec2 RenderManager::getScreenCoords(const vsg::dbox& bb)
    {
        // As OpenMW 0.51 places the focus tooltip: the top centre of the object's bounds.
        if (Misc::VsgMath::valid(bb))
        {
            const vsg::dmat4 viewProj = mProjectionMatrix->transform() * mViewMatrix->transform();
            const vsg::dvec4 clipPoint
                = viewProj * vsg::dvec4((bb.min.x + bb.max.x) * 0.5, (bb.min.y + bb.max.y) * 0.5, bb.max.z, 1.0);
            if (clipPoint.w > 0.0)
            {
                // Vulkan clip space has y pointing down, so this is already measured from the top.
                const float screenPointX = static_cast<float>((clipPoint.x / clipPoint.w + 1.0) * 0.5);
                const float screenPointY = static_cast<float>((clipPoint.y / clipPoint.w + 1.0) * 0.5);
                if (screenPointX >= 0.f && screenPointX <= 1.f && screenPointY >= 0.f && screenPointY <= 1.f)
                    return vsg::vec2(screenPointX, screenPointY);
            }
        }

        return vsg::vec2(0.5f, 0.f);
    }

    RenderManager::RayResult RenderManager::castRay(
        const vsg::vec3& origin, const vsg::vec3& dest, bool ignorePlayer, bool ignoreActors)
    {
        return MWRender::castRay(
            vsg::dvec3(origin), vsg::dvec3(dest), ignorePlayer, ignoreActors, *mScene, mIntersectorScene);
    }

    RenderManager::RayResult RenderManager::castCameraToViewportRay(
        float nX, float nY, float maxDistance, bool ignorePlayer, bool ignoreActors)
    {
        return MWRender::castCameraToViewportRay(
            nX, nY, maxDistance, ignorePlayer, ignoreActors, *mSceneCamera, *mScene, mIntersectorScene);
    }

    void RenderManager::updatePtr(const MWWorld::Ptr& old, const MWWorld::Ptr& updated)
    {
        mWaterRipples->updateEmitterPtr(old, updated);
        mScene->updatePtr(old, updated);
        // mActorsPaths->updatePtr(old, updated);
    }

    void RenderManager::spawnEffect(const std::string& model, std::string_view texture, const vsg::vec3& worldPosition,
        float scale, bool isMagicVFX, std::string_view effectId, bool loop)
    {
        mEffects->add(model, std::string(texture), worldPosition, scale, isMagicVFX, effectId, loop);
    }

    void RenderManager::removeEffect(std::string_view effectId)
    {
        mEffects->remove(effectId);
    }

    void RenderManager::notifyWorldSpaceChanged()
    {
        mEffects->clear();
        mWaterRipples->clear();
        // Groundcover is kept: its chunks depend only on the World's store and Morrowind's exterior, and
        // updateGroundcover hides them away from it, so coming out of a door reuses them.
    }

    void RenderManager::clear()
    {
        mRenderModes.clear();
        mRenderModes.resize(Render_Count);
        updateSceneChildren();
        mSky->setMoonColour(false);

        notifyWorldSpaceChanged();
        if (mDistantObjects)
            mDistantObjects->clear();
    }

    MWAnim::Object* RenderManager::getObject(const MWWorld::Ptr& ptr)
    {
        if (mPlayer.get() && ptr == mPlayer->getPtr())
            return mPlayer.get();
        return mScene->getObject(ptr);
    }

    const MWAnim::Object* RenderManager::getObject(const MWWorld::ConstPtr& ptr) const
    {
        const auto p
            = MWWorld::Ptr(const_cast<MWWorld::LiveCellRefBase*>(ptr.mRef), const_cast<MWWorld::CellStore*>(ptr.mCell));
        return const_cast<RenderManager*>(this)->getObject(p);
    }

    vsg::box RenderManager::getBoundingBox(const MWWorld::ConstPtr& ptr) const
    {
        auto obj = getObject(ptr);
        if (!obj)
            return {};
        return vsg::box(vsg::visit<vsg::ComputeBounds>(obj->transform()).bounds);
    }

    void RenderManager::setupPlayer(const MWWorld::Ptr& player)
    {
        mWaterRipples->removeEmitter(player);
        mWaterRipples->addEmitter(player);
    }

    void RenderManager::renderPlayer(const MWWorld::Ptr& player)
    {
        mPlayer.reset(new Player(mScene->context, player, Npc::ViewMode::Normal));
        mPlayer->compile();
        mPlayerGroup->children = { { Mask_Player, mPlayer->node() } };
        mCamera->setAnimation(mPlayer.get());
        mCamera->attachTo(player);
    }

    void RenderManager::rebuildPtr(const MWWorld::Ptr& ptr)
    {
        if (Npc* anim = dynamic_cast<Npc*>(getObject(ptr)))
        {
            anim->rebuild();
            if (anim == mPlayer.get() && mCamera->getTrackingPtr() == ptr)
            {
                mCamera->attachTo(ptr);
                mCamera->setAnimation(mPlayer.get());
            }
        }
    }

    void RenderManager::addWaterRippleEmitter(const MWWorld::Ptr& ptr)
    {
        mWaterRipples->addEmitter(ptr);
    }

    void RenderManager::removeWaterRippleEmitter(const MWWorld::Ptr& ptr)
    {
        mWaterRipples->removeEmitter(ptr);
    }

    void RenderManager::emitWaterRipple(const vsg::vec3& pos)
    {
        mWaterRipples->emitRipple(pos);
    }

    void RenderManager::updateProjectionMatrix()
    {
        float fov = getFieldOfView();
        const VkExtent2D windowSize = mAppliedExtent; // the extent drawn into, not the one requested
        // Field by field: vsg::Perspective's implicit copy assignment is deprecated.
        mProjectionMatrix->fieldOfViewY = fov;
        mProjectionMatrix->aspectRatio = static_cast<double>(windowSize.width) / static_cast<double>(windowSize.height);
        mProjectionMatrix->nearDistance = mNearClip;
        mProjectionMatrix->farDistance = mViewDistance;

        mSceneData->setDepthRange(mNearClip, mViewDistance);
        auto& sceneData = mSceneData->value();
        sceneData.projection = vsg::mat4(mProjectionMatrix->transform());
        sceneData.invProjection = vsg::inverse(sceneData.projection);
    }

    void RenderManager::processChangedSettings(const Settings::CategorySettingVector& changed)
    {
        for (Settings::CategorySettingVector::const_iterator it = changed.begin(); it != changed.end(); ++it)
        {
            if (it->first == "Camera" && it->second == "field of view")
            {
                mFieldOfView = Settings::camera().mFieldOfView;
                updateProjectionMatrix();
            }
            else if (it->first == "Camera" && it->second == "viewing distance")
            {
                mViewDistance = Settings::camera().mViewingDistance;
                // mFog->configure
                updateProjectionMatrix();
            }
        }
    }

    // TODO: if there is only ever one worldspace active at a time, isn't the worldspace parameter redundant
    float RenderManager::getTerrainHeightAt(const vsg::vec3& pos, ESM::RefId worldspace)
    {
        return mTerrainStorage->getHeightAt(pos, worldspace);
    }

    void RenderManager::overrideFieldOfView(float val)
    {
        if (!mFieldOfViewOverride || *mFieldOfViewOverride != val)
        {
            mFieldOfViewOverride = val;
            updateProjectionMatrix();
        }
    }

    void RenderManager::setFieldOfView(float val)
    {
        if (val != mFieldOfView)
        {
            mFieldOfView = val;
            updateProjectionMatrix();
        }
    }

    float RenderManager::getFieldOfView() const
    {
        return mFieldOfViewOverride ? *mFieldOfViewOverride : mFieldOfView;
    }

    void RenderManager::resetFieldOfView()
    {
        if (mFieldOfViewOverride)
        {
            mFieldOfViewOverride = {};
            updateProjectionMatrix();
        }
    }

    void RenderManager::setViewDistance(float d, bool dummy)
    {
        if (mViewDistance != d)
        {
            mViewDistance = d;
            updateProjectionMatrix();
        }
    }

    vsg::vec3 RenderManager::getHalfExtents(const MWWorld::ConstPtr& object) const
    {
        std::string modelName = std::string(object.getClass().getModel(object));
        if (modelName.empty())
            return {};

        vsg::ref_ptr<const vsg::Node> node = vsgUtil::readNode(modelName, mResourceSystem->nodeOptions);
        auto bounds = vsg::visit<vsg::ComputeBounds>(node).bounds;
        if (bounds.valid())
        {
            auto half = vsg::vec3(vsgUtil::extent(bounds) / 2.0);
            return { half.x, half.y, half.z };
        }
        return {};
    }

    void RenderManager::exportSceneGraph(const MWWorld::Ptr& ptr, const std::filesystem::path& dir)
    {
        vsg::ref_ptr<vsg::Node> node = mSceneRoot;
        if (auto obj = getObject(ptr))
            node = obj->node();
        auto path = (dir / "scene.vsgt").string();
        if (!vsg::write(node, path))
            std::cerr << "!vsg::write(" << path << ")" << std::endl;
    }

    LandManager* RenderManager::getLandManager() const
    {
        return mTerrainStorage->getLandManager();
    }

    void RenderManager::removeActorPath(const MWWorld::ConstPtr& actor) const
    {
        // mActorsPaths->remove(actor);
    }

    void RenderManager::updateExtents(uint32_t w, uint32_t h)
    {
        // Only asks the window for a new size. It is rebuilt when the next frame acquires its image, so the render
        // areas and the camera follow in syncExtents: setting them here would record this frame's passes with an area
        // larger than the old framebuffer (VUID-VkRenderPassBeginInfo-pNext-02852), which draws nothing and, as seen,
        // left the window blank after growing.
        mEngine.updateExtents({ w, h });
    }

    void RenderManager::syncExtents()
    {
        // Before the viewer acquires the next image, which is when the swapchain is rebuilt to the requested size: see
        // clampArea.
        const VkExtent2D extent = clampArea(mAppliedExtent, mEngine.swapchainExtent(), mEngine.extent2D());
        if (extent.width == mAppliedExtent.width && extent.height == mAppliedExtent.height)
            return;
        mAppliedExtent = extent;
        mSceneCamera->viewportState->set(0, 0, extent.width, extent.height);
        mRenderGraph->renderArea = { { 0, 0 }, extent };
        mGuiRenderGraph->renderArea = { { 0, 0 }, extent };
        if (mHdrTarget)
        {
            // New images and a new framebuffer, in the same render pass; the render area follows in the same call, as a
            // render area larger than the framebuffer draws nothing.
            mHdrTarget->resize(
                *mHdrContext, extent, [this](vsg::ref_ptr<vsg::Object> retired) { mCompileContext->detach(retired); });
            mRenderGraph->framebuffer = mHdrTarget->framebuffer();
            mTonemapGraph->renderArea = { { 0, 0 }, extent };
            if (mVelocity
                && !mVelocity->setSource(*mHdrContext, mHdrTarget->linearDepthView(), mHdrTarget->objectMotionView(),
                    extent, [this](vsg::ref_ptr<vsg::Object> retired) { mCompileContext->detach(retired); }))
                Log(Debug::Error) << "The velocity pass could not be rebuilt after a resize";
            if (mAmbientOcclusion
                && !mAmbientOcclusion->setSource(*mHdrContext, mHdrTarget->linearDepthView(), mHdrTarget->normalView(),
                    extent, [this](vsg::ref_ptr<vsg::Object> retired) { mCompileContext->detach(retired); }))
                Log(Debug::Error) << "The ambient occlusion pass could not be rebuilt after a resize";
            if (mBloom
                && !mBloom->setSource(*mHdrContext, mHdrTarget->colorView(), extent,
                    [this](vsg::ref_ptr<vsg::Object> retired) { mCompileContext->detach(retired); }))
                Log(Debug::Error) << "The bloom passes could not be rebuilt after a resize";
            if (!mTonemap->setSource(mHdrTarget->colorView(), mBloom ? mBloom->result() : nullptr, extent,
                    debugSources(*mHdrTarget, mVelocity ? mVelocity->result() : nullptr), aoSources(), sssSources()))
                Log(Debug::Error) << "The tonemap pass could not be rebuilt after a resize";
        }
        updateProjectionMatrix();
        if (mPostProcess)
            mPostProcess->resize(extent.width, extent.height);
    }

    vsg::ref_ptr<vsg::OperationThreads> RenderManager::getOperationThreads()
    {
        return mOperationThreads;
    }

    vsg::ref_ptr<vsgUtil::UpdateThreads> RenderManager::getUpdateThreads()
    {
        return mScene->getUpdateThreads();
    }

    void RenderManager::setFirstPersonMode(bool enabled)
    {
        if (enabled)
        {
            if (!mFirstPersonProjection)
            {
                auto firstPersonProjection = vsg::ref_ptr{ new vsgUtil::SetProjection };
                auto fov = vsg::ref_ptr{ new vsgUtil::FieldOfView };
                fov->perspective = mProjectionMatrix;
                fov->fieldOfViewY = Settings::camera().mFirstPersonFieldOfView;
                firstPersonProjection->projection = fov;
                mFirstPersonProjection = firstPersonProjection;
            }
            mPlayerGroup->children = { { Mask_FirstPerson, mFirstPersonProjection } };
            mFirstPersonProjection->children = { mPlayer->node() };
        }
        else
            mPlayerGroup->children = { { Mask_Player, mPlayer->node() } };
    }

    vsg::dmat4 RenderManager::getProjectionMatrix() const
    {
        return mProjectionMatrix->transform();
    }
}
