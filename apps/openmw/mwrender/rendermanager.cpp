#include "rendermanager.hpp"

#include "lodbakemode.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>

#include <vsg/app/CommandGraph.h>
#include <vsg/app/CompileManager.h>
#include <vsg/app/RenderGraph.h>
#include <vsg/core/Exception.h>
#include <vsg/core/Objects.h>
#include <vsg/io/write.h>
#include <vsg/threading/OperationThreads.h>
#include <vsg/utils/ComputeBounds.h>
#include <vsg/vk/Context.h>

#include <components/animation/pointshadowcarrier.hpp>
#include <components/animation/skin.hpp>
#include <components/animation/transform.hpp>
#include <components/debug/debuglog.hpp>
#include <components/detournavigator/agentbounds.hpp>
#include <components/detournavigator/navigator.hpp>
#include <components/detournavigator/navmeshcacheitem.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadland.hpp>
#include <components/fx/technique.hpp>
#include <components/files/conversion.hpp>
#include <components/lod/fartier.hpp>
#include <components/lod/lodworld.hpp>
#include <components/lod/viewlimits.hpp>
#include <components/misc/loadtrace.hpp>
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
#include <components/render/cloudgpu.hpp>
#include <components/render/cloudmath.hpp>
#include <components/render/covermath.hpp>
#include <components/render/download.hpp>
#include <components/render/engine.hpp>
#include <components/render/gbuffermath.hpp>
#include <components/render/hdrmath.hpp>
#include <components/render/hdrtarget.hpp>
#include <components/render/lightningmath.hpp>
#include <components/render/materialmath.hpp>
#include <components/render/materialoverrides.hpp>
#include <components/render/parallaxmath.hpp>
#include <components/render/precipitationmath.hpp>
#include <components/render/probedebug.hpp>
#include <components/render/probegpu.hpp>
#include <components/puddle/puddlegpu.hpp>
#include <components/render/puddlemapgpu.hpp>
#include <components/render/reflectiongpu.hpp>
#include <components/render/screen.hpp>
#include <components/render/screenshot.hpp>
#include <components/render/skyenvmath.hpp>
#include <components/render/skyvisdebug.hpp>
#include <components/render/skyvisgpu.hpp>
#include <components/render/sssmath.hpp>
#include <components/render/tonemap.hpp>
#include <components/render/velocity.hpp>
#include <components/render/vrambudgetscript.hpp>
#include <components/render/wetnessmath.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/values.hpp>

#include <components/neutralterrain/cellcache.hpp>
#include <components/neutralterrain/storage.hpp>
#include <components/terrain/paging.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/recursivedirectoryiterator.hpp>
#include <components/view/barriers.hpp>
#include <components/view/cloudvolume.hpp>
#include <components/view/collectlights.hpp>
#include <components/view/defaultstate.hpp>
#include <components/view/descriptors.hpp>
#include <components/view/lightgrid.hpp>
#include <components/view/scene.hpp>
#include <components/view/volumefog.hpp>
#include <components/vsgadapters/nif/nif.hpp>
#include <components/vsgutil/bounds.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/compileop.hpp>
#include <components/vsgutil/computebin.hpp>
#include <components/vsgutil/deferredbin.hpp>
#include <components/vsgutil/nullbin.hpp>
#include <components/vsgutil/projection.hpp>
#include <components/vsgutil/readnode.hpp>
#include <components/vsgutil/setbin.hpp>
#include <components/vsgutil/sharedview.hpp>
#include <components/vsgutil/suspendrenderpass.hpp>
#include <components/vsgutil/texturecap.hpp>
#include <components/vsgutil/textureresize.hpp>
#include <components/vsgutil/updatethreads.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwmechanics/actorutil.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "extentsync.hpp"

#include "animcontext.hpp"
#include "bin.hpp"
#include "skyrimmodel.hpp"
#include "camera.hpp"
#include "distantobjects.hpp"
#include "effects.hpp"
#include "env.hpp"
#include "fogmanager.hpp"
#include "groundcover.hpp"
#include "intersect.hpp"
#include "lodclusters.hpp"
#include "lodterrain.hpp"
#include "map.hpp"
#include "mask.hpp"
#include "navmesh.hpp"
#include "player.hpp"
#include "pointshadows.hpp"
#include "postprocess.hpp"
#include "precipitationoccluder.hpp"
#include "preview.hpp"
#include "projectiles.hpp"
#include "puddlemap.hpp"
#include "reflect.hpp"
#include "renderpass.hpp"
#include "scene.hpp"
#include "screenshotinterface.hpp"
#include "shadow.hpp"
#include "terrainstorage.hpp"
#include "texturemipsdriver.hpp"
#include "thumbnailstate.hpp"
#include "water.hpp"
#include "weather.hpp"
#include "weathereffects.hpp"
#include "worldmap.hpp"
#include <components/extension/debug_draw_frame.hpp>
#include <components/extension/debug_draw_geometry.hpp>
#include <components/misc/constants.hpp>
#include <components/render/debuggeometry.hpp>
#include <components/render/gputiming.hpp>
#include <components/render/processmemory.hpp>
#include <components/render/simplewater.hpp>
#include <components/view/env.hpp>

namespace MWRender
{
    namespace
    {
        // The most point lights the view collects (View::CollectLights' capacity, and the size of the point lights'
        // shadow data buffer, which is indexed by the light's place in the light buffer).
        constexpr std::size_t sMaxLights = 256;

        // [WorldLod] view distance: a number or `auto`. Text that is neither counts as the old default number; a
        // number above Lod::maxViewDistanceNumber is cut to it. Both log a warning, once for each text: it repeats when
        // the setting changes to another such text, and again after a good value was set in between. Main thread only
        // (the constructor, `setStore` and the settings handler).
        Lod::ViewDistanceSetting readViewDistanceSetting()
        {
            static std::optional<std::string> lastWarnedText;
            const std::string& text = Settings::worldLod().mViewDistance.get();
            const auto parsed = Lod::parseViewDistanceSetting(text);
            if (parsed && !parsed->mClamped)
            {
                lastWarnedText.reset();
                return *parsed;
            }
            if (!lastWarnedText || *lastWarnedText != text)
            {
                lastWarnedText = text;
                if (parsed)
                {
                    Log(Debug::Warning) << "[WorldLod] view distance \"" << text << "\" is above the largest, using "
                                        << Lod::maxViewDistanceNumber;
                }
                else
                {
                    Log(Debug::Warning) << "[WorldLod] view distance \"" << text
                                        << "\" is neither a number nor auto; using " << Lod::legacyViewDistance;
                }
            }
            if (parsed)
            {
                return *parsed;
            }
            Lod::ViewDistanceSetting fallback;
            fallback.mValue = Lod::legacyViewDistance;
            return fallback;
        }

        // The four distances that were one number (task 0066). Without the world LOD and distant terrain they are
        // [Camera] viewing distance; with them on and `map view` off they are the larger of that and [WorldLod] view
        // distance (task 0055); `map view` takes them apart (Lod::computeViewLimits). `landDiagonal` is the loaded
        // world's reach for `view distance = auto`, 0 while unknown.
        Lod::ViewLimits currentViewLimits(double landDiagonal, const Lod::FarTierSettings* tier)
        {
            Lod::ViewLimitsInput input;
            input.mCameraViewingDistance = Settings::camera().mViewingDistance;
            input.mLodActive = Settings::worldLod().mEnabled && Settings::terrain().mDistantTerrain && !sLodBakeRun;
            if (input.mLodActive)
            {
                input.mMapView = Settings::worldLod().mMapView;
                input.mSetting = readViewDistanceSetting();
                input.mMaxViewDistance = tier
                    ? Lod::effectiveMaxViewDistance(Settings::worldLod().mMaxViewDistance, *tier)
                    : Settings::worldLod().mMaxViewDistance;
                input.mLandDiagonal = landDiagonal;
            }
            return Lod::computeViewLimits(input);
        }

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
            descriptors.emplace_back(View::dummyVolume());
            descriptors.emplace_back(View::dummyVolumeParams());
            descriptors.emplace_back(View::dummyCloudShadow());
            descriptors.emplace_back(View::dummyPointShadowMap());
            descriptors.emplace_back(View::dummyPointShadowSampler());
            descriptors.emplace_back(View::dummyPointShadowData());
            descriptors.emplace_back(View::dummyPuddleMap());

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
        , mFieldOfView(Settings::camera().mFieldOfView)
    {
        // The same condition as fog.settings' cAerialHaze (components/resource/shadersettings.cpp), at the same time.
        mAerialShader = Settings::worldLod().mEnabled && Settings::terrain().mDistantTerrain
            && Settings::worldLod().mMapView && Settings::worldLod().mHaze;
        chooseFarTier();
        // The projection does not exist yet; updateProjectionMatrix runs when it is made.
        applyViewLimits(currentViewLimits(0.0, farTierOrNull()), false);
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

        // OpenEden (task 0054, S0): extra columns for the frame stats file. Registered only when asked, so with the
        // setting off there is nothing to run and the file is as before. Read on the main thread, in this order.
        if (Settings::rendering().mFrameCounters)
        {
            Render::FrameCounters& counters = mEngine.frameCounters();
            counters.add("rss mib", [] { return static_cast<double>(Render::processWorkingSetBytes() >> 20); });
            counters.add("gpu used mib", [this] { return static_cast<double>(mEngine.deviceLocalUsageMiB()); });
            counters.add("pshadow faces",
                [this] { return mPointShadows ? static_cast<double>(mPointShadows->facesDrawn()) : 0.0; });
            counters.add("cells", [this] { return static_cast<double>(mLoadedExteriorCells.load()); });
            counters.add("cells loaded total", [this] { return static_cast<double>(mExteriorCellLoadsTotal.load()); });
            // The census walks the whole scene graph, so it runs every 30 frames and the totals are carried between
            // runs. scene_draws refreshes it and scene_triangles (read after it) only reports.
            counters.add("scene draws", [this] {
                if (mCensusCountdown == 0)
                {
                    mCensus.reset();
                    if (mSceneRoot)
                        mSceneRoot->accept(mCensus);
                    mCensusDraws = mCensus.draws();
                    mCensusTriangles = mCensus.triangles();
                    mCensusCountdown = 30;
                }
                --mCensusCountdown;
                return static_cast<double>(mCensusDraws);
            });
            counters.add("scene triangles", [this] { return static_cast<double>(mCensusTriangles); });
        }

        // OpenEden (task 0080): the video memory scale-back. Nothing is made, and no memory budget is read, unless on.
        // Task 0080 stage d: the texture mip swapper exists once the scene does (updateTextureMips).
        if (const char* mipsScript = std::getenv("OPENEDEN_TEXTURE_MIPS_TEST"))
        {
            mTextureMipsScript = mipsScript;
            mTextureMipsWanted = true;
        }
        if (Settings::videoMemory().mEnabled
            && Settings::videoMemory().mOrder.get().find("texture mips") != std::string::npos)
            mTextureMipsWanted = true;
        if (Settings::videoMemory().mEnabled)
            setupVideoMemory();

        mRenderModes.resize(Render_Count);
        // As OpenMW's RenderingManager (task 0012).
        Nif::ReadOptions nifOptions;
        nifOptions.mAllowedVersions
            = Settings::models().mLoadUnsupportedNifFiles ? Nif::VersionSet::Any : Nif::VersionSet::Morrowind;
        Nif::Reader::setDefaultReadOptions(nifOptions);
        // Task 0110 (neutral world, stage N3a-4): with the neutral world on, Skyrim SE models are read as scenes and drawn as
        // static shapes through the same node cache; off, the NIF reader refuses them as before.
        if (Settings::game().mNeutralWorld)
        {
            // Task 0136: Skyrim trees bend in the weather's wind; the driver's one uniform is bound by every swaying shape.
            if (Settings::game().mNeutralTreeSway)
            {
                mTreeSway = std::make_unique<SkyrimTreeSway::Driver>();
                // A developer aid for frame checks: OPENEDEN_NEUTRAL_TREE_WIND (0 to 1) replaces the weather's wind speed.
                if (const char* forced = std::getenv("OPENEDEN_NEUTRAL_TREE_WIND"); forced != nullptr && *forced)
                {
                    mTreeWindOverride = std::strtof(forced, nullptr);
                    Log(Debug::Info) << "Skyrim tree sway: the wind speed is forced to " << *mTreeWindOverride
                                     << " (OPENEDEN_NEUTRAL_TREE_WIND).";
                }
                Log(Debug::Info) << "Skyrim tree sway is on.";
            }
            resourceSystem->nifReader->sceneReader = SkyrimModel::makeSceneReader(*resourceSystem->builder,
                resourceSystem->textureOptions, Bin_DepthSorted, mTreeSway ? mTreeSway->uniform() : nullptr);
        }
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
        if (Settings::rendering().mTaa && !hdr)
            Log(Debug::Warning) << "[Rendering] taa needs hdr and gbuffer (it reads the stored depth and velocity); "
                                   "temporal anti-aliasing is off";
        // Task 0049: the runtime texture downscaler's limits, before any model or terrain texture is read.
        {
            vsgUtil::TextureCap::Caps caps;
            caps.global = Settings::rendering().mTextureMaxSize;
            caps.terrain = Settings::rendering().mTextureMaxSizeTerrain;
            caps.object = Settings::rendering().mTextureMaxSizeObjects;
            const std::string budget = Settings::rendering().mTextureMemoryBudget.get();
            const auto budgetSetting = vsgUtil::TextureCap::parseBudget(budget);
            if (!budgetSetting.valid)
                Log(Debug::Warning) << "[Rendering] texture memory budget: unknown '" << budget << "', using off";
            caps.autoBudget = budgetSetting.autoBudget;
            if (caps.autoBudget)
                caps.deviceLocalMiB
                    = budgetSetting.simulatedMiB ? budgetSetting.simulatedMiB : mEngine.deviceLocalMiB();
            vsgUtil::TextureCap::setCaps(caps);
            using vsgUtil::TextureCap::Category;
            const int objectCap = vsgUtil::TextureCap::effectiveCap(Category::Object, caps);
            const int terrainCap = vsgUtil::TextureCap::effectiveCap(Category::Terrain, caps);
            if (objectCap > 0 || terrainCap > 0)
                Log(Debug::Info) << "Texture cap: objects " << objectCap << ", terrain " << terrainCap
                                 << " texels (0 = none)"
                                 << (caps.autoBudget
                                            ? ", from " + std::to_string(caps.deviceLocalMiB) + " MiB device memory"
                                            : std::string());
            mTextureDownscaleReport = Settings::rendering().mTextureDownscaleReport;
        }
        // Task 0045: parallax occlusion mapping from the `_p` height files. Independent of hdr and pbr; before any
        // model loads (the loaders look for the files).
        if (Settings::rendering().mParallax)
        {
            const auto quality = Render::Parallax::parseQuality(Settings::rendering().mParallaxQuality.get());
            if (!quality)
                Log(Debug::Warning) << "[Rendering] parallax quality: unknown '"
                                    << Settings::rendering().mParallaxQuality.get() << "', using medium";
            mParallaxQuality = static_cast<unsigned int>(quality.value_or(Render::Parallax::Quality::Medium)) + 1;
            resourceSystem->builder->graphics->parallaxQuality = mParallaxQuality;
            auto& objectMaps = resourceSystem->nifReader->autoMaps;
            objectMaps.autoUseHeightMaps = true;
            objectMaps.parallaxDiscardEdges = Settings::rendering().mParallaxDiscardEdges;
            objectMaps.parallaxScale = Settings::rendering().mParallaxScale;
            Log(Debug::Info) << "Parallax: occlusion mapping from height files, quality "
                             << Settings::rendering().mParallaxQuality.get();
        }
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
            // OpenEden (task 0076): temporal accumulation of the occlusion. It generates the occlusion itself, in a
            // compute pass, in place of the fragment pass; the settings are read at start.
            if (Settings::rendering().mAoTemporal)
            {
                if (!Render::Ao::Temporal::useAoTemporal(true, ao, gbuffer))
                    Log(Debug::Warning) << "[Rendering] ao temporal needs ao (hdr, gbuffer and pbr); the accumulation "
                                           "is off";
                else if (Render::AoTemporalPass::shadersAvailable(resourceSystem->shaderOptions))
                    mAoTemporalEnabled = true;
            }
            // OpenEden (task 0075): temporal anti-aliasing. It needs the G-buffer (linear depth and velocity) and a
            // single-sampled scene (the averaged extra outputs of a multisampled target are wrong at silhouettes); the
            // user's antialiasing setting is never changed. The jitter is made with the camera, below.
            if (Settings::rendering().mTaa)
            {
                const auto samples = static_cast<std::uint32_t>(mEngine.framebufferSamples());
                if (!Render::Taa::useTaa(true, true, gbuffer, samples))
                {
                    if (!gbuffer)
                        Log(Debug::Warning) << "[Rendering] taa needs hdr and gbuffer (it reads the stored depth and "
                                               "velocity); temporal anti-aliasing is off";
                    else
                        Log(Debug::Warning) << "[Rendering] taa needs a single-sampled scene ([Video] antialiasing = "
                                               "0); temporal anti-aliasing is off";
                }
                else if (Render::TaaPass::shadersAvailable(resourceSystem->shaderOptions))
                    mTaaJitter = Render::Taa::JitterOffset::create();
            }
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
                    mReflectionStrength = Settings::rendering().mGiReflectionStrength;
                    Log(Debug::Info) << "GI: exterior ambient light from the baked sky-visibility field";
                }
                else
                    Log(Debug::Warning) << "[Rendering] gi exterior needs hdr, pbr and atmosphere; it is off";
            }
            // Task 0037: rain wetness acts in the material model's lighting, so it needs pbr.
            if (Settings::rendering().mWetness)
            {
                if (pbr)
                {
                    auto& graphics = *resourceSystem->builder->graphics;
                    graphics.wetnessEnabled = true;
                    graphics.wetnessPuddles = Settings::rendering().mWetnessPuddles;
                    graphics.wetnessDebug = Settings::rendering().mWetnessDebug;
                    mWetnessEnabled = true;
                    mWetnessStrength = Settings::rendering().mWetnessStrength;
                    Log(Debug::Info) << "Weather: rain wetness on";
                    // Task 0095: the terrain's puddles from the baked puddle map. The buffer exists from here on; it
                    // stays "off" (zero) until a pack is read, so the shader draws the noise puddles meanwhile.
                    if (Settings::rendering().mWetnessPuddleMap)
                    {
                        std::uint32_t debugMode = Render::PuddleMap::DebugOff;
                        if (!Settings::rendering().mWetnessPuddles)
                            Log(Debug::Warning) << "[Rendering] wetness puddle map needs wetness puddles; it is off";
                        else if (!Render::PuddleMap::parseDebugMode(
                                     Settings::rendering().mWetnessPuddleDebug.get(), debugMode))
                        {
                            Log(Debug::Warning) << "[Rendering] wetness puddle debug: unknown '"
                                                << Settings::rendering().mWetnessPuddleDebug.get()
                                                << "' (off, capacity, level or cell); using off";
                            mPuddleMapEnabled = true;
                        }
                        else
                            mPuddleMapEnabled = true;
                        mPuddleDebug = debugMode;
                        mPuddleKeepStaticNoise = Settings::rendering().mWetnessPuddleStaticNoise;
                        mPuddleTopographic = Settings::rendering().mWetnessPuddleTopographic;
                        mPuddleTopographicStrength = Settings::rendering().mWetnessPuddleTopographicStrength;
                        mPuddleSupply
                            = Settings::rendering().mWetnessPuddleSupply * Settings::rendering().mWetnessPuddleDepth;
                        mPuddleAbsorption = Settings::rendering().mWetnessPuddleAbsorption;
                        if (mPuddleMapEnabled)
                            Log(Debug::Info) << "Weather: puddles from the baked puddle map";
                    }
                }
                else
                    Log(Debug::Warning) << "[Rendering] wetness needs hdr and pbr; it is off";
            }
            // Task 0039: snow and ash cover acts in the material model's lighting too, so it needs pbr.
            if (Settings::rendering().mCover)
            {
                if (pbr)
                {
                    auto& graphics = *resourceSystem->builder->graphics;
                    graphics.coverEnabled = true;
                    graphics.coverDebug = Settings::rendering().mCoverDebug;
                    mCoverEnabled = true;
                    mCoverStrength = Settings::rendering().mCoverStrength;
                    Log(Debug::Info) << "Weather: snow and ash cover on";
                }
                else
                    Log(Debug::Warning) << "[Rendering] cover needs hdr and pbr; it is off";
            }
            // Task 0077: shadows of the nearest point lights. The lights are summed per pixel only with the material
            // model, which needs the G-buffer's material output.
            if (Settings::shadows().mPointLightShadows)
            {
                if (Settings::shaders().mClassicFalloff)
                    Log(Debug::Warning) << "[Shadows] point light shadows do not work with [Shaders] classic falloff "
                                           "(a light then reaches much farther than its shadow map); they are off";
                else if (materialOutput)
                {
                    mPointShadows = std::make_unique<PointShadows>(*context, sMaxLights);
                    resourceSystem->builder->graphics->pointShadowEnabled = true;
                    Log(Debug::Info) << "Shadows: point lights, " << mPointShadows->slotCount() << " at a time, "
                                     << Settings::shadows().mPointLightShadowResolution.get() << " texels a face";
                }
                else
                    Log(Debug::Warning)
                        << "[Shadows] point light shadows need hdr, gbuffer and pbr (the lights are then summed per "
                           "pixel); they are off";
            }
            // Task 0038: the clouds' shadows. The shader term is switched by `shadow.settings`
            // (Resource::ShaderSettings, from the same settings); here the data buffer is made real.
            if (Settings::rendering().mCloudShadows)
            {
                if (Render::Cloud::useShadows(true, true, Settings::rendering().mAtmosphere))
                {
                    mCloudShadowEnabled = true;
                    mCloudShadowStrength = Settings::rendering().mCloudShadowStrength;
                    mCloudShadowDebug = Settings::rendering().mCloudShadowDebug;
                    Log(Debug::Info) << "Weather: cloud shadows on";
                }
                else
                    Log(Debug::Warning) << "[Rendering] cloud shadows need hdr and atmosphere; they are off";
            }
            // Task 0040: lit rain and snow, and lightning as light. The sun glow is a shader setting
            // (Resource::ShaderSettings), which needs hdr and atmosphere.
            if (Settings::rendering().mPrecipitationLighting)
            {
                mPrecipitationLit = true;
                Log(Debug::Info) << "Weather: lit rain and snow on";
            }
            if (Settings::rendering().mLightning)
            {
                mLightningEnabled = true;
                mLightningStrength = Settings::rendering().mLightningStrength;
                mLightningDebugAge = Settings::rendering().mLightningDebugAge;
                Log(Debug::Info) << "Weather: lightning as light on (peak cap " << Render::Lightning::peakCap
                                 << ", flashes at least " << Render::Lightning::minInterval << " s apart)";
            }
            if (Settings::rendering().mSunGlow)
            {
                if (Settings::rendering().mAtmosphere)
                    Log(Debug::Info) << "Weather: sun glow in haze on";
                else
                    Log(Debug::Warning) << "[Rendering] sun glow needs hdr and atmosphere; it is off";
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
        auto collectLights = vsg::ref_ptr{ new View::CollectLights(sMaxLights) };
        collectLights->setLinear(mHdrTarget != nullptr);
        if (mPointShadows)
            collectLights->setShadowTarget(mPointShadows->maps().data(), Render::PointShadow::lightEntryOffset());
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
        // Task 0028, stage d: the same for the reflection probes (and, task 0036, the sky environment image).
        if (mGiEnabled || mGiSkyEnabled)
            descriptors.emplace_back(View::reflectionBuffer(mReflectionData));
        else
            descriptors.emplace_back(View::dummyReflections());
        // Task 0036: the exterior sky-visibility field (cells' data, then its header), or stand-ins that switch it off.
        // Task 0038: the clouds' shadows share the header's buffer, so it is real with them too; the cells' data is
        // not.
        if (mGiSkyEnabled)
            descriptors.emplace_back(View::skyVisBuffer(mSkyVisPool));
        else
            descriptors.emplace_back(View::dummySkyVis());
        if (mGiSkyEnabled || mCloudShadowEnabled)
            descriptors.emplace_back(View::skyVisSkyBuffer(mSkyVisHeader));
        else
            descriptors.emplace_back(View::dummySkyVisSky());
        // Task 0041: the volumetric fog's volume and parameter block, or stand-ins that say "off".
        if (Settings::rendering().mVolumetrics && !mHdrTarget)
            Log(Debug::Warning) << "[Rendering] volumetrics needs hdr (the fog is lit in linear space); the "
                                   "volumetric fog is off";
        if (Render::Volume::useVolumetrics(Settings::rendering().mVolumetrics, mHdrTarget != nullptr))
        {
            const auto quality = Render::Volume::parseQuality(Settings::rendering().mVolumetricsQuality.get());
            if (!quality)
                Log(Debug::Warning) << "[Rendering] volumetrics quality: unknown '"
                                    << Settings::rendering().mVolumetricsQuality.get() << "', using medium";
            const auto tier = quality.value_or(Render::Volume::Quality::Medium);
            mVolumeFog = std::make_unique<View::VolumeFog>(
                tier, Render::Volume::temporalEnabled(tier, Settings::rendering().mVolumetricsTemporal));
            descriptors.emplace_back(mVolumeFog->volumeDescriptor());
            descriptors.emplace_back(mVolumeFog->paramsDescriptor());
        }
        else
        {
            descriptors.emplace_back(View::dummyVolume());
            descriptors.emplace_back(View::dummyVolumeParams());
        }
        // Task 0042: the volumetric clouds are made here, before the view set, because their ground shadow map is one
        // of its bindings; their commands are built (and the sky given the picture) when the scene graph is.
        {
            const auto& rendering = Settings::rendering();
            if (rendering.mVolumetricClouds && !(mHdrTarget && rendering.mAtmosphere))
                Log(Debug::Warning) << "[Rendering] volumetric clouds needs hdr and atmosphere (they are lit in linear "
                                       "space by the weather's sun); the volumetric clouds are off";
            if (Render::CloudVolume::useVolumetricClouds(
                    rendering.mVolumetricClouds, mHdrTarget != nullptr, rendering.mAtmosphere)
                && View::CloudVolume::shadersAvailable(mResourceSystem->shaderOptions))
            {
                const auto quality = Render::CloudVolume::parseQuality(rendering.mVolumetricCloudsQuality.get());
                if (!quality)
                    Log(Debug::Warning) << "[Rendering] volumetric clouds quality: unknown '"
                                        << rendering.mVolumetricCloudsQuality.get() << "', using medium";
                const auto extent = mHdrTarget->extent();
                mCloudVolume
                    = std::make_unique<View::CloudVolume>(quality.value_or(Render::CloudVolume::Quality::Medium),
                        std::array<std::uint32_t, 2>{ extent.width, extent.height });
            }
        }
        descriptors.emplace_back(mCloudVolume ? mCloudVolume->shadowDescriptor() : View::dummyCloudShadow());
        // Task 0077: the point lights' shadow maps, or stand-ins that find no shadow.
        if (mPointShadows)
        {
            descriptors.emplace_back(mPointShadows->maps().shadowMap());
            descriptors.emplace_back(mPointShadows->maps().shadowSampler());
            descriptors.emplace_back(mPointShadows->maps().dataDescriptor());
        }
        else
        {
            descriptors.emplace_back(View::dummyPointShadowMap());
            descriptors.emplace_back(View::dummyPointShadowSampler());
            descriptors.emplace_back(View::dummyPointShadowData());
        }
        // Task 0095: the baked puddle map's window of cells, or a stand-in that says "no map".
        if (mPuddleMapEnabled)
            descriptors.emplace_back(View::puddleMapBuffer(mPuddleMapData));
        else
            descriptors.emplace_back(View::dummyPuddleMap());
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
        // Task 0075: the view draws with the jittered projection; everything else keeps the true camera.
        if (mTaaJitter)
            mRenderCamera = vsg::Camera::create(Render::Taa::JitteredProjection::create(mProjectionMatrix, mTaaJitter),
                mViewMatrix, mSceneCamera->viewportState);

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
        mRain->setLit(mPrecipitationLit);
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
        // The extensions' debug draw: lines only, refilled every frame it is on. Two geometries, because the depth test
        // is a property of a whole geometry here. Capacity: the worst case of DebugDrawFrame (about 103,000 lines) fits
        // either one. They hold no storage until they are enabled.
        constexpr std::size_t extensionDebugLineVertices = std::size_t(1) << 18;
        mExtensionDebugTested = std::make_unique<Render::DebugGeometry>(
            *mResourceSystem->builder, mCompileContext, extensionDebugLineVertices, 0, true);
        mExtensionDebugOnTop = std::make_unique<Render::DebugGeometry>(
            *mResourceSystem->builder, mCompileContext, extensionDebugLineVertices, 0, false);
        if ((mGiEnabled || mGiSkyEnabled) && Settings::rendering().mGiDebug)
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
        // Task 0048: with ao on and `ao terrain`, the terrain writes its ambient part (needs the ambient output).
        terrainMaps.aoTerrain = mAoEnabled && Settings::rendering().mAoTerrain;
        terrainMaps.pointShadow = mPointShadows != nullptr;
        terrainMaps.wetness.enabled = mWetnessEnabled;
        terrainMaps.wetness.puddles = mWetnessEnabled && Settings::rendering().mWetnessPuddles;
        terrainMaps.wetness.debug = mWetnessEnabled && Settings::rendering().mWetnessDebug;
        terrainMaps.cover.enabled = mCoverEnabled;
        terrainMaps.cover.debug = mCoverEnabled && Settings::rendering().mCoverDebug;
        const auto& shaders = Settings::shaders();
        terrainMaps.autoMaps.autoUseNormalMaps = shaders.mAutoUseTerrainNormalMaps;
        terrainMaps.autoMaps.autoUseSpecularMaps = shaders.mAutoUseTerrainSpecularMaps;
        terrainMaps.autoMaps.normalMapPattern = shaders.mNormalMapPattern;
        terrainMaps.autoMaps.normalHeightMapPattern = shaders.mNormalHeightMapPattern;
        terrainMaps.autoMaps.specularMapPattern = shaders.mTerrainSpecularMapPattern;
        // Task 0045: the terrain layers' height files (their displacement scales come from the material table, which
        // exists with pbr).
        if (mParallaxQuality > 0)
        {
            terrainMaps.autoMaps.autoUseHeightMaps = true;
            terrainMaps.parallaxQuality = mParallaxQuality;
            terrainMaps.parallaxScale = Settings::rendering().mParallaxScale;
            if (mResourceSystem->materialTable)
                terrainMaps.layerValues = [table = mResourceSystem->materialTable](std::string_view texturePath) {
                    return table->resolveValues("", texturePath);
                };
        }
        const VFS::Manager* vfs = mResourceSystem->getVFS();
        terrainMaps.exists = [vfs](std::string_view path) { return vfs->exists(VFS::Path::Normalized(path)); };
        if (mResourceSystem->materialTable && Settings::rendering().mPbrMaterialMaps)
        {
            // Task 0044: with the material model and its maps on, the terrain layers read `_rmaos` / `_orm` and `_n`
            // files next to their textures and are lit with the material model.
            terrainMaps.pbr = true;
            terrainMaps.autoMaps.autoUseMaterialMaps = true;
            terrainMaps.autoMaps.autoUseNormalMaps = true;
            terrainMaps.layerValues = [table = mResourceSystem->materialTable](std::string_view texturePath) {
                return table->resolveValues("", texturePath);
            };
        }
        if (Settings::worldLod().mEnabled)
            mTerrainMapsForLod = std::make_unique<Terrain::TerrainMapSettings>(terrainMaps);
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
        // Task 0041: the volumetric fog's passes have a bin and a command graph of their own, because they must run
        // after the shadow maps and the light grid (see the submit orders below) and before the scene.
        vsg::ref_ptr<vsgUtil::ComputeBin> volumeBin;
        if (mVolumeFog)
        {
            if (mVolumeFog->createCommands(mResourceSystem->shaderOptions, viewDescriptorSet))
            {
                viewRoot->children.insert(
                    viewRoot->children.begin() + 1, vsgUtil::createBinSetter(Bin_Volume, mVolumeFog->node()));
                volumeBin = vsgUtil::ComputeBin::create();
                volumeBin->binNumber = Bin_Volume;
                Log(Debug::Info) << "Volumetrics: a froxel volume of " << mVolumeFog->grid().x << " x "
                                 << mVolumeFog->grid().y << " x " << mVolumeFog->grid().z;
            }
            else
                mVolumeFog.reset();
        }
        // Task 0042: the volumetric clouds' passes, in a graph of their own that runs first (they read only the scene
        // block, which the CPU writes).
        vsg::ref_ptr<vsgUtil::ComputeBin> cloudBin;
        {
            const auto extent = mHdrTarget ? mHdrTarget->extent() : VkExtent2D{};
            if (mCloudVolume)
            {
                if (mCloudVolume->createCommands(mResourceSystem->shaderOptions, viewDescriptorSet))
                {
                    viewRoot->children.insert(
                        viewRoot->children.begin() + 1, vsgUtil::createBinSetter(Bin_Cloud, mCloudVolume->node()));
                    cloudBin = vsgUtil::ComputeBin::create();
                    cloudBin->binNumber = Bin_Cloud;
                    mSky->setVolumeClouds(mCloudVolume->resultDescriptor());
                    Log(Debug::Info) << "Volumetric clouds: marching " << mCloudVolume->marchSize()[0] << " x "
                                     << mCloudVolume->marchSize()[1] << " for a picture of " << extent.width << " x "
                                     << extent.height;
                }
                else
                    mCloudVolume.reset();
            }
        }
        mView = vsgUtil::createSharedView(mRenderCamera ? mRenderCamera : mSceneCamera, viewRoot);
        mView->viewDependentState = collectLights;
        mView->mask = ~Mask_GUI;
        if (mTaaJitter)
        {
            // Task 0075: blended draws (rain, snow, particles, glass) have no depth or velocity for the resolve to
            // follow, so they are drawn after it (the overlay graph below) and this bin only collects them.
            mTransparentBin = vsgUtil::DeferredBin::create(Bin_DepthSorted, vsg::Bin::DESCENDING);
            mView->bins.push_back(mTransparentBin);
        }
        else
            mView->bins.push_back(vsg::Bin::create(Bin_DepthSorted, vsg::Bin::DESCENDING));

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
            // Task 0072, stage g: the passes read their shader stages through the shader options.
            const auto& shaderOptions = mResourceSystem->shaderOptions;
            mTonemap = std::make_unique<Render::Tonemap>(
                mCompileContext->clone(~vsg::Mask(0)), mTonemapGraph, shaderOptions);
            if (Settings::rendering().mHdrBloom)
                mBloom
                    = std::make_unique<Render::Bloom>(*context, mCompileContext->clone(~vsg::Mask(0)), shaderOptions);
            // Task 0026, stage b: screen motion from the stored depth, with the G-buffer.
            if (mGBufferOutputs > 0)
                mVelocity = std::make_unique<Render::Velocity>(
                    *context, mCompileContext->clone(~vsg::Mask(0)), shaderOptions);
            // Task 0029: the occlusion pass, after the scene (it reads the scene's depth and normals). Its settings
            // (ao radius, ao slices, ao steps) reach the shader through ao.settings (Resource::ShaderSettings).
            if (mAoEnabled && !mAoTemporalEnabled)
                mAmbientOcclusion = std::make_unique<Render::AmbientOcclusion>(
                    *context, mCompileContext->clone(~vsg::Mask(0)), shaderOptions);
            // Task 0076: the accumulating occlusion pass takes its place; it follows the velocity pass (it reads it).
            if (mAoTemporalEnabled)
            {
                Render::Ao::Temporal::Settings aoTemporal;
                aoTemporal.history = Settings::rendering().mAoTemporalHistory;
                aoTemporal.clampGamma = Settings::rendering().mAoTemporalClamp;
                aoTemporal.radius = Settings::rendering().mAoRadius;
                aoTemporal.slices = Settings::rendering().mAoSlices;
                aoTemporal.steps = Settings::rendering().mAoSteps;
                const std::string aoDebug = Settings::rendering().mAoTemporalDebug.get();
                const auto parsedAoDebug = Render::Ao::Temporal::parseDebugView(aoDebug);
                if (!parsedAoDebug)
                    Log(Debug::Warning) << "[Rendering] ao temporal debug: unknown '" << aoDebug << "', using none";
                aoTemporal.debug = parsedAoDebug.value_or(Render::Ao::Temporal::DebugView::None);
                mAoTemporal = std::make_unique<Render::AoTemporalPass>(
                    mCompileContext->clone(~vsg::Mask(0)), shaderOptions, aoTemporal);
                Log(Debug::Info) << "AO: temporal accumulation, history " << aoTemporal.history << ", clamp "
                                 << aoTemporal.clampGamma << (mTaaJitter ? ", with the TAA state" : "");
            }
            // Task 0075: the temporal anti-aliasing resolve, between the occlusion pass and the bloom.
            if (mTaaJitter)
            {
                Render::Taa::Settings taa;
                taa.sharpness = Settings::rendering().mTaaSharpness;
                taa.jitterScale = Settings::rendering().mTaaJitterScale;
                taa.history = Settings::rendering().mTaaHistory;
                taa.clipGamma = Settings::rendering().mTaaClipGamma;
                const std::string clipping = Settings::rendering().mTaaClipping.get();
                const auto parsedClipping = Render::Taa::parseClipping(clipping);
                if (!parsedClipping)
                    Log(Debug::Warning) << "[Rendering] taa clipping: unknown '" << clipping << "', using minmax";
                taa.clipping = parsedClipping.value_or(Render::Taa::defaultClipping);
                const std::string debug = Settings::rendering().mTaaDebug.get();
                const auto parsedDebug = Render::Taa::parseDebugView(debug);
                if (!parsedDebug)
                    Log(Debug::Warning) << "[Rendering] taa debug: unknown '" << debug << "', using none";
                taa.debug = parsedDebug.value_or(Render::Taa::DebugView::None);
                taa.overlay = true;
                // The overlay draws the blended things with the unjittered camera against the jittered depth: their
                // pipelines get a slope-scaled depth bias of the jitter's size (the lava flip; before the first
                // pipeline is made).
                resourceSystem->builder->graphics->overlayDepthBiasSlope
                    = Render::Taa::overlayDepthBiasSlope(taa.jitterScale);
                mTaa = std::make_unique<Render::TaaPass>(
                    mCompileContext->clone(~vsg::Mask(0)), shaderOptions, taa, mTaaJitter);
                Log(Debug::Info) << "TAA: temporal anti-aliasing, " << Render::Taa::jitterPeriod
                                 << " jitter samples, history " << taa.history << ", clipping " << clipping;
            }
        }
        else
            mRenderGraph = mEngine.createRenderGraph();
        // The graph that has the window: the one the GUI pass and the extents follow.
        const vsg::ref_ptr<vsg::RenderGraph> windowGraph = mTonemapGraph ? mTonemapGraph : mRenderGraph;
        mRenderGraph->children = { mView };
        if (mTaa)
        {
            // Task 0075: the second render pass over the resolved picture. Its view replays the main view's
            // depth-sorted bin with the unjittered camera, so the streaks do not move with the jitter; the bins
            // are null because the elements' own bins are all collected by the main view.
            mOverlayGraph = vsg::RenderGraph::create();
            mOverlayGraph->windowResizeHandler = nullptr;
            auto overlayView = vsgUtil::createSharedView(mSceneCamera, vsgUtil::BinReplay::create(mTransparentBin));
            overlayView->mask = ~Mask_GUI;
            for (int bin = Bin_DepthSorted; bin <= Bin_Cloud; ++bin)
                overlayView->bins.push_back(vsgUtil::NullBin::create(bin));
            mOverlayGraph->children = { overlayView };
        }
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
        if (volumeBin)
        {
            // Submit order: the shadow maps (-3), then the light grid (-2), then the volume (-1), then the scene (0).
            // Graphs with the same order are submitted in whatever order their threads finish recording.
            computeBin->commandGraph->submitOrder = -2;
            volumeBin->commandGraph = mEngine.createCommandGraph(Pipeline::COMPUTE_SET);
            volumeBin->commandGraph->submitOrder = -1;
            if (mGpuTimer)
            {
                const std::size_t pass = mGpuTimer->addPass("volumetrics");
                volumeBin->timingBegin = mGpuTimer->beginNode(pass);
                volumeBin->timingEnd = mGpuTimer->endNode(pass);
            }
            mView->bins.push_back(volumeBin);
        }

        if (cloudBin)
        {
            // Submit order -4: before the shadow maps (-3), the light grid (-2), the fog volume (-1) and the scene (0).
            cloudBin->commandGraph = mEngine.createCommandGraph(Pipeline::COMPUTE_SET);
            cloudBin->commandGraph->submitOrder = -4;
            if (mGpuTimer)
            {
                const std::size_t pass = mGpuTimer->addPass("volumetric clouds");
                cloudBin->timingBegin = mGpuTimer->beginNode(pass);
                cloudBin->timingEnd = mGpuTimer->endNode(pass);
            }
            mView->bins.push_back(cloudBin);
        }

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
                mResourceSystem->textureOptions, mResourceSystem->shaderOptions, mRenderGraph->renderArea.extent.width,
                mRenderGraph->renderArea.extent.height);
        mCommandGraph->children = { View::postComputeBarrier(), mRenderGraph };
        // The first index after the scene (and its velocity and occlusion passes).
        const int afterScene
            = 2 + (mVelocity ? 1 : 0) + (mAmbientOcclusion ? 1 : 0) + (mAoTemporal ? 1 : 0) + (mTaa ? 2 : 0);
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
            if (mAoTemporal)
                mCommandGraph->children.insert(
                    mCommandGraph->children.begin() + 2 + (mVelocity ? 1 : 0), mAoTemporal->node());
            if (mTaa)
            {
                mCommandGraph->children.insert(mCommandGraph->children.begin() + afterScene - 2, mTaa->node());
                mCommandGraph->children.insert(mCommandGraph->children.begin() + afterScene - 1, mOverlayGraph);
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
        if (mAoTemporal)
            timeChild(mGpuTimer, mCommandGraph->children, mAoTemporal->node().get(), "ao temporal");
        if (mTaa)
        {
            timeChild(mGpuTimer, mCommandGraph->children, mTaa->node().get(), "taa");
            timeChild(mGpuTimer, mCommandGraph->children, mOverlayGraph.get(), "taa overlay");
        }
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
            castShadowGraph->submitOrder = mVolumeFog ? -3 : -1; // task 0041: before the light grid and the volume
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

        // Task 0077: one command graph for each point light slot; update() puts the six faces in it each frame.
        if (mPointShadows)
        {
            auto& maps = mPointShadows->maps();
            for (int slot = 0; slot < maps.slotCount; ++slot)
            {
                auto castGraph = mEngine.createCommandGraph(Pipeline::COMPUTE_SET);
                castGraph->submitOrder = mVolumeFog ? -3 : -1; // before the light grid and the scene, as the cascades
                for (int face = 0; face < Render::PointShadow::facesPerLight; ++face)
                {
                    auto rg = maps.renderGraph(slot, face);
                    auto view = maps.faceView(slot, face);
                    // The slot's bit tells a light's own model that this is its light's pass (Anim::Transform).
                    view->mask = mShadow->viewMask() | Anim::pointShadowSlotMask(slot);
                    view->bins = { vsg::Bin::create(Bin_DepthSorted, vsg::Bin::NO_SORT),
                        vsgUtil::NullBin::create(Bin_Compute) };
                    auto root = createCastShadowState();
                    root->children = { view };
                    rg->children = { root };
                    view->children = createPointShadowScene();
                    // In the graph from the start so that the viewer compiles the framebuffer; PointShadows::update
                    // replaces the children by the faces of the slot in use.
                    castGraph->children.push_back(rg);
                    if (slot == 0 && face == 0)
                        addCompileViews.emplace_back(
                            [this, rg, view] { mCompileContext->compileManager->add(*rg->framebuffer, view); });
                }
                mPointShadows->setGraph(slot, castGraph);
                // The timer table is small: the first slots are timed (the pass "point shadow <slot>").
                if (mGpuTimer && slot < 4)
                {
                    const std::size_t pass = mGpuTimer->addPass("point shadow " + std::to_string(slot));
                    mPointShadows->setSlotTimer(slot, mGpuTimer->beginNode(pass), mGpuTimer->endNode(pass));
                }
                threads.push_back(castGraph);
                mRecordShadow.push_back(castGraph->recordTraversal);
            }
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

        // OpenEden: screenshots (F12, Lua, --auto-screenshot) copy the window's image at the end of the GUI graph, the
        // last to write it in a frame (submit order 1, alone at it), before it is presented (Render::FrameCapture).
        mEngine.setScreenshotGraph(guiCommandGraph);
        // The save thumbnail: at the end of the main graph, after the inserts above, before the GUI graph.
        mThumbnailCapture = mEngine.addFrameCapture(mCommandGraph, Render::DownloadSync::InFrame);
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
            if (mAoTemporal
                && !mAoTemporal->setSource(mHdrTarget->linearDepthView(), mHdrTarget->normalView(), mVelocity->result(),
                    mHdrTarget->extent(),
                    [this](vsg::ref_ptr<vsg::Object> retired) { mCompileContext->detach(retired); }))
                throw std::runtime_error("The ambient occlusion accumulation pass could not be built");
            if (mTaa
                && !mTaa->setSource(mHdrTarget->colorView(), mHdrTarget->linearDepthView(), mVelocity->result(),
                    mHdrTarget->extent(),
                    [this](vsg::ref_ptr<vsg::Object> retired) { mCompileContext->detach(retired); }))
                throw std::runtime_error("The temporal anti-aliasing pass could not be built");
            if (mTaa && !rebuildTaaOverlay())
                throw std::runtime_error("The temporal anti-aliasing overlay could not be built");
            if (mBloom
                && !mBloom->setSource(*mHdrContext, sceneColourView(), mHdrTarget->extent(),
                    [this](vsg::ref_ptr<vsg::Object> retired) { mCompileContext->detach(retired); }))
                throw std::runtime_error("The bloom passes could not be built");
            if (!mTonemap->setSource(sceneColourView(), mBloom ? mBloom->result() : nullptr, mHdrTarget->extent(),
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
        if (mVramScaler && Settings::videoMemory().mReport)
            Log(Debug::Info) << mVramScaler->reportText();
        if (mTextureDownscaleReport)
            Log(Debug::Info) << vsgUtil::TextureResize::describe(vsgUtil::TextureResize::report().snapshot());
        if (mCompileOp)
            mCompileOp->wait();
    }

    void RenderManager::setStore(const MWWorld::ESMStore& store)
    {
        // Task 0066: the reach of the loaded world's land, for `[WorldLod] view distance = auto` with `map view`. It is
        // worked out whatever the settings say (a few thousand cells, once per store), so that turning `enabled` or
        // `map view` on later finds it (review of stages a and b: it used to stay 0 and `auto` fell back to 40000).
        mLandDiagonal = 0;
        {
            std::vector<std::pair<std::int32_t, std::int32_t>> cells;
            for (const ESM::Land& land : store.get<ESM::Land>())
            {
                cells.emplace_back(land.mX, land.mY);
            }
            mLandDiagonal = Lod::landDiagonal(Lod::landExtent(cells), ESM::Land::REAL_SIZE);
        }
        if (!mViewLimitsOverridden)
        {
            applyViewLimits(currentViewLimits(mLandDiagonal, farTierOrNull()), true);
        }
        // Task 0055: an earlier LodTerrain reads the storage on a worker thread; it goes before the store changes.
        mLodTerrain.reset();
        mLodClusters.reset();
        // Task 0095: the puddle map's worker reads the storage and the store too.
        releasePuddleMap();
        mLodClustersAttached = false;
        mLodClustersPushed = 0;
        mTerrainStorage->setStore(&store);
        mTerrain->setStorage(mTerrainStorage.get());
        if (mPuddleMapEnabled && !mPuddlePack.empty())
        {
            PuddleMapRuntime::Setup puddles;
            puddles.mPackPath = mPuddlePack;
            puddles.mStorage = mTerrainStorage.get();
            puddles.mStore = &store;
            puddles.mVfs = mResourceSystem->getVFS();
            puddles.mSupply = mPuddleSupply;
            puddles.mAbsorption = mPuddleAbsorption;
            puddles.mDebugMode = mPuddleDebug;
            puddles.mKeepStaticNoise = mPuddleKeepStaticNoise;
            puddles.mTopographic = mPuddleTopographic;
            puddles.mTopographicStrength = mPuddleTopographicStrength;
            mPuddleMap = std::make_unique<PuddleMapRuntime>(std::move(puddles));
        }
        // Task 0055: baked terrain tiles, only when asked for. Nothing below runs with the setting off.
        if (Settings::worldLod().mEnabled && mTerrainMapsForLod && !mLodTerrainPack.empty())
        {
            std::vector<std::pair<std::int32_t, std::int32_t>> landCells;
            for (const ESM::Land& land : store.get<ESM::Land>())
                landCells.emplace_back(land.mX, land.mY);
            mLodTerrainAttached = false;
            LodTerrain::Setup setup;
            setup.mPack = mLodTerrainPack;
            setup.mThreshold = mFarTierActive
                ? Lod::effectiveTileThreshold(Settings::worldLod().mTerrainTileThreshold, mFarTier)
                : Settings::worldLod().mTerrainTileThreshold;
            setup.mMaxLevel = mFarTierActive ? mFarLevels : 0;
            setup.mResidentBudgetBytes = mFarTierActive ? farTerrainBudgetBytes() : 0;
            setup.mLive = mTerrainStorage.get();
            setup.mLandCells = std::move(landCells);
            // The live layer list, taken here once (the staleness check on a worker thread must not ask for it).
            setup.mLayers = mTerrainStorage->getLayers();
            setup.mCompile = mCompileContext->clone(Mask_Terrain);
            setup.mImageOptions = mResourceSystem->textureOptions;
            setup.mShaderOptions = mResourceSystem->shaderOptions;
            setup.mSampler = mResourceSystem->builder->createSampler();
            setup.mMapSettings = *mTerrainMapsForLod;
            setup.mShareLayersFrom = &mTerrain->builder();
            setup.mVfs = mResourceSystem->getVFS();
            setup.mFadeSeconds = Settings::worldLod().mTerrainFadeTime;
            mLodTerrain = std::make_unique<LodTerrain>(std::move(setup));
        }
        // Task 0059: baked object clusters (DRAFT layout), only when asked for. Nothing below runs with the setting
        // off.
        if (Settings::worldLod().mEnabled && Settings::worldLod().mObjectClusters && !mLodClusterPack.empty())
        {
            LodClusters::Setup setup;
            setup.mPack = mLodClusterPack;
            setup.mThreshold = mFarTierActive
                ? Lod::effectiveTileThreshold(Settings::worldLod().mObjectClusterThreshold, mFarTier)
                : Settings::worldLod().mObjectClusterThreshold;
            setup.mMaxLevel = mFarTierActive ? mFarLevels : 0;
            setup.mFarTextureMaxSize = mFarTierActive ? mFarTextureSize : 0;
            setup.mResidentBudgetBytes = mFarTierActive ? farMemoryBudgetBytes() : 0;
            // Task 0060, option b: the minimal and low tiers may give up far clusters for the budget.
            setup.mEvictFar = mFarTierActive && mFarTier.mEvictFarClusters;
            setup.mFadeSeconds = Settings::worldLod().mObjectClusterFadeTime;
            setup.mPlayEffects = Settings::worldLod().mObjectClusterEffects;
            setup.mPlaySway = Settings::worldLod().mObjectClusterSway;
            setup.mSwayWindScale = Settings::worldLod().mObjectClusterSwayWind;
            setup.mSwayBoost = Settings::worldLod().mObjectClusterSwayBoost;
            setup.mCellSize = ESM::Land::REAL_SIZE;
            setup.mExpectedWorldspace = "Morrowind"; // what the object baker writes (task 0133 stage 2)
            setup.mBuilder = mResourceSystem->builder.get();
            setup.mImageOptions = mResourceSystem->textureOptions;
            setup.mCompile = mScene->context.compileContext;
            mLodClusters = std::make_unique<LodClusters>(std::move(setup));
            mLodClustersAttached = false;
        }
    }

    // Task 0080 (stage c): the systems the scale-back can reduce at run time are the live budgets of the world LOD's
    // far field. Each level halves the budget again (level 1 is half of the tier's, 2 a quarter, 3 an eighth). The
    // others the spec lists (point light shadows, volumetrics, cloud quality, history features, texture detail) are
    // made at start from settings and cannot be rebuilt at run time yet; they are not registered, so they are not in
    // the order (the settings reader names them as unknown).
    void RenderManager::setupVideoMemory()
    {
        namespace VM = Render::VideoMemory;
        const auto& settings = Settings::videoMemory();
        constexpr int farMaxLevel = 3;

        // Per adapter: the level it is at, and what was held when each level was entered, so that a restore costs
        // what the step down freed.
        struct FarState
        {
            int level = 0;
            std::uint64_t heldBeforeStep[farMaxLevel + 1] = {};
        };
        auto objectsState = std::make_shared<FarState>();
        auto terrainState = std::make_shared<FarState>();

        std::vector<VM::Adapter> adapters;
        if (mFarTierActive)
        {
            VM::Adapter objects;
            objects.name = "far objects";
            objects.maxLevel = farMaxLevel;
            objects.account = [this, objectsState](VM::Ledger& ledger) {
                if (!mLodClusters)
                {
                    ledger.set("lod.clusters", 0);
                    return;
                }
                // A rebuilt client starts at the tier's budget: put it back to this level.
                const std::uint64_t wanted = farMemoryBudgetBytes() >> objectsState->level;
                if (mLodClusters->residentBudget() != wanted)
                    mLodClusters->setResidentBudget(wanted);
                ledger.set("lod.clusters", static_cast<std::int64_t>(mLodClusters->stats().mHeldBytes));
            };
            objects.apply = [this, objectsState](int level) {
                if (!mLodClusters)
                    return false;
                if (level > objectsState->level)
                    objectsState->heldBeforeStep[level] = mLodClusters->stats().mHeldBytes;
                objectsState->level = level;
                mLodClusters->setResidentBudget(farMemoryBudgetBytes() >> level);
                return true;
            };
            objects.stepBytes = [this, objectsState](int from, bool down) -> std::int64_t {
                if (!mLodClusters)
                    return 0;
                const auto held = static_cast<std::int64_t>(mLodClusters->stats().mHeldBytes);
                if (down)
                    return std::max<std::int64_t>(
                        0, held - static_cast<std::int64_t>(farMemoryBudgetBytes() >> (from + 1)));
                return std::max<std::int64_t>(0, static_cast<std::int64_t>(objectsState->heldBeforeStep[from]) - held);
            };
            adapters.push_back(std::move(objects));

            VM::Adapter terrain;
            terrain.name = "far terrain";
            terrain.maxLevel = farMaxLevel;
            terrain.account = [this, terrainState](VM::Ledger& ledger) {
                if (!mLodTerrain)
                {
                    ledger.set("lod.tiles", 0);
                    return;
                }
                const std::uint64_t wanted = farTerrainBudgetBytes() >> terrainState->level;
                if (mLodTerrain->residentBudget() != wanted)
                    mLodTerrain->setResidentBudget(wanted);
                ledger.set("lod.tiles", static_cast<std::int64_t>(mLodTerrain->stats().mHeldBytes));
            };
            terrain.apply = [this, terrainState](int level) {
                if (!mLodTerrain)
                    return false;
                if (level > terrainState->level)
                    terrainState->heldBeforeStep[level] = mLodTerrain->stats().mHeldBytes;
                terrainState->level = level;
                mLodTerrain->setResidentBudget(farTerrainBudgetBytes() >> level);
                return true;
            };
            terrain.stepBytes = [this, terrainState](int from, bool down) -> std::int64_t {
                if (!mLodTerrain)
                    return 0;
                const auto held = static_cast<std::int64_t>(mLodTerrain->stats().mHeldBytes);
                if (down)
                    return std::max<std::int64_t>(
                        0, held - static_cast<std::int64_t>(farTerrainBudgetBytes() >> (from + 1)));
                return std::max<std::int64_t>(0, static_cast<std::int64_t>(terrainState->heldBeforeStep[from]) - held);
            };
            adapters.push_back(std::move(terrain));
        }
        else
            Log(Debug::Info) << "[VideoMemory] the far field is not active ([WorldLod] enabled, [Terrain] distant "
                                "terrain and map view are needed), so far objects and far terrain are not available";

        if (mTextureMipsWanted)
        {
            VM::Adapter mips;
            mips.name = "texture mips";
            mips.maxLevel = TextureMipsDriver::maxLevel;
            mips.account = [this](VM::Ledger& ledger) {
                if (mTextureMips)
                    mTextureMips->account(ledger);
            };
            mips.apply = [this](int level) {
                if (!mTextureMips)
                    return false;
                mTextureMips->setLevel(level);
                return true;
            };
            mips.stepBytes = [this](int level, bool down) -> std::int64_t {
                return mTextureMips ? mTextureMips->stepBytes(level, down) : 0;
            };
            adapters.push_back(std::move(mips));
        }

        VM::ScalerConfig config;
        config.params.highWater = settings.mHighWater;
        config.params.lowWater = settings.mLowWater;
        config.params.dwell = settings.mDwell;
        config.params.restoreDwell = settings.mRestoreDwell;
        const std::string restore = settings.mRestore.get();
        config.params.automaticRestore = restore != "manual";
        if (restore != "automatic" && restore != "manual")
            Log(Debug::Warning) << "[VideoMemory] restore: unknown '" << restore << "', using automatic";
        const std::string usageSource = settings.mUsageSource.get();
        config.useDriverUsage = usageSource != "ledger";
        if (usageSource != "driver" && usageSource != "ledger")
            Log(Debug::Warning) << "[VideoMemory] usage source: unknown '" << usageSource << "', using driver";
        config.sampleInterval = settings.mSampleInterval;
        config.budgetFraction = settings.mBudgetFraction;
        config.order = settings.mOrder.get();
        config.fallbackBudgetBytes = static_cast<std::int64_t>(mEngine.deviceLocalMiB()) << 20;
        const std::string budgetText = settings.mBudget.get();
        config.budget = VM::parseBudget(budgetText);
        if (!config.budget.valid)
        {
            Log(Debug::Warning) << "[VideoMemory] budget: unknown '" << budgetText << "', using auto";
            config.budget = VM::BudgetSetting{};
        }

        // After a reduction VSG's memory pools keep the freed blocks; hand the empty ones back to the driver (main
        // thread, from update(), a few seconds after the step, see Render::VideoMemory::PoolReleasePlanner).
        config.releasePools = [this] {
            VM::ReleaseResult result;
            const auto begin = std::chrono::steady_clock::now();
            const std::int64_t bytes = mEngine.releaseUnusedDeviceMemory();
            result.milliseconds
                = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
            result.bytes = std::max<std::int64_t>(bytes, 0);
            return result;
        };

        // Test aid: a scripted budget (OPENEDEN_VRAM_BUDGET_SCRIPT, see vrambudgetscript.hpp) stands in for the
        // driver's, so a reduce-then-restore cycle can be shown on a card that has room.
        std::vector<VM::BudgetStep> budgetScript;
        if (const char* scriptText = std::getenv("OPENEDEN_VRAM_BUDGET_SCRIPT"))
        {
            budgetScript = VM::parseBudgetScript(scriptText);
            if (budgetScript.empty())
                Log(Debug::Warning) << "[VideoMemory] OPENEDEN_VRAM_BUDGET_SCRIPT is not valid and is ignored";
        }
        const auto scriptStart = std::chrono::steady_clock::now();

        mVramScaler = std::make_unique<VM::Scaler>(
            std::move(config), std::move(adapters),
            [this, budgetScript, scriptStart] {
                VM::Sample sample = mEngine.deviceLocalMemorySample();
                if (!budgetScript.empty())
                {
                    const double seconds
                        = std::chrono::duration<double>(std::chrono::steady_clock::now() - scriptStart).count();
                    const std::int64_t mib = VM::budgetAt(budgetScript, seconds);
                    if (mib > 0)
                        sample.budgetBytes = mib << 20;
                }
                return sample;
            },
            [](bool warning, const std::string& text) { Log(warning ? Debug::Warning : Debug::Info) << text; });
        if (!mVramScaler->active())
        {
            mVramScaler.reset();
            return;
        }
        Log(Debug::Info) << "[VideoMemory] scale-back is on, order: " << settings.mOrder.get();
        if (Settings::rendering().mFrameCounters)
        {
            Render::FrameCounters& counters = mEngine.frameCounters();
            counters.add("vram budget mib", [this] { return mVramScaler ? mVramScaler->budgetMiB() : -1.0; });
            counters.add("vram used mib", [this] { return mVramScaler ? mVramScaler->usedMiB() : -1.0; });
            counters.add("vram accounted mib", [this] { return mVramScaler ? mVramScaler->accountedMiB() : -1.0; });
            counters.add(
                "vram level", [this] { return mVramScaler ? static_cast<double>(mVramScaler->levelCount()) : 0.0; });
        }
    }

    // Task 0066 stage e: the hardware tier of the whole-map view. Only with `map view` on (and the world LOD and
    // distant terrain): otherwise nothing here is looked at and every number stays the setting's own.
    std::uint64_t RenderManager::farMemoryBudgetBytes() const
    {
        const int setting = Settings::worldLod().mFarMemoryBudget;
        return setting > 0 ? static_cast<std::uint64_t>(setting) << 20 : mFarTier.mResidentBudgetBytes;
    }

    // The terrain tiles' share of the far memory budget (task 0060): a quarter of the setting when it is given, on
    // top of it (so `far memory budget = N` holds 1.25 N in all), the tier's own number otherwise (the tiers' tile
    // budgets are far smaller than their cluster budgets: Lod::farTierSettings). The budget governs only the tiles
    // inside the live range; those past it are drawn whatever it says (second review).
    std::uint64_t RenderManager::farTerrainBudgetBytes() const
    {
        const int setting = Settings::worldLod().mFarMemoryBudget;
        return setting > 0 ? (static_cast<std::uint64_t>(setting) << 20) / 4 : mFarTier.mTerrainBudgetBytes;
    }

    void RenderManager::chooseFarTier()
    {
        mFarTierActive = Settings::worldLod().mEnabled && Settings::terrain().mDistantTerrain
            && Settings::worldLod().mMapView && !sLodBakeRun;
        if (!mFarTierActive)
        {
            return;
        }
        Lod::HardwareReport report;
        report.mDeviceMemoryBytes = mEngine.deviceLocalMiB() << 20;
        report.mSystemMemoryBytes = Render::physicalMemoryBytes();
        report.mSoftwareDevice = mEngine.deviceIsSoftware();
        const std::string& text = Settings::worldLod().mTier.get();
        const Lod::FarTierSetting setting = Lod::parseFarTierSetting(text);
        if (!setting.mValid)
        {
            Log(Debug::Warning) << "[WorldLod] tier \"" << text
                                << "\" is not auto, minimal, low, medium or high; using auto";
        }
        mFarTierName = Lod::resolveFarTier(setting, report);
        mFarTier = Lod::farTierSettings(mFarTierName);
        mFarLevels = Lod::resolveFarLevels(Settings::worldLod().mFarLevels, mFarTier.mFarLevels);
        mFarTextureSize = static_cast<int>(
            Lod::resolveFarTextureSize(Settings::worldLod().mFarTextureMaxSize, mFarTier.mFarTextureMaxSize));
        static constexpr const char* tierNames[] = { "minimal", "low", "medium", "high" };
        Log(Debug::Info) << "World LOD tier: " << tierNames[static_cast<int>(mFarTierName)] << " ("
                         << (setting.mAuto ? "from the device: " : "named: ") << (report.mDeviceMemoryBytes >> 20)
                         << " MiB device memory, " << (report.mSystemMemoryBytes >> 20) << " MiB system memory"
                         << (report.mSoftwareDevice ? ", a software device" : "") << "), far levels " << mFarLevels
                         << ", tile threshold at least " << mFarTier.mTileThreshold << " px, view distance cap "
                         << Lod::effectiveMaxViewDistance(Settings::worldLod().mMaxViewDistance, mFarTier)
                         << ", far cluster pictures " << mFarTextureSize << " px.";
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

    vsg::Group::Children RenderManager::createPointShadowScene()
    {
        // The same four settings as the sun's; objects also cast indoors, where most of the point lights are.
        vsg::Group::Children children;
        if (Settings::shadows().mTerrainShadows)
            children.push_back(mTerrainView->node());
        if (Settings::shadows().mObjectShadows)
            children.push_back(mScene->getObjects());
        if (Settings::shadows().mActorShadows)
            children.push_back(mScene->getActors());
        if (Settings::shadows().mPlayerShadows)
            children.push_back(mPlayerGroup);
        return children;
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
        if (mLodTerrain && mRenderModes[Render_Terrain])
            mSceneRoot->addChild(mLodTerrain->node());
        mSceneRoot->addChild(mPlayerGroup);
        mSceneRoot->addChild(mScene->getObjects());
        if (mGroundcover)
            mSceneRoot->addChild(mGroundcover->node());
        if (mDistantObjects)
            mSceneRoot->addChild(mDistantObjects->node());
        if (mLodClusters)
            mSceneRoot->addChild(mLodClusters->node());
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
        {
            // With TAA the rain joins the bin that is drawn after the resolve.
            if (mTransparentBin)
                mSceneRoot->addChild(vsgUtil::createBinSetter(Bin_DepthSorted, mRain->node()));
            else
                mSceneRoot->addChild(mRain->node());
        }
        mSceneRoot->addChild(mDebugGeometry->node());
        mSceneRoot->addChild(mNavMeshGeometry->node());
        mSceneRoot->addChild(mExtensionDebugTested->node());
        mSceneRoot->addChild(mExtensionDebugOnTop->node());
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

    void RenderManager::setCover(int kind, float amount)
    {
        mCoverKind = static_cast<Render::Cover::Kind>(std::clamp(kind, 0, 2));
        mCoverAmount = Render::Wetness::saturate(amount);
    }

    void RenderManager::setLightning(float intensity, const vsg::vec3& direction)
    {
        if (!mLightningEnabled)
            return;
        // The cap is enforced where the flash is made (Render::Lightning::peak); it is repeated here so that no caller
        // can pass it.
        mLightningIntensity = intensity > 0.f ? std::min(intensity, Render::Lightning::peakCap) : 0.f;
        mLightningDirection = direction;
    }

    void RenderManager::setImageSpaceStops(float stops)
    {
        mImageSpaceStops = std::isfinite(stops) ? std::clamp(stops, -2.f, 2.f) : 0.f;
    }

    void RenderManager::setWetness(float wetness)
    {
        mWetness = Render::Wetness::saturate(wetness);
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
        if (!mReflectionData || mSkyEnvOwns)
            return; // the sky environment image owns the buffer outdoors; updateSkyEnvironment switches it off
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
        mSkyEnvOwns = false;
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
        ++mLoadedExteriorCells; // task 0054 counters
        ++mExteriorCellLoadsTotal;
        if (!mGiSkyEnabled)
            return;
        std::lock_guard<std::mutex> lock(mSkyVisMutex);
        mSkyCells.insert({ x, y });
        mSkyCellsChanged = true;
    }

    void RenderManager::exteriorCellUnloaded(int x, int y)
    {
        --mLoadedExteriorCells;
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

    void RenderManager::updatePuddleMap()
    {
        // Task 0095: the window of baked puddle cells round the camera. The worker packs it; the render thread only
        // names the camera's cell and copies a finished buffer (no allocation).
        if (!mPuddleMap || !mPuddleMapData)
            return;
        const vsg::dvec3 eye = mViewMatrix->eye;
        mPuddleMap->setCamera(static_cast<std::int32_t>(std::floor(eye.x / Constants::CellSizeInUnits)),
            static_cast<std::int32_t>(std::floor(eye.y / Constants::CellSizeInUnits)));
        if (mPuddleMap->takeNewest(mPuddleMapData->data(), mPuddleMapData->size()))
            mPuddleMapData->dirty();
    }

    void RenderManager::updateCloudShadow()
    {
        if (!mCloudShadowEnabled || !mSkyVisHeader)
            return;
        // Task 0038: the clouds' picture grids, scroll, rotations and the weather's share go into the end of the
        // header. Indoors, or with no clouds, the strength is 0 and the shader returns at once. The scroll is the sky's
        // own (game time, advanced in Sky::update), so the shadow follows the clouds overhead.
        Render::Cloud::Params params;
        const Sky::CloudShadowInput input
            = mRenderModes[Render_Sky].active ? mSky->getCloudShadow() : Sky::CloudShadowInput{};
        vsg::vec4* header = mSkyVisHeader->data();
        if (input.valid)
        {
            // The sun's light is dim at night (the manager's mirrored light), and then there are no shadows (spec D6).
            const vsg::vec4& sunLight = mSceneData->value().lightDiffuse;
            const float sunLuminance = Render::Cloud::luminance(sunLight.x, sunLight.y, sunLight.z);
            params.strength
                = mCloudShadowDebug ? 1.f : mCloudShadowStrength * Render::Cloud::strength(sunLuminance, input.share);
            params.blend = input.blend;
            // The grids wrap, so only the fraction counts, and a float that has grown over hours keeps its precision.
            params.scroll = input.scroll - std::floor(input.scroll);
            for (int layer = 0; layer < 2; ++layer)
            {
                params.rotation[layer] = input.rotation[layer];
                params.hasLayer[layer] = input.grid[layer] != nullptr;
            }
        }
        // The grids are copied only when the weather's pictures change.
        bool gridsDirty = false;
        for (std::size_t layer = 0; layer < 2; ++layer)
            if (mCloudGridWritten[layer] != input.grid[layer] || !mCloudShadowWritten)
            {
                Render::Cloud::writeGrid(header, layer, input.grid[layer]);
                mCloudGridWritten[layer] = input.grid[layer];
                gridsDirty = true;
            }
        // The whole header is uploaded when it is dirtied (about 34 KB), and the scroll moves a few thousandths of a
        // picture a second, far less than a grid square (1 / 64) in a tenth of a second: a change smaller than
        // `Render::Cloud::scrollStep` alone is not worth an upload.
        const bool gridsChanged = !mCloudShadowWritten;
        const auto& last = mCloudParamsWritten;
        const bool same = !gridsChanged && last.strength == params.strength && last.blend == params.blend
            && last.rotation[0] == params.rotation[0] && last.rotation[1] == params.rotation[1]
            && last.hasLayer[0] == params.hasLayer[0] && last.hasLayer[1] == params.hasLayer[1]
            && std::abs(last.scroll - params.scroll) < Render::Cloud::scrollStep;
        mCloudShadowWritten = true;
        if (same && !gridsDirty)
            return;
        mCloudParamsWritten = params;
        Render::Cloud::writeParams(header, params);
        mSkyVisHeader->dirty();
    }

    void RenderManager::updateSkyEnvironment()
    {
        if (!mGiSkyEnabled || !mReflectionData)
            return;
        bool outdoors;
        {
            std::lock_guard<std::mutex> lock(mSkyVisMutex);
            outdoors = !mSkyCells.empty();
        }
        if (!outdoors)
        {
            if (mSkyEnvOwns)
            {
                // Left the exterior: the buffer is free again (an interior's own reflection probes load themselves).
                mSkyEnvOwns = false;
                (*mReflectionData)[0].w = 0.f;
                mReflectionData->dirty();
            }
            return;
        }
        const vsg::vec4 skyColour = mDisplaySkyColor;
        const vsg::vec4 horizon = mFog->getFogColor(false);
        const vsg::vec3 zenithLinear = Render::Hdr::toLinear(vsg::vec3(skyColour.x, skyColour.y, skyColour.z));
        const vsg::vec3 horizonLinear = Render::Hdr::toLinear(vsg::vec3(horizon.x, horizon.y, horizon.z));
        const vsg::vec3 ambient = getLinearAmbient();
        // Rebuilt only when the sky has moved (the hour or the weather), not every frame.
        if (mSkyEnvOwns
            && !Render::Gi::skyEnvironmentChanged(mSkyEnvZenith, mSkyEnvHorizon, zenithLinear, horizonLinear))
        {
            // The image is still right. The ambient light drifts with the hour: only the header's copy of it (what
            // the lookup compares the current ambient with) is patched, with the same tolerance, without repacking.
            if (Render::Gi::skyEnvironmentChanged(mSkyEnvAmbient, mSkyEnvAmbient, ambient, ambient))
            {
                mSkyEnvAmbient = ambient;
                (*mReflectionData)[1] = vsg::vec4(mReflectionStrength, ambient.x, ambient.y, ambient.z);
                mReflectionData->dirty();
            }
            return;
        }
        mSkyEnvZenith = zenithLinear;
        mSkyEnvHorizon = horizonLinear;
        mSkyEnvAmbient = ambient;
        Render::Gi::ReflectionSet set;
        set.cell = Render::Gi::CellId{ false, std::string(), 0, 0 };
        set.samples = 1;
        set.resolution = Render::Gi::skyEnvironmentResolution;
        // The reflection lookup adds what the ambient light has gained since the bake; this one is made now.
        set.ambient = ambient;
        set.probes.push_back(Render::Gi::makeSkyEnvironmentProbe(
            zenithLinear, horizonLinear, horizonLinear * Render::Gi::groundSkyFactor, set.resolution));
        const auto packed = Render::Gi::packReflections(set, mReflectionStrength);
        if (packed.size() > mReflectionData->size())
            return;
        std::copy(packed.begin(), packed.end(), mReflectionData->begin());
        // Marks the buffer as the sky image, so the object shader can scale its reflection by sky visibility and
        // roughness (reflectiongpu.hpp, skyReflectionFactor); an interior's own probes leave this at 0.
        (*mReflectionData)[2].x = 1.f;
        mReflectionData->dirty();
        mSkyEnvOwns = true;
    }

    void RenderManager::updateSkyDebug()
    {
        if (!mProbeGeometry || !mGiSkyEnabled)
            return;
        // The sky-visibility markers around the camera, refilled when the camera moves to another column (`gi debug`).
        const vsg::vec3 eye(mViewMatrix->eye);
        const int cellX = static_cast<int>(std::floor(eye.x / Render::Gi::exteriorCellSize));
        const int cellY = static_cast<int>(std::floor(eye.y / Render::Gi::exteriorCellSize));
        const auto found = mSkyFields.find({ cellX, cellY });
        const Render::Gi::SkyVisField* field = found != mSkyFields.end() ? found->second.get() : nullptr;
        if (!field)
        {
            // Clear what was drawn, unless an interior's own markers are up.
            if (mSkyDebugDrawn && !mLoadedVolume && !mLoadedReflections)
            {
                mProbeGeometry->begin();
                mProbeGeometry->end();
            }
            mSkyDebugDrawn = false;
            return;
        }
        const std::array<int, 4> key{ cellX, cellY, static_cast<int>(std::floor(eye.x / field->spacing)),
            static_cast<int>(std::floor(eye.y / field->spacing)) };
        if (mSkyDebugDrawn && key == mSkyDebugKey && field == mSkyDebugField)
            return;
        mSkyDebugKey = key;
        mSkyDebugField = field;
        mSkyDebugDrawn = true;
        mProbeGeometry->setEnabled(true);
        mProbeGeometry->begin();
        Render::Gi::drawSkyVisibility(*mProbeGeometry, *field, eye, Render::Gi::skyVisDebugRadius);
        mProbeGeometry->end();
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

    bool RenderManager::useNeutralTerrain(
        openeden::sdk::game::WorldCellSource& source, std::uint64_t world, std::string& error)
    {
        if (mNeutralStorage != nullptr && mNeutralWorld == world && mNeutralGame == source.gameId())
            return true;
        // The cache keeps the cells of the window round the camera; a long view distance asks for more than the default
        // 1,024 (terrainEnsureWindow gives 42 x 42 at 10 cells).
        NeutralTerrain::CacheLimits limits;
        limits.mMaxCells = 4096;
        // Task 0149: the alpha layers of the quadrants are drawn (and kept in the cache) only with this setting.
        const bool terrainLayers = Settings::game().mNeutralTerrainLayers;
        limits.mKeepLayers = terrainLayers;
        auto cache = std::make_shared<NeutralTerrain::CellCache>(source, limits);
        if (!cache->setWorld(world, error))
            return false;
        auto storage = std::make_unique<NeutralTerrain::Storage>(cache);
        storage->setLayerSplat(terrainLayers);

        // Everything that reads the old storage goes first (as in setStore): the LOD tiles and clusters and the puddle
        // map read it on worker threads. Then the terrain forgets its view and its chunks, which were cut from the
        // old land, and is pointed at the new storage (its layer arrays are rebuilt on a worker).
        mLodTerrain.reset();
        mLodClusters.reset();
        releasePuddleMap();
        mLodClustersAttached = false;
        mLodClustersPushed = 0;
        mTerrain->clearView(*mTerrainView);
        mTerrain->clearCache();
        mTerrain->setStorage(storage.get());
        mNeutralStorage = std::move(storage);
        mNeutralCache = std::move(cache);
        mNeutralGame = std::string(source.gameId());
        mNeutralWorld = world;
        mNeutralWindow = {};
        Log(Debug::Info) << "Neutral terrain: " << mNeutralGame << " world " << world << " ("
                         << mNeutralCache->world().editorId << "), cells of " << mNeutralStorage->cellWorldSize
                         << " units with " << mNeutralStorage->cellVertices << " vertices, "
                         << mNeutralCache->textures().size() << " ground textures"
                         << (terrainLayers ? ", alpha layers on" : "");
        startNeutralLodTerrain(source);
        startNeutralLodClusters(source);
        return true;
    }

    void RenderManager::startNeutralLodClusters(openeden::sdk::game::WorldCellSource& source)
    {
        if (!Settings::worldLod().mEnabled || !Settings::worldLod().mObjectClusters || mLodRoot.empty()
            || !mNeutralCache || !mNeutralStorage)
            return;
        const Lod::NeutralWorldFacts facts = neutralWorldFacts(source, mNeutralCache->world());
        const std::filesystem::path pack = Lod::objectPackPath(mLodRoot, facts.mGameId, facts.mWorldEditorId);
        std::error_code ec;
        if (!std::filesystem::exists(pack, ec))
        {
            Log(Debug::Info) << "World LOD clusters: no pack for " << facts.mGameId << " world " << facts.mWorldEditorId
                             << " (" << Files::pathToUnicodeString(pack) << "); make one with openmw --bake-lod-objects.";
            return;
        }
        LodClusters::Setup setup;
        setup.mPack = pack;
        setup.mExpectedWorldspace = Lod::neutralWorldspaceName(facts);
        setup.mThreshold = mFarTierActive
            ? Lod::effectiveTileThreshold(Settings::worldLod().mObjectClusterThreshold, mFarTier)
            : Settings::worldLod().mObjectClusterThreshold;
        setup.mMaxLevel = mFarTierActive ? mFarLevels : 0;
        setup.mFarTextureMaxSize = mFarTierActive ? mFarTextureSize : 0;
        setup.mResidentBudgetBytes = mFarTierActive ? farMemoryBudgetBytes() : 0;
        setup.mEvictFar = mFarTierActive && mFarTier.mEvictFarClusters;
        setup.mFadeSeconds = Settings::worldLod().mObjectClusterFadeTime;
        setup.mPlayEffects = Settings::worldLod().mObjectClusterEffects;
        setup.mPlaySway = Settings::worldLod().mObjectClusterSway;
        setup.mSwayWindScale = Settings::worldLod().mObjectClusterSwayWind;
        setup.mSwayBoost = Settings::worldLod().mObjectClusterSwayBoost;
        setup.mCellSize = mNeutralStorage->cellWorldSize;
        setup.mBuilder = mResourceSystem->builder.get();
        setup.mImageOptions = mResourceSystem->textureOptions;
        setup.mCompile = mScene->context.compileContext;
        mLodClusters = std::make_unique<LodClusters>(std::move(setup));
        mLodClustersAttached = false;
        Log(Debug::Info) << "World LOD clusters: using " << Files::pathToUnicodeString(pack) << " for " << facts.mGameId
                         << " world " << facts.mWorldEditorId;
    }

    void RenderManager::startNeutralLodTerrain(openeden::sdk::game::WorldCellSource& source)
    {
        if (!Settings::worldLod().mEnabled || !mTerrainMapsForLod || mLodRoot.empty() || !mNeutralCache)
            return;
        const Lod::NeutralWorldFacts facts = neutralWorldFacts(source, mNeutralCache->world());
        const std::filesystem::path pack = Lod::terrainPackPath(mLodRoot, facts.mGameId, facts.mWorldEditorId);
        std::error_code ec;
        if (!std::filesystem::exists(pack, ec))
        {
            Log(Debug::Info) << "World LOD terrain: no pack for " << facts.mGameId << " world " << facts.mWorldEditorId
                             << " (" << Files::pathToUnicodeString(pack) << "); make one with openmw --bake-lod-terrain.";
            return;
        }
        mLodTerrainAttached = false;
        mNeutralExtentPending = true;
        LodTerrain::Setup setup;
        setup.mPack = pack;
        setup.mNeutralWorldspaceName = Lod::neutralWorldspaceName(facts);
        setup.mThreshold = mFarTierActive ? Lod::effectiveTileThreshold(Settings::worldLod().mTerrainTileThreshold, mFarTier)
                                          : Settings::worldLod().mTerrainTileThreshold;
        setup.mMaxLevel = mFarTierActive ? mFarLevels : 0;
        setup.mResidentBudgetBytes = mFarTierActive ? farTerrainBudgetBytes() : 0;
        setup.mLive = mNeutralStorage.get();
        setup.mLayers = mNeutralStorage->getLayers();
        setup.mCompile = mCompileContext->clone(Mask_Terrain);
        setup.mImageOptions = mResourceSystem->textureOptions;
        setup.mShaderOptions = mResourceSystem->shaderOptions;
        setup.mSampler = mResourceSystem->builder->createSampler();
        setup.mMapSettings = *mTerrainMapsForLod;
        setup.mShareLayersFrom = &mTerrain->builder();
        setup.mVfs = mResourceSystem->getVFS();
        setup.mFadeSeconds = Settings::worldLod().mTerrainFadeTime;
        mLodTerrain = std::make_unique<LodTerrain>(std::move(setup));
        Log(Debug::Info) << "World LOD terrain: using " << Files::pathToUnicodeString(pack) << " for "
                         << facts.mGameId << " world " << facts.mWorldEditorId;
    }

    std::shared_ptr<const openeden::sdk::game::TerrainCell> RenderManager::neutralTerrainCell(
        std::int32_t x, std::int32_t y) const
    {
        return mNeutralCache ? mNeutralCache->get(x, y) : nullptr;
    }

    float RenderManager::neutralTerrainCellSize() const
    {
        return mNeutralStorage ? mNeutralStorage->cellWorldSize : 0.f;
    }

    void RenderManager::ensureNeutralTerrain(const vsg::dvec3& eye)
    {
        if (!mNeutralCache)
            return;
        const NeutralTerrain::CellWindow window = NeutralTerrain::terrainEnsureWindow(
            eye.x, eye.y, mViewDistance, mNeutralStorage->cellWorldSize);
        if (window.empty() || window == mNeutralWindow)
            return;
        Misc::LoadTrace::Scope trace("terrain.neutral.ensure", window.count());
        const std::uint64_t unloadedBefore = mNeutralCache->unloadedReads();
        const std::uint64_t builtBefore = mNeutralCache->builtCells();
        mNeutralCache->ensure(window.mX0, window.mY0, window.mX1, window.mY1);
        mNeutralWindow = window;
        Log(Debug::Verbose) << "Neutral terrain: cells (" << window.mX0 << ", " << window.mY0 << ") to (" << window.mX1
                            << ", " << window.mY1 << ") ensured, " << (mNeutralCache->builtCells() - builtBefore)
                            << " built, " << mNeutralCache->resident() << " resident, " << unloadedBefore
                            << " chunk reads of cells that were not loaded so far";
    }

    void RenderManager::updateTerrain(const vsg::dvec3& pos, float dt)
    {
        ensureNeutralTerrain(pos);
        // Task 0089: the layer arrays are built on a thread of their own; the main thread does not wait for them, the
        // terrain follows in the first frame after they are done.
        if (!mTerrain->layersReady())
            return;
        const Terrain::Coverage* coverage = nullptr;
        if (mLodTerrain)
        {
            if (!mLodTerrainAttached)
            {
                mLodTerrainAttached = true;
                updateSceneChildren();
            }
            LodTerrain::Camera camera;
            camera.mEye = mViewMatrix->eye;
            camera.mForward = vsg::normalize(mViewMatrix->center - mViewMatrix->eye);
            camera.mFovYDegrees = mProjectionMatrix->fieldOfViewY;
            camera.mAspect = mProjectionMatrix->aspectRatio;
            camera.mViewportHeight = mAppliedExtent.height;
            camera.mViewDistance = mTileRange;
            camera.mPagingRange = mViewDistance; // what the live terrain and objects reach (task 0060)
            camera.mDeltaSeconds = dt; // task 0087: the tile fade; 0 for the cell grid change between frames
            coverage = mLodTerrain->update(camera);
            if (mNeutralExtentPending && mLodTerrain->stats().mUsable)
            {
                // Task 0133: a neutral world has no ESM land to measure for `view distance = auto` with `map view`; the
                // tiles of its pack say how far the land reaches.
                mNeutralExtentPending = false;
                const Lod::LandExtent extent = mLodTerrain->landExtent();
                if (!extent.empty() && mNeutralStorage)
                {
                    mLandDiagonal = Lod::landDiagonal(extent, mNeutralStorage->cellWorldSize);
                    if (!mViewLimitsOverridden)
                        applyViewLimits(currentViewLimits(mLandDiagonal, farTierOrNull()), true);
                    Log(Debug::Info) << "World LOD terrain: the land of the neutral world reaches " << mLandDiagonal
                                     << " units across (cells " << extent.mMinX << ", " << extent.mMinY << " to "
                                     << extent.mMaxX << ", " << extent.mMaxY << ").";
                }
            }
        }
        const bool coverageChanged = coverage != nullptr && coverage->version != mLastCoverageVersion;
        // The clock is read only for a coverage change, the one case the timing line below reports (third review).
        const auto rebuildStart
            = coverageChanged ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        Misc::LoadTrace::Scope traceUpdateView("terrain.updateview");
        // Task 0088: the live terrain's chunks dither in and out over the same time as the LOD tiles, with the world
        // LOD on and a fade time above 0; otherwise none of it runs.
        Terrain::ChunkFadeParams chunkFade;
        if (Settings::worldLod().mEnabled)
        {
            chunkFade.fadeSeconds = Settings::worldLod().mTerrainFadeTime;
            chunkFade.deltaSeconds = dt;
        }
        auto compile = mTerrain->updateView(*mTerrainView, pos, mViewDistance, false, coverage, &chunkFade);
        traceUpdateView.end();
        if (compile)
        {
            Misc::LoadTrace::Scope traceCompile("terrain.compile");
            compile->compile();
        }
        if (coverageChanged)
        {
            mLastCoverageVersion = coverage->version;
            ++mCoverageRebuilds;
            const double milliseconds
                = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - rebuildStart).count();
            // A slow rebuild is worth a line (second review: a 492 ms frame on the climb was not explained).
            constexpr double slowRebuildMs = 50;
            if (milliseconds > mCoverageRebuildWorstMs || milliseconds > slowRebuildMs)
            {
                mCoverageRebuildWorstMs = std::max(mCoverageRebuildWorstMs, milliseconds);
                Log(Debug::Info) << "World LOD tiles: the live terrain rebuilt its view for coverage change "
                                 << mCoverageRebuilds << " in " << milliseconds << " ms (worst so far "
                                 << mCoverageRebuildWorstMs << " ms).";
            }
        }
    }

    void RenderManager::releaseWorldLod()
    {
        // ~LodTerrain waits for its worker, which reads the World's ESM store through the live terrain storage.
        mLodTerrain.reset();
        // The puddle map's worker reads the same store (task 0095, review of stage W-d).
        releasePuddleMap();
    }

    void RenderManager::releasePuddleMap()
    {
        // The worker reads the terrain storage and the ESM store that the World owns, and the renderer outlives the
        // World: it ends before the store goes, and the buffer says "no map" until a new world makes a new runtime.
        mPuddleMap.reset();
        if (mPuddleMapData)
        {
            Puddle::packOff(mPuddleMapData->data(), mPuddleMapData->size());
            mPuddleMapData->dirty();
        }
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

    void RenderManager::setCellObjectsPending(int x, int y, bool pending)
    {
        if (pending)
            mUnbuiltCells.insert({ x, y });
        else
            mUnbuiltCells.erase({ x, y });
        if (mDistantObjects)
            mDistantObjects->setCellUnbuilt({ x, y }, pending);
    }

    void RenderManager::setActiveGrid(int x, int y, int halfGrid)
    {
        mActiveGrid = { { x, y }, halfGrid };
        if (mDistantObjects)
            mDistantObjects->setActiveGrid(mActiveGrid);
        if (mLodClusters)
            mLodClusters->setActiveGrid(mActiveGrid);
    }

    void RenderManager::updateLodClusters(const vsg::vec3& cameraPosition, float dt)
    {
        if (!mLodClusters)
            return;
        if (!mLodClustersAttached)
        {
            mLodClustersAttached = true;
            updateSceneChildren();
        }
        // Morrowind's exterior only, as the distant objects.
        const MWWorld::Ptr player = MWMechanics::getPlayer();
        LodClusters::Camera camera;
        // Task 0133 stage 2: a game pack's neutral exterior is the other world a pack can be for (the pack is the one
        // of the world the terrain follows: a change of world replaces the clusters, useNeutralTerrain).
        camera.mEnabled = !player.isEmpty() && player.isInCell() && player.getCell()->isExterior()
            && (player.getCell()->isNeutral()
                || player.getCell()->getCell()->getWorldSpace() == ESM::Cell::sDefaultWorldspaceId);
        camera.mEye = mViewMatrix->eye;
        camera.mForward = vsg::normalize(mViewMatrix->center - mViewMatrix->eye);
        camera.mFovYDegrees = mProjectionMatrix->fieldOfViewY;
        camera.mAspect = mProjectionMatrix->aspectRatio;
        camera.mViewportHeight = mAppliedExtent.height;
        camera.mViewDistance = mTileRange;
        camera.mPagingRange = mViewDistance; // what the live terrain and objects reach (task 0060)
        // Without the live distant objects (object paging off) nothing else draws a cluster's statics (second review).
        // Task 0133 stage 2: in a neutral world nothing pages distant objects (DistantObjects reads the ESM store), only
        // the active grid's live cells draw statics, so a cluster has no fallback and is never given up.
        const bool neutralExterior = camera.mEnabled && player.getCell()->isNeutral();
        camera.mLiveObjectsPage = mDistantObjects != nullptr && !neutralExterior;
        // The drawn terrain tiles: the ground beneath a far cluster (far cluster eviction; empty without LodTerrain).
        if (mLodTerrain)
        {
            camera.mGround = mLodTerrain->drawnGround();
            camera.mGroundVersion = mLodTerrain->groundVersion();
        }
        // Outside the exterior the live objects are not drawn at all (DistantObjects is hidden and does not rebuild),
        // so waiting for them would keep a cluster on screen in an interior: nothing to wait for there.
        const bool exterior = camera.mEnabled;
        mLodClusters->setWindSpeed(mSky ? mSky->getBaseWindSpeed() : 0.f);
        mLodClusters->update(camera, dt, [this, exterior, neutralExterior](const std::pair<int, int>& cell) {
            return !exterior || neutralExterior || !mDistantObjects || mDistantObjects->staticsReady(cell);
        });
        // The live distant objects stop drawing the statics of the cells a cluster draws (and take them back).
        if (mDistantObjects && !neutralExterior && mLodClusters->clusteredCellsVersion() != mLodClustersPushed)
        {
            mLodClustersPushed = mLodClusters->clusteredCellsVersion();
            mDistantObjects->setClusteredCells(mLodClusters->clusteredCells());
        }
        (void)cameraPosition;
    }

    void RenderManager::updateDistantObjects(const vsg::vec3& cameraPosition, float dt)
    {
        if (!Settings::terrain().mDistantTerrain || !Settings::terrain().mObjectPaging)
            return;
        const MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.isEmpty() || !player.isInCell())
            return;
        if (!mDistantObjects)
        {
            // Task 0096 stage d: read once, when the chunks are first made (the chunks are built with or without
            // controllers).
            DistantObjects::AnimationOptions animation;
            animation.mEnabled = Settings::worldLod().mAnimatedObjects;
            animation.mStopDistance = Settings::worldLod().mAnimatedObjectDistance;
            animation.mBudget = static_cast<unsigned>(Settings::worldLod().mAnimatedObjectBudget.get());
            mDistantObjects = std::make_unique<DistantObjects>(mScene->noParticlesContext,
                MWBase::Environment::get().getWorld()->getStore(), static_cast<float>(ESM::Land::REAL_SIZE),
                Settings::terrain().mObjectPagingMinSize, animation);
            mDistantObjects->setActiveGrid(mActiveGrid);
            for (const auto& cell : mUnbuiltCells)
                mDistantObjects->setCellUnbuilt(cell, true);
            updateSceneChildren();
        }
        // Morrowind's exterior only: another worldspace's coordinates would select its chunks.
        if (player.getCell()->isExterior()
            && player.getCell()->getCell()->getWorldSpace() == ESM::Cell::sDefaultWorldspaceId)
        {
            mDistantObjects->update(cameraPosition, mViewDistance);
            mDistantObjects->updateAnimation(cameraPosition, dt);
        }
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
        // Task 0059: a static a script disabled is drawn by the live objects again for its cell.
        if (mLodClusters && !enabled && (type == ESM::REC_STAT || type == ESM::REC_ACTI) && ptr.isInCell()
            && ptr.getCell()->isExterior())
            mLodClusters->overrideReference(ptr.getCellRef().getRefNum());
        if (!mDistantObjects || !ptr.isInCell() || !ptr.getCell()->isExterior() || !isDistantObjectType(type))
            return false;
        return mDistantObjects->setDisabled(ptr.getCellRef().getRefNum(), !enabled, referenceCells(ptr));
    }

    void RenderManager::pagingBlacklistObject(int type, const MWWorld::ConstPtr& ptr)
    {
        if (mLodClusters && (type == ESM::REC_STAT || type == ESM::REC_ACTI) && ptr.isInCell()
            && ptr.getCell()->isExterior())
            mLodClusters->overrideReference(ptr.getCellRef().getRefNum());
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
        if (mPointShadows)
        {
            // The weather calls this every frame. The casters change only when the state does; with the cache on
            // that is the only time the faces' scenes are set again and the maps are drawn afresh (task 0077, e).
            const bool changed = mPointShadowSkyState != static_cast<int>(enabled);
            if (changed || !mPointShadows->cacheEnabled())
            {
                auto& maps = mPointShadows->maps();
                for (int slot = 0; slot < maps.slotCount; ++slot)
                {
                    for (int face = 0; face < Render::PointShadow::facesPerLight; ++face)
                        maps.faceView(slot, face)->children = createPointShadowScene();
                }
            }
            if (changed)
            {
                mPointShadowSkyState = static_cast<int>(enabled);
                mPointShadows->invalidateAll();
            }
        }
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

        // OpenEden (task 0066, stage c): with the whole-map view on, the main view's exterior above water takes the
        // aerial perspective in place of the linear fog end (the scale's sign says so); interiors and under water keep
        // the fog.
        const bool aerial = mAerialShader && !mViewLimitsOverridden && Settings::worldLod().mMapView
            && Settings::worldLod().mHaze && !sLodBakeRun && !isUnderwater && mRenderModes[Render_Sky].active
            && !Settings::fog().mUseDistantFog;
        mSceneData->setFogRange(mFog->getFogStart(isUnderwater), mFog->getFogEnd(isUnderwater), aerial);

        auto& sceneData = mSceneData->value();
        mDisplayFogColor = vsg::vec4(fogColor[0], fogColor[1], fogColor[2], 1.f);
        sceneData.fogColor = toView(mDisplayFogColor);
        // The marker of the linear view; 1 without HDR, as it always was (lib/view/linear.glsl).
        sceneData.fogColor.w = linear ? 2.f : 1.f;

        auto ambient = mAmbientColor;
        ambient += vsg::vec4(0.7, 0.7, 0.7, 0.0) * mNightEyeFactor;
        mDisplayAmbient = ambient;
        sceneData.ambient = toView(ambient);
        // Task 0037: the wetness rides in the ambient colour's alpha, as 1 + wetness (the scene block cannot grow, see
        // lib/view/data.glsl). Other views (map, preview, editor) leave the alpha at 1 or 0 and so see a dry world; the
        // shaders read max(alpha - 1, 0). Off, the alpha is not touched.
        if (mWetnessEnabled)
            sceneData.ambient.w = 1.f + mWetness * mWetnessStrength;
        // Task 0039: snow and ash cover share that alpha (Render::Cover::pack); without cover it is the line above.
        if (mCoverEnabled)
            sceneData.ambient.w = Render::Cover::pack(
                mWetnessEnabled ? mWetness * mWetnessStrength : 0.f, mCoverKind, mCoverAmount * mCoverStrength);
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
        // Task 0040: during a lightning flash the sun's light is tilted towards the flash, by the flash's share of the
        // light (both measured in the display-encoded brightness the weather colours use). The sun's light colour
        // already has the flash added (WeatherManager::updateLightning).
        vsg::vec3 lightPosition = mLightPosition;
        if (mLightningEnabled && mLightningIntensity > 0.f)
        {
            const vsg::vec3 flashColour = Render::Lightning::colourAdd(mLightningIntensity);
            const float flashWeight = Render::Atmosphere::Detail::luma(flashColour);
            const float sunWeight = std::max(
                Render::Atmosphere::Detail::luma(vsg::vec3(mLightDiffuse.x, mLightDiffuse.y, mLightDiffuse.z))
                    - flashWeight,
                0.f);
            lightPosition
                = Render::Lightning::mergedDirection(mLightPosition, sunWeight, mLightningDirection, flashWeight);
        }
        mSceneData->setLightPosition(lightPosition, sceneData.invView);
        sceneData.lightViewPos.a = mRenderModes[Render_Sky] ? mSky->getLightSize() : 8.f;

        const auto& vp = mSceneCamera->getViewport();
        sceneData.resolution = { vp.width, vp.height };
        mShadow->updateCascades(*mSceneCamera, sceneData, lightPosition);
        // Task 0077: the point lights the last traversal collected get slots; the faces and the data are updated for
        // the frame about to be recorded.
        if (mPointShadows)
        {
            if (auto collect = dynamic_cast<View::CollectLights*>(mView->viewDependentState.get()))
            {
                std::uint32_t worldEpoch = 0;
                if (mPointShadows->cacheEnabled())
                {
                    // The moving casters: actors, doors and the player (their sphere and pose, compared with the
                    // last frame's inside the tracker), and the scene's own change counter.
                    auto& motion = mPointShadows->motion();
                    motion.begin();
                    mScene->collectMoving(motion);
                    if (mPlayer && mPlayer->transform())
                    {
                        const Anim::Transform& t = *mPlayer->transform();
                        Render::PointShadow::MotionTracker::Pose pose{};
                        for (int i = 0; i < 3; ++i)
                            pose[static_cast<std::size_t>(i)] = t.translation[i];
                        for (int i = 0; i < 3; ++i)
                            for (int j = 0; j < 3; ++j)
                                pose[static_cast<std::size_t>(3 + i * 3 + j)] = t.rotation(i, j);
                        motion.add(reinterpret_cast<std::uintptr_t>(mPlayer.get()), pose,
                            vsg::dvec3(t.translation[0], t.translation[1], t.translation[2] + 64.0), 150.f);
                    }
                    worldEpoch = mScene->changeCounter();
                }
                mPointShadows->update(mViewMatrix->transform(), collect->snapshots(), worldEpoch);
                collect->setShadowEntries(mPointShadows->entries());
            }
        }
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
        data.far = mFarPlane;
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
        mFog->configure(mFogRange, cell);
    }

    void RenderManager::configureFog(
        float fogDepth, float underwaterFog, float dlFactor, float dlOffset, const vsg::vec4& color)
    {
        mFog->configure(mFogRange, fogDepth, underwaterFog, dlFactor, dlOffset, color);
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
        if ((!mAmbientOcclusion && !mAoTemporal) || !mHdrTarget)
            return sources;
        sources.ambient = mHdrTarget->ambientView();
        // The accumulated occlusion (task 0076) replaces the fragment pass's image; the tonemap's filter is the same.
        sources.ao = mAoTemporal ? mAoTemporal->result() : mAmbientOcclusion->result();
        sources.normal = mHdrTarget->normalView();
        sources.linearDepth = mHdrTarget->linearDepthView();
        sources.aoExtent = mAoTemporal ? mAoTemporal->resultExtent() : mAmbientOcclusion->resultExtent();
        return sources;
    }

    vsg::ref_ptr<vsg::ImageView> RenderManager::sceneColourView() const
    {
        if (mTaa && mTaa->result())
            return mTaa->result();
        return mHdrTarget->colorView();
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
        // Task 0075: the same camera comparison decides whether the anti-aliasing history may be used. The view
        // direction of a view matrix is minus the third row of its rotation.
        Render::Taa::ResetInput taaReset{};
        taaReset.havePrevious = mHavePreviousCamera;
        taaReset.gapSeconds = std::chrono::duration<double>(now - mPreviousCameraTime).count();
        taaReset.cameraDistance = vsg::length(eye - mPreviousEye);
        taaReset.viewDirectionCos = vsg::dot(vsg::dvec3(view(0, 2), view(1, 2), view(2, 2)),
            vsg::dvec3(mPreviousView(0, 2), mPreviousView(1, 2), mPreviousView(2, 2)));
        mPreviousCameraTime = now;
        if (!mHavePreviousCamera || resumed || Render::Velocity::cameraJumped(mPreviousEye, eye))
        {
            mPreviousView = view;
            mPreviousProjection = projection;
            mPreviousEye = eye;
            mHavePreviousCamera = true;
        }
        const vsg::mat4 toPrevious
            = Render::Velocity::toPreviousClip(mPreviousProjection, mPreviousView, vsg::inverse(view));
        mVelocity->setMatrix(toPrevious, static_cast<float>(projection(0, 0)), static_cast<float>(projection(1, 1)));
        if (mTaa)
        {
            Render::Taa::FrameInput input;
            input.toPreviousClip = toPrevious;
            input.projectionX = static_cast<float>(projection(0, 0));
            input.projectionY = static_cast<float>(projection(1, 1));
            input.reset = Render::Taa::historyReset(taaReset);
            mTaa->beginFrame(input);
        }
        if (mAoTemporal)
        {
            // Task 0076: the same camera data. With TAA on the frame index and the jitter are its state's, so the two
            // accumulate in step; with it off the pass counts its own frames and there is no jitter.
            Render::Ao::Temporal::FrameInput input;
            input.toPreviousClip = toPrevious;
            input.rotation = Render::Ao::Temporal::relativeRotation(view, mPreviousView);
            input.projectionX = static_cast<float>(projection(0, 0));
            input.projectionY = static_cast<float>(projection(1, 1));
            input.reset = Render::Taa::historyReset(taaReset);
            if (mTaa)
            {
                const Render::Taa::TemporalState& state = mTaa->state();
                input.frame = state.frame;
                input.jitterDelta = Render::Ao::Temporal::jitterDeltaUv(state.jitterPixels, state.previousJitterPixels,
                    mHdrTarget->extent().width, mHdrTarget->extent().height);
            }
            mAoTemporal->beginFrame(input);
        }
        mPreviousView = view;
        mPreviousProjection = projection;
        mPreviousEye = eye;
    }

    void RenderManager::updateVolume()
    {
        if (!mVolumeFog)
            return;
        const auto& rendering = Settings::rendering();
        const bool underwater
            = mRenderModes[Render_Water].active && mWater->isUnderwater(vsg::vec3(mCamera->getPosition()));
        Render::Volume::ParamsInput input;
        input.grid = mVolumeFog->grid();
        // Underwater fog is left to the water's own fog in version 1 (task 0041, D8).
        input.enabled = !underwater;
        input.fogEnd = mFog->getFogEnd(underwater);
        input.densityScale = rendering.mVolumetricsDensity;
        input.heightScale = Render::Volume::defaultHeightScale;
        input.anisotropy = rendering.mVolumetricsAnisotropy;
        // The sun's light and its shadow cascades. With `enable shadows` off there are no cascades (spec D3): the fog
        // is lit by the sun everywhere, as an even glow round it, and never forces the cascades on.
        input.sunGain = Render::Volume::defaultSunGain;
        input.lampGain = Render::Volume::defaultLampGain;
        input.debug = rendering.mVolumetricsDebug;
        if (mShadow->shadow)
        {
            input.cascades = static_cast<std::uint32_t>(mShadow->shadow->numCascades);
            input.shadowMaxDistance = Settings::shadows().mMaximumShadowMapDistance;
            input.shadowFadeStart = Settings::shadows().mShadowFadeStart;
        }
        mVolumeFog->setParams(input, mViewMatrix->transform(), mProjectionMatrix->transform());
    }

    void RenderManager::updateCloudVolume()
    {
        if (!mCloudVolume)
            return;
        const auto& rendering = Settings::rendering();
        const Sky::VolumeCloudInput clouds
            = mRenderModes[Render_Sky].active ? mSky->getVolumeClouds() : Sky::VolumeCloudInput{};
        const bool underwater
            = mRenderModes[Render_Water].active && mWater->isUnderwater(vsg::vec3(mCamera->getPosition()));
        // Nothing is drawn (and nothing marched) where the sky shows no volume: interiors, weathers that keep the
        // vanilla sky, under water.
        const bool active = clouds.valid && clouds.share > 0.f && !underwater;
        mCloudVolume->setEnabled(active);
        if (!active)
        {
            // The ground shadow map is told that nothing casts a shadow.
            mCloudVolume->setParams({}, mViewMatrix->transform(), mProjectionMatrix->transform());
            return;
        }
        const auto tier = Render::CloudVolume::tierSettings(mCloudVolume->quality());
        Render::CloudVolume::ParamsInput input;
        input.coverage = clouds.coverage * rendering.mVolumetricCloudsCoverageScale;
        input.densityScale = rendering.mVolumetricCloudsDensity;
        input.windMeters = { clouds.windX, clouds.windY };
        if (rendering.mVolumetricCloudsShadows)
        {
            // The sun's light is dim at night (the manager's mirrored light), and then there are no shadows.
            const vsg::vec4& sunLight = mSceneData->value().lightDiffuse;
            input.shadowShare = clouds.share;
            input.shadowStrength = mCloudShadowStrength
                * Render::Cloud::strength(Render::Cloud::luminance(sunLight.x, sunLight.y, sunLight.z), 1.f);
        }
        input.steps = tier.steps;
        input.lightSteps = tier.lightSteps;
        input.hazeLengthMeters = Render::CloudVolume::hazeLengthMeters(mFog->getFogEnd(false));
        input.debug = Render::CloudVolume::parseDebugView(rendering.mVolumetricCloudsDebug.get());
        // The curved layer (a planet radius, a longer reach) and the variety (the wind wrap is the sky's).
        if (rendering.mVolumetricCloudsCurved)
        {
            input.planetRadiusMeters = rendering.mVolumetricCloudsPlanetRadius * 1000.f;
            input.rayLimitMeters = Render::CloudVolume::curvedMaxRayMeters;
        }
        input.variety = rendering.mVolumetricCloudsVariety;
        mCloudVolume->setParams(input, mViewMatrix->transform(), mProjectionMatrix->transform());
    }

    void RenderManager::onFrame(float dt)
    {
        updateSceneData();
        updateVolume();
        updateCloudVolume();
        updateSkyVisibility();
        updatePuddleMap();
        updateSkyEnvironment();
        updateSkyDebug();
        updateCloudShadow();
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
                mImageSpaceStopsApplied = 0.f; // the parameters were rebuilt without the grade; the next frame adds it
                if (steppedAside)
                    Log(Debug::Info) << "HDR: a post-processing technique sets hdr, so the built-in tonemap only clips";
            }
        }
        updateVelocity();
        if (mTonemap && std::abs(mImageSpaceStops - mImageSpaceStopsApplied) > 0.002f)
        {
            // The weather's image space brightness (task 0127): add the change to the tonemap's exposure.
            auto parameters = mTonemap->parameters();
            parameters.exposureStops += mImageSpaceStops - mImageSpaceStopsApplied;
            mTonemap->setParameters(parameters);
            mImageSpaceStopsApplied = mImageSpaceStops;
        }
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
            updateTerrain(mViewMatrix->eye, dt);
    }

    void RenderManager::setupScreenshotInterface()
    {
        // OpenEden (screenshots, third and fourth review rounds): the save thumbnail is a copy made inside the frame it
        // shows, by a Render::FrameCapture at the end of the main command graph (after the scene, tonemap and
        // post-process, before the GUI graph), read once that frame's fence has signalled. It used to count retrieve()
        // calls to numFrames() and read the copy without knowing the frame had finished, with no host barrier. A
        // request that cannot be copied gives no thumbnail instead of holding the save. The state is shared with the
        // capture's callbacks, which never refer to this RenderManager, and is a ThumbnailState: every request has a
        // generation, so the late callback of an abandoned save cannot answer the next one.
        using Reader = std::function<vsg::ref_ptr<vsg::Data>()>;
        auto state = std::make_shared<ThumbnailState<Reader>>();
        mScreenshotInterface->request = [this, state](int w, int h) {
            const std::uint64_t generation = state->begin();
            const VkExtent2D size{ static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
            Log(Debug::Info) << "Save thumbnail requested: " << w << " x " << h;
            if (mThumbnailCapture)
            {
                // The middle of the window in the thumbnail's aspect ratio, scaled with linear filtering
                // (FrameCapture::beforeRecord).
                const bool accepted = mThumbnailCapture->request(
                    [state, generation](vsg::ref_ptr<Render::Screenshot> copy) {
                        state->delivered(generation, [copy]() -> vsg::ref_ptr<vsg::Data> {
                            // A small picture, read on the main thread (no file is written: no name was given).
                            copy->operate();
                            return copy->downloadData;
                        });
                    },
                    [state, generation] { state->failed(generation); }, size);
                // A request is already waiting for its frame: it is the same picture and its callbacks answer.
                if (!accepted)
                {
                    Log(Debug::Info) << "Save thumbnail request merged into the one still waiting for its frame";
                    state->merged();
                }
            }
            else if (!mEngine.hasWindow())
            {
                // A headless engine (VSGOPENMW_INDIRECT): its image is never presented and Engine::loop waits for each
                // frame's fences, so the image holds the last frame and the copy can be made and waited for here.
                try
                {
                    const auto device = mEngine.getOrCreateDevice();
                    const auto image = mEngine.colorImageView(0)->image;
                    const VkExtent2D extent = mEngine.swapchainExtent();
                    auto scaled
                        = Render::createScaledDownloadCommands(device, image, Render::cropToAspect(extent, size),
                            image->format, mEngine.getPresentLayout(), size, Render::DownloadSync::Transfer);
                    if (scaled && Render::submit(device, scaled->commands) == VK_SUCCESS)
                    {
                        const Render::HostBuffer target = scaled->target;
                        state->delivered(generation, [device, target]() -> vsg::ref_ptr<vsg::Data> {
                            return Render::readDownloadedBuffer(device, target);
                        });
                    }
                    else
                        state->failed(generation);
                }
                catch (const vsg::Exception& e)
                {
                    Log(Debug::Warning) << "Save thumbnail skipped: " << e.message << " VkResult " << e.result;
                    state->failed(generation);
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Warning) << "Save thumbnail skipped: " << e.what();
                    state->failed(generation);
                }
            }
            else
                state->failed(generation);
        };
        mScreenshotInterface->retrieve = [state]() -> std::pair<vsg::ref_ptr<vsg::Data>, bool> {
            auto result = state->retrieve();
            if (!result.ready)
                return { {}, false };
            vsg::ref_ptr<vsg::Data> data;
            if (result.copy)
            {
                // Called by SaveGame::run outside its try block, in the loop callback: a throw here would end the game
                // (Engine::frame catches only std::exception, and MappedData::create throws vsg::Exception), so a
                // thumbnail that cannot be read is no thumbnail.
                try
                {
                    const auto start = std::chrono::steady_clock::now();
                    data = (*result.copy)();
                    Log(Debug::Verbose) << "Save thumbnail read in "
                                        << std::chrono::duration_cast<std::chrono::duration<float, std::milli>>(
                                               std::chrono::steady_clock::now() - start)
                                               .count()
                                        << " ms";
                }
                catch (const vsg::Exception& e)
                {
                    Log(Debug::Warning) << "Save thumbnail skipped: " << e.message << " VkResult " << e.result;
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Warning) << "Save thumbnail skipped: " << e.what();
                }
            }
            return { data, true };
        };
    }

    // Task 0080 stage d: made when the scene exists, then ticked once a frame (main thread).
    bool RenderManager::ensureTextureMips()
    {
        if (!mTextureMips)
        {
            if (!mCompileContext || !mSceneRoot)
                return false;
            TextureMipsDriver::Services services;
            services.camera = [this] { return mCamera->getPosition(); };
            // The terrain's layer arrays. Chunks nobody draws now still bind the old arrays: the views built ahead of
            // the player, the pooled local map views and the batch cache entries only they held are dropped before and
            // after an array swap (they build again from the new descriptors when needed).
            services.beforeArraySwap = [this] {
                MWBase::Environment::get().getWorld()->releaseTerrainPreload();
                if (mMap)
                    mMap->clearIdleTerrainViews();
                // Last: an entry the cache shares with a view is only the cache's once the views let it go.
                if (mTerrain)
                    mTerrain->pruneCache();
                if (mLodTerrain)
                    mLodTerrain->pruneCache();
            };
            services.afterArraySwap = services.beforeArraySwap;
            // The Builder (and the LOD tiles' own) hand the new descriptor to later batches, and the batch caches
            // follow the new binds.
            services.arrayDescriptorReplaced
                = [this](const vsg::DescriptorImage* old, vsg::ref_ptr<vsg::DescriptorImage> replacement) {
                      if (mTerrain)
                          mTerrain->replaceArrayDescriptor(old, replacement);
                      if (mLodTerrain)
                          mLodTerrain->replaceArrayDescriptor(old, replacement);
                  };
            services.bindsReplaced =
                [this](const std::map<const vsg::BindDescriptorSet*, vsg::ref_ptr<vsg::BindDescriptorSet>>& replaced) {
                    if (mTerrain)
                        mTerrain->replaceBinds(replaced);
                    if (mLodTerrain)
                        mLodTerrain->replaceBinds(replaced);
                };
            services.compile = mCompileContext->clone(~vsg::Mask(0));
            services.root = [this]() -> vsg::Node* {
                // The main scene, and the graph that draws the inventory preview and the local map.
                mTextureMipsRoots = vsg::Group::create();
                for (const auto& graph : mEngine.allCommandGraphs())
                    mTextureMipsRoots->addChild(graph);
                if (mGuiRenderGraph)
                    mTextureMipsRoots->addChild(mGuiRenderGraph);
                // The models the loaders cache are what scene objects are cloned from, and they hold the same binds.
                const auto addModels = [this](const vsg::ref_ptr<const vsg::Options>& options) {
                    if (options && options->sharedObjects)
                        for (const auto& model : vsgUtil::TextureMips::cachedModels(*options->sharedObjects))
                            mTextureMipsRoots->addChild(model);
                };
                addModels(mResourceSystem->nodeOptions);
                addModels(mScene->context.nodeOptions);
                addModels(mScene->context.effectOptions);
                addModels(mScene->context.actorOptions);
                for (const auto& options : mScene->context.partBoneOptions)
                    addModels(options);
                return mTextureMipsRoots.get();
            };
            services.pruneCaches = [this] {
                mResourceSystem->updateCache(0.0);
                mScene->context.prune(); // the actors' and parts' shared objects, as the cell change does
            };
            services.memory = [this] { return mEngine.deviceLocalMemorySample(); };
            services.releasePools = [this] { return mEngine.releaseUnusedDeviceMemory(); };
            services.poolStats = [this] { return mEngine.deviceMemoryPoolStats(); };
            mTextureMips = std::make_unique<TextureMipsDriver>(std::move(services));
            if (!mTextureMipsScript.empty() && !mTextureMips->setScript(mTextureMipsScript))
                Log(Debug::Warning) << "OPENEDEN_TEXTURE_MIPS_TEST is not valid ('seconds:level,...') and is ignored";
            Log(Debug::Info) << "[VideoMemory] texture mips: the swapper is ready";
        }
        return true;
    }

    void RenderManager::updateTextureMips(float dt)
    {
        if (ensureTextureMips())
            mTextureMips->update(dt);
    }

    // The `texturemips` console command (task 0080): making the swapper on the first call also makes it tick.
    std::string RenderManager::textureMipsCommand(const std::string& argument)
    {
        mTextureMipsWanted = true;
        if (!ensureTextureMips())
            return "Texture mips: the scene is not ready";
        return mTextureMips->command(argument);
    }

    void RenderManager::update(float dt, bool paused)
    {
        if (paused)
            dt = 0; // return;
        else
            mSceneData->value().time += dt;

        if (mVramScaler)
            mVramScaler->tick(dt);
        if (mTextureMipsWanted)
            updateTextureMips(dt);
        mEffects->update(dt);
        mProjectiles->update(dt);
        static const std::string noEffect;
        mWeatherEffects->setEffect(mRenderModes[Render_Sky] ? mSky->getParticleEffect() : noEffect);
        // Weather particles stop at the water surface, as OpenMW's underwater switch did.
        const vsg::vec3 cameraPosition(mCamera->getPosition());
        const bool aboveWater = !(mRenderModes[Render_Water].active && mWater->isUnderwater(cameraPosition));
        // Task 0040: lit snow (and blizzard): the sun's part of the flakes' light, from the sun's colour and the angle
        // between the camera's direction and the sun, in the display-encoded colour the material's emissive takes.
        if (mPrecipitationLit && mRenderModes[Render_Sky])
        {
            const std::string& particles = mSky->getParticleEffect();
            const auto& models = Settings::models();
            if (!particles.empty()
                && (particles == models.mWeathersnow.get().value()
                    || particles == models.mWeatherblizzard.get().value()))
            {
                const vsg::mat4& invView = mSceneData->value().invView;
                const vsg::vec3 forward(-invView[2][0], -invView[2][1], -invView[2][2]);
                const float sunLength = vsg::length(mLightPosition);
                const float forwardLength = vsg::length(forward);
                const float cosTheta = sunLength > 0.f && forwardLength > 0.f
                    ? vsg::dot(mLightPosition, forward) / (sunLength * forwardLength)
                    : 0.f;
                const vsg::vec3 sunLinear
                    = Render::Hdr::toLinear(vsg::vec3(mLightDiffuse.x, mLightDiffuse.y, mLightDiffuse.z));
                const vsg::vec3 lit = Render::Hdr::toEncoded(Render::Precipitation::snowSunLight(sunLinear, cosTheta));
                mWeatherEffects->setSunLight(&lit);
            }
            else
                mWeatherEffects->setSunLight(nullptr);
        }
        mWeatherEffects->update(
            dt, cameraPosition, mSky->getParticleOrientation(), mSky->getPrecipitationAlpha(), aboveWater);
        mSky->getRainParameters(mRainParameters);
        mRainParameters.enabled = mRainParameters.enabled && mRenderModes[Render_Sky];
        mRain->update(dt, cameraPosition, mRainParameters, aboveWater);
        mWaterRipples->update(dt, paused);
        Misc::LoadTrace::Scope traceDistant("render.groundcover.distant.lod");
        {
            Misc::LoadTrace::Scope traceGroundcover("render.update.groundcover");
            updateGroundcover(cameraPosition);
        }
        {
            Misc::LoadTrace::Scope traceObjects("render.update.distantobjects");
            updateDistantObjects(cameraPosition, dt);
        }
        {
            Misc::LoadTrace::Scope traceClusters("render.update.lodclusters");
            updateLodClusters(cameraPosition, dt);
        }
        if (mTreeSway)
            mTreeSway->update(paused ? 0.0 : static_cast<double>(dt),
                mTreeWindOverride ? *mTreeWindOverride : (mSky ? mSky->getBaseWindSpeed() : 0.f));
        traceDistant.end();
        if (mRenderModes[Render_Sky])
            mSky->update(dt);

        mPlayer->update(dt);
        setFirstPersonMode(mCamera->getMode() == Camera::Mode::FirstPerson);

        mScene->setNightDayMode(MWBase::Environment::get().getWorld()->getNightDayMode());
        Misc::LoadTrace::Scope traceSceneUpdate("render.scene.update");
        mScene->update(dt);
        traceSceneUpdate.end();
        updateNavMesh();
        updateExtensionDebug();
        /*
        updateRecastMesh();
        */
        mCamera->update(dt, paused);
        mCamera->updateCamera(*mViewMatrix);
        // After the camera moves: the particle shader centres the box on this frame's view.
        updateOccluder(vsg::vec3(mViewMatrix->eye));
    }

    // Task 0017 stage 5: the extensions' debug draw. With no frame (the setting is off) nothing is built, filled or
    // drawn: the two geometries stay disabled and hold no storage. The scale from the SDK's meters to the game's units
    // is applied once, in expandDebugDrawFrame.
    void RenderManager::updateExtensionDebug()
    {
        const bool active = mExtensionDebugFrame != nullptr;
        if (active != mExtensionDebugActive)
        {
            mExtensionDebugActive = active;
            mExtensionDebugTested->setEnabled(active);
            mExtensionDebugOnTop->setEnabled(active);
        }
        if (!active)
            return;
        for (const bool depthTest : { true, false })
        {
            Render::DebugGeometry& geometry = depthTest ? *mExtensionDebugTested : *mExtensionDebugOnTop;
            geometry.begin();
            openeden::extension::expandDebugDrawFrame(*mExtensionDebugFrame, depthTest, Constants::UnitsPerMeter,
                [&geometry](
                    const openeden::sdk::Vector3& a, const openeden::sdk::Vector3& b, const openeden::sdk::Color4f& c) {
                    geometry.addLine(vsg::vec3(a.x, a.y, a.z), vsg::vec3(b.x, b.y, b.z), vsg::vec4(c.r, c.g, c.b, c.a));
                });
            geometry.end();
        }
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

    void RenderManager::setNeutralCellWater(int x, int y, float cellSize, float height)
    {
        mWater->setCellWater(x, y, cellSize, height);
        if (mWater->cellWaterCount() == 0)
            return;
        // Cell-bounded surfaces need the water pass's copies and the water render mode, and no infinite plane.
        mNeutralWaterMode = true;
        mWater->setGlobalVisible(false);
        setRenderModeActive(Render_Water, true);
    }

    void RenderManager::removeNeutralCellWater(int x, int y)
    {
        mWater->removeCellWater(x, y);
        if (mNeutralWaterMode && mWater->cellWaterCount() == 0)
        {
            mNeutralWaterMode = false;
            mWater->setGlobalVisible(true);
            setRenderModeActive(Render_Water, false);
        }
    }

    void RenderManager::setWaterEnabled(bool enabled)
    {
        // Task 0129: the game's own water is the infinite plane again.
        mNeutralWaterMode = false;
        mWater->setGlobalVisible(true);
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
        if (mLodClusters)
        {
            mLodClusters->clearOverrides();
            // clear() emptied the live objects' set of clustered cells: send it again.
            mLodClustersPushed = ~std::uint64_t{ 0 };
        }
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
        mProjectionMatrix->farDistance = mFarPlane;

        mSceneData->setDepthRange(mNearClip, mFarPlane);
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
            else if ((it->first == "Camera" && it->second == "viewing distance")
                || (it->first == "Terrain" && it->second == "distant terrain")
                || (it->first == "WorldLod"
                    && (it->second == "enabled" || it->second == "view distance" || it->second == "map view"
                        || it->second == "max view distance")))
            {
                mViewLimitsOverridden = false;
                applyViewLimits(currentViewLimits(mLandDiagonal, farTierOrNull()), true);
                // mFog->configure
            }
        }
    }

    // TODO: if there is only ever one worldspace active at a time, isn't the worldspace parameter redundant
    float RenderManager::getTerrainHeightAt(const vsg::vec3& pos, ESM::RefId worldspace)
    {
        if (mNeutralStorage)
            return mNeutralStorage->getHeightAt(pos, worldspace);
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
        // A script's distance sets every limit, as the one number did before task 0066, until a setting changes. With
        // `map view` on that cancels the map view (the limits are one number again), and the haze is off while it is
        // set. The guard compares all four limits: comparing only the paging range made a call that matched it do
        // nothing while the far plane or the tile range differed (review of stages a and b). A distance that is not
        // finite or not positive is ignored.
        const Lod::ViewLimits wanted = Lod::uniformViewLimits(d);
        if (wanted == Lod::ViewLimits{})
        {
            return;
        }
        const Lod::ViewLimits current{ mTileRange, mViewDistance, mFarPlane, mFogRange };
        if (wanted != current)
        {
            mViewLimitsOverridden = true;
            mViewDistance = wanted.mPagingRange;
            mTileRange = wanted.mTileRange;
            mFarPlane = wanted.mFarPlane;
            mFogRange = wanted.mFogRange;
            updateProjectionMatrix();
        }
    }

    void RenderManager::applyViewLimits(const Lod::ViewLimits& limits, bool updateProjection)
    {
        mViewDistance = limits.mPagingRange;
        mTileRange = limits.mTileRange;
        mFogRange = limits.mFogRange;
        if (mFarPlane != limits.mFarPlane)
        {
            mFarPlane = limits.mFarPlane;
            updateProjection = true;
        }
        if (updateProjection && mProjectionMatrix)
        {
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
            if (mAoTemporal
                && !mAoTemporal->setSource(mHdrTarget->linearDepthView(), mHdrTarget->normalView(), mVelocity->result(),
                    extent, [this](vsg::ref_ptr<vsg::Object> retired) { mCompileContext->detach(retired); }))
                Log(Debug::Error) << "The ambient occlusion accumulation pass could not be rebuilt after a resize";
            if (mTaa
                && !mTaa->setSource(mHdrTarget->colorView(), mHdrTarget->linearDepthView(), mVelocity->result(), extent,
                    [this](vsg::ref_ptr<vsg::Object> retired) { mCompileContext->detach(retired); }))
                Log(Debug::Error) << "The temporal anti-aliasing pass could not be rebuilt after a resize";
            if (mTaa && !rebuildTaaOverlay())
                Log(Debug::Error) << "The temporal anti-aliasing overlay could not be rebuilt after a resize";
            if (mBloom
                && !mBloom->setSource(*mHdrContext, sceneColourView(), extent,
                    [this](vsg::ref_ptr<vsg::Object> retired) { mCompileContext->detach(retired); }))
                Log(Debug::Error) << "The bloom passes could not be rebuilt after a resize";
            if (!mTonemap->setSource(sceneColourView(), mBloom ? mBloom->result() : nullptr, extent,
                    debugSources(*mHdrTarget, mVelocity ? mVelocity->result() : nullptr), aoSources(), sssSources()))
                Log(Debug::Error) << "The tonemap pass could not be rebuilt after a resize";
        }
        updateProjectionMatrix();
        if (mPostProcess)
            mPostProcess->resize(extent.width, extent.height);
    }

    bool RenderManager::rebuildTaaOverlay()
    {
        if (mOverlayGraph->framebuffer)
            mCompileContext->detach(mOverlayGraph->framebuffer);
        // The framebuffer reads the view's Vulkan handle when it is made, and nothing else has compiled this view yet
        // (the resolve's descriptors use views of their own); compiling twice does nothing.
        if (mTaa->result())
            mTaa->result()->compile(*mHdrContext);
        mOverlayGraph->framebuffer = mHdrTarget->createOverlayFramebuffer(mTaa->result());
        mOverlayGraph->renderArea = { { 0, 0 }, mHdrTarget->extent() };
        return mOverlayGraph->framebuffer != nullptr;
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
                // Task 0075: the arms and weapon get the same jitter as the world.
                if (mTaaJitter)
                    firstPersonProjection->projection = Render::Taa::JitteredProjection::create(fov, mTaaJitter);
                else
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
