#ifndef VSGOPENMW_MWRENDER_RENDERMANAGER_H
#define VSGOPENMW_MWRENDER_RENDERMANAGER_H

#include <components/lod/fartier.hpp>
#include <components/lod/viewlimits.hpp>

#include "distantobjectcells.hpp"
#include "rain.hpp"
#include "renderinginterface.hpp"
#include "rendermode.hpp"
#include "skyrimtreesway.hpp"
#include "waterripples.hpp"

#include <array>
#include <atomic>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>

#include <chrono>

#include <vsg/maths/box.h>
#include <vsg/nodes/Group.h>

#include <components/esm/refid.hpp>
#include <components/esm3/cellref.hpp>
#include <components/misc/vsgmath.hpp>
#include <components/render/aotemporal.hpp>
#include <components/render/cloudgpu.hpp>
#include <components/render/covermath.hpp>
#include <components/render/probes.hpp>
#include <components/render/reflectionprobes.hpp>
#include <components/render/scenecensus.hpp>
#include <components/render/skyvisibility.hpp>
#include <components/render/taa.hpp>
#include <components/render/tonemap.hpp>
#include <components/render/vramscaler.hpp>
#include <components/settings/settings.hpp>

#include <components/neutralterrain/window.hpp>

namespace MWWorld
{
    class Ptr;
    class ConstPtr;
    class CellStore;
    class Cell;
    class ESMStore;
    class GroundcoverStore;
}
namespace NeutralTerrain
{
    class CellCache;
    class Storage;
}
namespace openeden::sdk::game
{
    class WorldCellSource;
}
namespace Resource
{
    class ResourceSystem;
}
namespace openeden::extension
{
    class DebugDrawFrame;
}

namespace Render
{
    class Bloom;
    class Velocity;
    class AmbientOcclusion;
    class DebugGeometry;
    class Engine;
    class FrameCapture;
    class GpuTimer;
    class HdrTarget;
    class SimpleWater;
    class Tonemap;
}
namespace vsg
{
    class Camera;
    class Context;
    class LookAt;
    class Perspective;
    class View;
    class RecordTraversal;
    class OperationThreads;
}
namespace vsgUtil
{
    class SuspendRenderPass;
    class DeferredBin;
    struct CompileOp;
    class CompileContext;
    class UpdateThreads;
}
namespace View
{
    class LightGrid;
    class VolumeFog;
    class CloudVolume;
    struct Scene;
}
namespace ESM
{
    struct Cell;
}
namespace Terrain
{
    class Paging;
    class View;
    struct TerrainMapSettings;
}
namespace DetourNavigator
{
    struct Navigator;
    struct Settings;
}
namespace MWAnim
{
    class Object;
    struct Context;
}
namespace MWRender
{
    class LodTerrain;
    class PuddleMapRuntime;
    class LodClusters;
    class PrecipitationOccluder;
    class DistantObjects;
    class PostProcess;
    class Scene;
    class Reflect;
    class Effects;
    class WeatherEffects;
    class Groundcover;
    class Projectiles;
    class FogManager;
    class Sky;
    class Player;
    class Pathgrid;
    class Camera;
    class Water;
    class TerrainStorage;
    class LandManager;
    class NavMesh;
    class ActorsPaths;
    class RecastMesh;
    class Shadow;
    class PointShadows;
    struct ScreenshotInterface;
    class Map;
    class WorldMap;
    class Preview;

    /*
     * Creates command graphs.
     */
    class RenderManager
    {
    public:
        RenderManager(Render::Engine& engine, vsg::ref_ptr<vsg::Node> guiRoot,
            Resource::ResourceSystem* resourceSystem /*, DetourNavigator::Navigator& navigator*/);
        ~RenderManager();

        void setStore(const MWWorld::ESMStore& store);

        /// Task 0125 (stage E2 of docs/research/0053): draws the land of a game pack's neutral world instead of
        /// Morrowind's. Builds the terrain cache and storage for `world` of `source` and points the terrain at them (the
        /// terrain, its view and its chunk cache are rebuilt: call it behind the loading screen, with the terrain
        /// preloader's views released). The same game and world again does nothing. False with `error` when the source
        /// has no terrain for the world; the terrain is then left as it was. Main thread.
        bool useNeutralTerrain(
            openeden::sdk::game::WorldCellSource& source, std::uint64_t world, std::string& error);
        /// Whether the terrain draws a neutral world, and which one (game id, world id).
        bool neutralTerrainActive() const { return mNeutralStorage != nullptr; }
        const std::string& neutralTerrainGame() const { return mNeutralGame; }
        std::uint64_t neutralTerrainWorld() const { return mNeutralWorld; }
        /// The land of one cell of the active neutral world (own or a parent's); null with none, or when no neutral
        /// world is active. The cell must have been in the window ensureNeutralTerrain filled.
        std::shared_ptr<const openeden::sdk::game::TerrainCell> neutralTerrainCell(std::int32_t x, std::int32_t y) const;
        /// Loads the cells Terrain::Paging will read for a camera at `eye` into the cache (main thread). A window the
        /// last call already filled costs a comparison.
        void ensureNeutralTerrain(const vsg::dvec3& eye);
        /// Task 0133: the baked terrain tiles of the active neutral world, when its pack exists (main thread).
        void startNeutralLodTerrain(openeden::sdk::game::WorldCellSource& source);
        /// The height and bounds the physics heightfield of a neutral cell needs; the world's cell size and vertices.
        float neutralTerrainCellSize() const;
        /// Task 0055: where the baked terrain tiles are (used only with `[WorldLod] enabled`). Call before setStore.
        void setLodTerrainPack(std::filesystem::path path) { mLodTerrainPack = std::move(path); }
        /// Task 0133: <user data>/openeden/lod, the folder a game pack's neutral world keeps its packs in
        /// (Lod::terrainPackPath, keyed by game and world). Used only with `[WorldLod] enabled`.
        void setLodRoot(std::filesystem::path path) { mLodRoot = std::move(path); }
        // Task 0095: where the baked puddle map is (used with `wetness puddle map`).
        void setPuddlePack(std::filesystem::path path) { mPuddlePack = std::move(path); }
        void setLodClusterPack(std::filesystem::path path) { mLodClusterPack = std::move(path); }

        RenderingInterface& getObjects();

        Resource::ResourceSystem* getResourceSystem();
        const MWAnim::Context& getAnimContext();
        double getReferenceTime() const { return 0.0; }
        Terrain::Paging* getTerrain() { return mTerrain.get(); }
        vsg::ref_ptr<vsg::OperationThreads> getOperationThreads();
        vsg::ref_ptr<vsgUtil::UpdateThreads> getUpdateThreads();
        Map* getMap() { return mMap.get(); }
        WorldMap* getWorldMap() { return mWorldMap.get(); }
        /// Debug lines drawn by the renderer (the Jolt debug draw, task 0011).
        Render::DebugGeometry& getDebugGeometry() { return *mDebugGeometry; }
        /// The navigator whose navmesh the navmesh debug draw shows; set by the world, which owns it, and cleared
        /// (nullptr) when the world is destroyed.
        void setNavigator(const DetourNavigator::Navigator* navigator) { mNavigator = navigator; }
        /// World's groundcover; null when the World goes. Groundcover is drawn only with `[Groundcover] enabled`.
        void setGroundcoverStore(const MWWorld::GroundcoverStore* store);
        /// Called by the World's destructor, while the store it owns still exists: ends the world LOD terrain, whose
        /// staleness check runs on a worker thread that reads that store (task 0060 review fixes: a bake mode quits
        /// within a second of the start and the check was still running when the store went).
        void releaseWorldLod();
        void setNavMeshMode(Settings::NavMeshRenderMode value);
        Preview* getPreview() { return mPreview.get(); }
        LandManager* getLandManager() const;
        ScreenshotInterface& getScreenshotInterface() { return *mScreenshotInterface; }

        void setNightEyeFactor(float factor);
        void setAmbientColour(const vsg::vec4& colour);
        /// Task 0037: the rain wetness (0 dry to 1 soaked) the weather manager worked out; 0 indoors. Ignored unless
        /// [Rendering] wetness took effect at start. Main thread.
        void setWetness(float wetness);

        /// Task 0039: the snow or ash cover the weather manager worked out: the kind (0 none, 1 snow, 2 ash, as
        /// `Render::Cover::Kind`) and how much of it lies on the ground (0 to 1); 0 indoors. Ignored unless
        /// [Rendering] cover took effect at start. Main thread.
        void setCover(int kind, float amount);

        /// Task 0040: whether [Rendering] lightning took effect at start (needs hdr), its strength and the test aid
        /// age (-1: off).
        bool lightningEnabled() const { return mLightningEnabled; }
        float lightningStrength() const { return mLightningStrength; }
        float lightningDebugAge() const { return mLightningDebugAge; }
        /// Task 0040: the lightning flash's brightness this frame (0 to Render::Lightning::peakCap, added to the sun,
        /// ambient and fog colours by the weather manager) and the direction to the flash in world space. The sun's
        /// light is tilted towards the flash by its share of the light, for the shadows and the shading. 0 indoors.
        /// Ignored unless lightning took effect. Main thread.
        void setLightning(float intensity, const vsg::vec3& direction);
        void skySetDate(int day, int month);
        int skyGetMasserPhase() const;
        int skyGetSecundaPhase() const;
        void skySetMoonColour(bool red);

        void setSunDirection(const vsg::vec3& direction);
        void setSunColour(const vsg::vec4& diffuse, const vsg::vec4& specular);

        void configureAmbient(const MWWorld::Cell& cell);
        // Task 0028: the cell's flat ambient colour in linear light, without night eye: what the light probe baker
        // assumes.
        vsg::vec3 getLinearAmbient() const;
        // Task 0028: the interior's baked light probes, if it has a file (and gi is on); disableProbes for any cell
        // without.
        void loadProbes(const MWWorld::Cell& cell);
        void disableProbes();
        // Stage d: the reflection probes of an interior (their own file), and no reflections.
        void loadReflections(const Render::Gi::CellId& id);
        void disableReflections();
        void updateProbeDebug();
        // Task 0036: the loaded exterior cells, from the scene (any thread; read in onFrame). The renderer reads each
        // cell's sky-visibility file when it is wanted and drops it when the cell is unloaded.
        void exteriorCellLoaded(int x, int y);
        void exteriorCellUnloaded(int x, int y);
        void configureFog(const MWWorld::Cell& cell);
        void configureFog(float fogDepth, float underwaterFog, float dlFactor, float dlOffset, const vsg::vec4& colour);

        void addCell(const MWWorld::CellStore* store);
        void removeCell(const MWWorld::CellStore* store);
        void changeCellGrid(const vsg::vec3& pos);
        /// Task 0060: an exterior cell's objects are queued to be built over several frames (pending) or all built.
        /// The distant objects and clusters keep covering the cell until then.
        void setCellObjectsPending(int x, int y, bool pending);
        /// The exterior cells the scene loads, which distant objects leave out.
        void setActiveGrid(int x, int y, int halfGrid);
        /// For post-processing's sun position (OpenMW's RenderingManager::setNight).
        void setNight(bool night) { mPostProcessNight = night; }
        /// Post-processing, or null with `[Post Processing] enabled` off.
        PostProcess* getPostProcess() { return mPostProcess.get(); }
        void pruneCache();

        void updatePtr(const MWWorld::Ptr& old, const MWWorld::Ptr& updated);

        void rotateObject(const MWWorld::Ptr& ptr, const vsg::dquat& rot);
        void moveObject(const MWWorld::Ptr& ptr, const vsg::vec3& pos);
        void scaleObject(const MWWorld::Ptr& ptr, const vsg::vec3& scale);

        void removeObject(const MWWorld::Ptr& ptr);

        void setWaterEnabled(bool enabled);
        void setWaterHeight(float level);
        /// Task 0129: a water surface bounded to one exterior grid square of a game pack's neutral world (the infinite
        /// plane is hidden while any exists).
        void setNeutralCellWater(int x, int y, float cellSize, float height);
        void removeNeutralCellWater(int x, int y);

        struct RayResult
        {
            bool mHit = false;
            vsg::vec3 mHitNormalWorld;
            vsg::vec3 mHitPointWorld;
            MWWorld::Ptr mHitObject;
            ESM::RefNum mHitRefnum = { 0, -1 };
            float mRatio = 0.f;
        };

        RayResult castRay(const vsg::vec3& origin, const vsg::vec3& dest, bool ignorePlayer, bool ignoreActors = false);

        /// Return the object under the mouse cursor / crosshair position, given by nX and nY normalized screen
        /// coordinates, where (0,0) is the top left corner.
        RayResult castCameraToViewportRay(
            float nX, float nY, float maxDistance, bool ignorePlayer, bool ignoreActors = false);

        /// Get the bounding box of the given object in screen coordinates as (minX, minY, maxX, maxY), with (0,0) being
        /// the top left corner.
        vsg::vec4 getScreenBounds(const vsg::dbox& worldbb);
        /// Normalized screen position of the top centre of the box, from the top left; (0.5, 0) if it is off screen.
        vsg::vec2 getScreenCoords(const vsg::dbox& bb);

        /// The sun light as OpenMW 0.51 reports it: the direction towards the sun, with w = 0.
        const vsg::vec4& getSunLightPosition() const { return mSunLightPosition; }

        bool toggleRenderMode(RenderMode mode);
        void setRenderModeActive(RenderMode mode, bool enabled);
        void setViewMode(ViewMode mode, bool enable);
        void setSkyEnabled(bool enable);
        Sky* getSky();

        void spawnEffect(const std::string& model, std::string_view texture, const vsg::vec3& worldPosition,
            float scale = 1.f, bool isMagicVFX = true, std::string_view effectId = {}, bool loop = false);
        /// Removes the spawned effects with this ID (OpenMW 0.51 Lua VFX).
        void removeEffect(std::string_view effectId);

        /// Clear all savegame-specific data
        void clear();

        /// Clear all worldspace-specific data
        void notifyWorldSpaceChanged();

        void onFrame(float dt);
        void update(float dt, bool paused);

        /// The `texturemips` console command of task 0080: status, a manual level or off. Returns the line to print.
        std::string textureMipsCommand(const std::string& argument);

        MWAnim::Object* getObject(const MWWorld::Ptr& ptr);
        const MWAnim::Object* getObject(const MWWorld::ConstPtr& ptr) const;
        vsg::box getBoundingBox(const MWWorld::ConstPtr& ptr) const;

        void addWaterRippleEmitter(const MWWorld::Ptr& ptr);
        void removeWaterRippleEmitter(const MWWorld::Ptr& ptr);
        void emitWaterRipple(const vsg::vec3& pos);

        void updatePlayerPtr(const MWWorld::Ptr& ptr);

        void setupPlayer(const MWWorld::Ptr& player);
        void renderPlayer(const MWWorld::Ptr& player);

        void rebuildPtr(const MWWorld::Ptr& ptr);

        void processChangedSettings(const Settings::CategorySettingVector& settings);

        float getTerrainHeightAt(const vsg::vec3& pos, ESM::RefId worldspace);

        Camera* getCamera() { return mCamera.get(); }
        Projectiles* getProjectiles() { return mProjectiles.get(); }

        /// temporarily override the field of view with given value.
        void overrideFieldOfView(float val);
        /// reset a previous overrideFieldOfView() call, i.e. revert to field of view specified in the settings file.
        void resetFieldOfView();
        void setFieldOfView(float val);
        float getFieldOfView() const;
        float getViewDistance() const { return mViewDistance; }
        void setViewDistance(float distance, bool dummy = true);

        vsg::vec3 getHalfExtents(const MWWorld::ConstPtr& object) const;

        void exportSceneGraph(const MWWorld::Ptr& ptr, const std::filesystem::path& dir);

        bool toggleBorders();

        void updateActorPath(const MWWorld::ConstPtr& actor, const std::deque<vsg::vec3>& path,
            const vsg::vec3& halfExtents, const vsg::vec3& start, const vsg::vec3& end) const;

        void removeActorPath(const MWWorld::ConstPtr& actor) const;

        void setNavMeshNumber(const std::size_t value);

        /// The extensions' debug draw of this frame (task 0017 stage 5), drawn by the core as lines: null switches it
        /// off. The engine passes the host's frame each frame, between the Ui phase and the render update; the object
        /// stays owned by the extension host and must outlive the render manager's use of it. Main thread.
        void setExtensionDebugDraw(const openeden::extension::DebugDrawFrame* frame) { mExtensionDebugFrame = frame; }

        bool pagingEnableObject(int type, const MWWorld::ConstPtr& ptr, bool enabled);
        void pagingBlacklistObject(int type, const MWWorld::ConstPtr& ptr);
        bool pagingUnlockCache();
        void getPagedRefnums(const vsg::ivec4& activeGrid, std::set<ESM::RefNum>& out);

        void updateExtents(uint32_t w, uint32_t h);
        // Applies the swapchain's current extent to the camera, the render areas and post-processing; call once per
        // frame before recording, whatever the game state.
        void syncExtents();

        vsg::dmat4 getProjectionMatrix() const;

        /// Task 0075: the temporal state of the anti-aliasing (frame index, this frame's jitter, whether the history
        /// is valid, the matrix to the previous frame), for whatever accumulates over time; null unless
        /// [Rendering] taa took effect. Valid after onFrame.
        const Render::Taa::TemporalState* temporalState() const { return mTaa ? &mTaa->state() : nullptr; }

    private:
        vsg::ref_ptr<vsg::Node> createMapScene();
        vsg::Group::Children createCastShadowScene(bool exterior);
        /// Task 0077: the casters of the point lights' shadow faces (the same four settings, objects also indoors).
        vsg::Group::Children createPointShadowScene();
        void ensureCompiled();
        void createIntersectorScene();
        void updateSceneChildren();
        void updateNavMesh();
        void updateSceneData();
        void updateVolume();
        void updateCloudVolume();
        void updateProjectionMatrix();
        void setupScreenshotInterface();
        void setFirstPersonMode(bool enabled);
        void updateTerrain(const vsg::dvec3& pos, float dt = 0.f);

        struct Mode
        {
            bool active = true;
            bool toggled = true;
            operator bool() const { return active && toggled; }
        };
        std::vector<Mode> mRenderModes;

        Render::Engine& mEngine;
        bool mTextureDownscaleReport = false; // task 0049
        // Null unless [Rendering] gpu timing is on and the device supports it (task 0023). Owned by the Engine.
        Render::GpuTimer* mGpuTimer = nullptr;

        // Task 0054 (S0): what [Rendering] frame counters reports. The cell counts are atomics because cells load on a
        // worker thread; the census is read on the main thread only (its totals are refreshed every 30 frames).
        std::atomic<int> mLoadedExteriorCells{ 0 };
        std::atomic<unsigned> mExteriorCellLoadsTotal{ 0 };
        Render::SceneCensus mCensus;
        std::uint64_t mCensusDraws = 0;
        std::uint64_t mCensusTriangles = 0;
        unsigned mCensusCountdown = 0;
        static constexpr std::size_t noPass = static_cast<std::size_t>(-1);
        std::size_t mReflectPass = noPass;
        vsg::ref_ptr<vsg::StateGroup> mSceneRoot;
        vsg::ref_ptr<vsg::Group> mIntersectorScene;
        vsg::ref_ptr<vsg::Switch> mPlayerGroup;
        vsg::ref_ptr<vsg::View> mView;
        std::unique_ptr<Shadow> mShadow;
        // Task 0077: shadows of the nearest point lights (null unless [Shadows] point light shadows took effect).
        std::unique_ptr<PointShadows> mPointShadows;
        int mPointShadowSkyState = -1; // the last setSkyEnabled value the point light shadow cache saw
        std::unique_ptr<View::LightGrid> mLightGrid;
        // Task 0041: the volumetric fog; null unless `[Rendering] volumetrics` is on (and its shaders were found).
        std::unique_ptr<View::VolumeFog> mVolumeFog;
        // Task 0042: the ray-marched clouds; null unless `[Rendering] volumetric clouds` is on (and its shaders found).
        std::unique_ptr<View::CloudVolume> mCloudVolume;
        std::unique_ptr<View::Scene> mSceneData;
        vsg::ref_ptr<vsg::Node> mGui;
        vsg::ref_ptr<vsg::Camera> mSceneCamera;
        // Task 0075: with TAA the view draws with this camera, which has the jitter in its projection; mSceneCamera
        // and mProjectionMatrix stay unjittered for everything that needs the true camera. Null without TAA.
        vsg::ref_ptr<vsg::Camera> mRenderCamera;
        vsg::ref_ptr<Render::Taa::JitterOffset> mTaaJitter;
        vsg::ref_ptr<vsg::LookAt> mViewMatrix;
        vsg::ref_ptr<vsg::Perspective> mProjectionMatrix;
        vsg::ref_ptr<vsg::Group> mFirstPersonProjection;

        vsg::ref_ptr<vsg::RenderGraph> mRenderGraph;
        vsg::ref_ptr<vsg::RenderGraph> mGuiRenderGraph;
        // The linear HDR path (task 0025, [Rendering] hdr); all null with it off. mRenderGraph then draws into
        // mHdrTarget, and mTonemapGraph, a graph on the window, turns the result into the swapchain picture.
        vsg::ref_ptr<vsg::Context> mHdrContext; // before the target: it makes its images
        std::unique_ptr<Render::HdrTarget> mHdrTarget;
        // Task 0026 (F3): colour outputs of the main pass beyond the scene's own; 0 without the G-buffer.
        unsigned int mGBufferOutputs = 0;
        // Task 0028: baked light probes ([Rendering] gi). The buffer is the main view's probe descriptor; it is null
        // with gi off. Written on the main thread only.
        // Task 0029: ambient occlusion ([Rendering] ao); null without it. Read at start.
        bool mAoEnabled = false;
        std::unique_ptr<Render::AmbientOcclusion> mAmbientOcclusion;
        // Task 0076: [Rendering] ao temporal took effect (it needs ao): the accumulating compute pass replaces the
        // fragment pass above, which is then not made.
        bool mAoTemporalEnabled = false;
        std::unique_ptr<Render::AoTemporalPass> mAoTemporal;
        Render::Tonemap::AoSources aoSources() const;
        // Task 0033: subsurface scattering of skin ([Rendering] sss); off without its images. Read at start.
        bool mSssEnabled = false;
        Render::Tonemap::SssSources sssSources() const;
        vsg::ref_ptr<vsg::vec4Array> mProbeData;
        bool mGiEnabled = false;
        float mProbeStrength = 1.f;
        // Stage d: the reflection probe buffer (binding 11), null with gi off.
        vsg::ref_ptr<vsg::vec4Array> mReflectionData;
        float mReflectionStrength = 1.f;
        // `gi debug`: markers for the loaded probes, and the volume they were made from.
        std::unique_ptr<Render::DebugGeometry> mProbeGeometry;
        std::unique_ptr<Render::Gi::Volume> mLoadedVolume;
        std::unique_ptr<Render::Gi::ReflectionSet> mLoadedReflections;
        // Task 0036: the exterior sky-visibility field (bindings 12 and 13), null with gi exterior off.
        void updateSkyVisibility();
        void updatePuddleMap();
        void releasePuddleMap();
        // Stage d: the sky environment image, in the reflection probe buffer while outdoors.
        void updateSkyEnvironment();
        // Stage e: `gi debug` markers of the sky-visibility field around the camera.
        void updateSkyDebug();
        void updateCloudShadow();
        bool mSkyDebugDrawn = false;
        std::array<int, 4> mSkyDebugKey{};
        const Render::Gi::SkyVisField* mSkyDebugField = nullptr;
        bool mSkyEnvOwns = false;
        vsg::vec3 mSkyEnvZenith{ -1.f, -1.f, -1.f };
        vsg::vec3 mSkyEnvHorizon{ -1.f, -1.f, -1.f };
        vsg::vec3 mSkyEnvAmbient{ -1.f, -1.f, -1.f };
        bool mGiSkyEnabled = false;
        // Task 0037: rain wetness took effect at start (needs hdr and pbr), its strength, and the current value.
        bool mWetnessEnabled = false;
        // Task 0095: the terrain's puddles from the baked puddle map; the buffer the shader reads (binding 20).
        bool mPuddleMapEnabled = false;
        vsg::ref_ptr<vsg::uintArray> mPuddleMapData;
        std::filesystem::path mPuddlePack;
        float mPuddleSupply = 0.5f;
        float mPuddleAbsorption = 0.1f;
        std::uint32_t mPuddleDebug = 0;
        bool mPuddleKeepStaticNoise = true;
        bool mPuddleTopographic = false;
        float mPuddleTopographicStrength = 1.f;
        // Task 0045: parallax occlusion mapping took effect at start: 0 off, 1 low, 2 medium, 3 high.
        unsigned int mParallaxQuality = 0;
        float mWetnessStrength = 1.f;
        // Task 0038: the clouds' shadows (the data rides in mSkyVisHeader), and which grids it holds now.
        bool mCloudShadowEnabled = false;
        bool mCloudShadowDebug = false;
        float mCloudShadowStrength = 0.75f;
        bool mCloudShadowWritten = false;
        Render::Cloud::Params mCloudParamsWritten;
        const float* mCloudGridWritten[2] = { nullptr, nullptr };
        float mWetness = 0.f;
        // Task 0039: snow and ash cover took effect at start (needs hdr and pbr), its strength, and the current kind
        // and amount.
        bool mCoverEnabled = false;
        float mCoverStrength = 1.f;
        Render::Cover::Kind mCoverKind = Render::Cover::Kind::None;
        float mCoverAmount = 0.f;
        // Task 0040: lit rain and snow took effect at start, lightning took effect (needs hdr), its strength and test
        // age, and the current flash.
        bool mPrecipitationLit = false;
        bool mLightningEnabled = false;
        float mLightningStrength = 1.f;
        float mLightningDebugAge = -1.f;
        float mLightningIntensity = 0.f;
        vsg::vec3 mLightningDirection{ 0.f, 0.f, 1.f };
        float mSkyVisStrength = 1.f;
        vsg::ref_ptr<vsg::vec4Array> mSkyVisPool;
        vsg::ref_ptr<vsg::vec4Array> mSkyVisHeader;
        std::mutex mSkyVisMutex;
        std::set<std::pair<int, int>> mSkyCells;
        bool mSkyCellsChanged = false;
        // The parsed files of the loaded cells; a null entry is a cell with no (or a bad) file.
        std::map<std::pair<int, int>, std::unique_ptr<Render::Gi::SkyVisField>> mSkyFields;
        // The sky colours the header was last written for, to write it again only when they move.
        vsg::vec3 mSkyVisLastSky{ -1.f, -1.f, -1.f };
        vsg::vec3 mSkyVisLastHorizon{ -1.f, -1.f, -1.f };
        bool mSkyVisHeaderWritten = false;
        std::unique_ptr<Render::Tonemap> mTonemap;
        std::unique_ptr<Render::Bloom> mBloom; // null unless [Rendering] hdr bloom
        // Task 0026, stage b: null unless [Rendering] gbuffer. The previous frame's camera, for its motion.
        std::unique_ptr<Render::Velocity> mVelocity;
        // Task 0075: temporal anti-aliasing; null unless [Rendering] taa took effect (it needs hdr, gbuffer and a
        // single-sampled scene). Resolves the scene before the bloom and the tonemap.
        std::unique_ptr<Render::TaaPass> mTaa;
        // Task 0075 (transparents after the resolve): with TAA the main view's depth-sorted bin only collects; the
        // overlay graph, between the resolve and the bloom, draws its elements into the resolved picture.
        vsg::ref_ptr<vsgUtil::DeferredBin> mTransparentBin;
        vsg::ref_ptr<vsg::RenderGraph> mOverlayGraph;
        // Builds the overlay graph's framebuffer over the TAA output (start and every resize).
        bool rebuildTaaOverlay();
        // What the bloom and the tonemap show: the resolved picture with TAA, else the target's colour.
        vsg::ref_ptr<vsg::ImageView> sceneColourView() const;
        vsg::dmat4 mPreviousView;
        vsg::dmat4 mPreviousProjection;
        vsg::dvec3 mPreviousEye;
        bool mHavePreviousCamera = false;
        std::chrono::steady_clock::time_point mPreviousCameraTime;
        void updateVelocity();
        bool mTonemapSteppedAside = false; // a post-processing technique that sets `hdr` is enabled
        vsg::ref_ptr<vsg::RenderGraph> mTonemapGraph;
        vsg::ref_ptr<vsg::CommandGraph> mCommandGraph;
        vsg::ref_ptr<vsg::RecordTraversal> mRecord;
        std::vector<vsg::ref_ptr<vsg::RecordTraversal>> mRecordShadow;
        vsg::ref_ptr<vsgUtil::CompileContext> mCompileContext;
        vsg::ref_ptr<vsg::OperationThreads> mOperationThreads;
        vsg::ref_ptr<vsgUtil::CompileOp> mCompileOp;

        Resource::ResourceSystem* mResourceSystem;

        std::unique_ptr<Reflect> mReflect;
        std::unique_ptr<Scene> mScene;
        std::unique_ptr<Water> mWater;
        std::unique_ptr<Render::SimpleWater> mSimpleWater;
        std::unique_ptr<Terrain::Paging> mTerrain;
        std::unique_ptr<TerrainStorage> mTerrainStorage;
        // Task 0125: the land of a game pack's neutral world, when the player is in one. Declared after the terrain
        // (Paging) that holds a raw pointer to the storage, so that it goes first.
        std::shared_ptr<NeutralTerrain::CellCache> mNeutralCache;
        std::unique_ptr<NeutralTerrain::Storage> mNeutralStorage;
        std::string mNeutralGame;
        std::uint64_t mNeutralWorld = 0;
        NeutralTerrain::CellWindow mNeutralWindow;
        // Task 0095: reads the pack on a worker thread; declared after the terrain storage it reads, so it goes first.
        std::unique_ptr<PuddleMapRuntime> mPuddleMap;
        std::unique_ptr<Terrain::View> mTerrainView;
        // Task 0055: baked terrain tiles for the far field, only with `[WorldLod] enabled` (declared after the
        // terrain storage it reads, so it goes first).
        std::filesystem::path mLodTerrainPack;
        std::filesystem::path mLodRoot;
        // Task 0133: a neutral world's land extent comes from its LOD pack, once that is adopted.
        bool mNeutralExtentPending = false;
        std::unique_ptr<Terrain::TerrainMapSettings> mTerrainMapsForLod;
        std::unique_ptr<LodTerrain> mLodTerrain;
        bool mLodTerrainAttached = false;
        // Task 0060, second review: how often the LOD tiles' coverage changed (each change makes the live terrain
        // rebuild its whole view, synchronously on the main thread) and what that cost at worst.
        std::uint64_t mLastCoverageVersion = 0;
        std::uint32_t mCoverageRebuilds = 0;
        double mCoverageRebuildWorstMs = 0;
        std::unique_ptr<Sky> mSky;
        std::unique_ptr<Render::DebugGeometry> mDebugGeometry;
        std::unique_ptr<Render::DebugGeometry> mNavMeshGeometry;
        // The extensions' debug draw (task 0017 stage 5): depth tested lines, and lines drawn over everything. No
        // storage is made until the first enabled frame.
        std::unique_ptr<Render::DebugGeometry> mExtensionDebugTested;
        std::unique_ptr<Render::DebugGeometry> mExtensionDebugOnTop;
        const openeden::extension::DebugDrawFrame* mExtensionDebugFrame = nullptr;
        bool mExtensionDebugActive = false;
        void updateExtensionDebug();
        std::unique_ptr<FogManager> mFog;
        std::unique_ptr<Effects> mEffects;
        std::unique_ptr<WeatherEffects> mWeatherEffects;
        std::unique_ptr<PrecipitationOccluder> mOccluder;
        bool mOccluderActive = false;
        void updateOccluder(const vsg::vec3& cameraPosition);
        std::unique_ptr<PostProcess> mPostProcess;
        VkExtent2D mAppliedExtent{};
        void updatePostProcess();
        // The scene colours as the game gives them (display-encoded), for the post-processing uniforms
        // (updateSceneData).
        vsg::vec4 mDisplayFogColor{};
        vsg::vec4 mDisplayAmbient{};
        vsg::vec4 mDisplaySkyColor{};
        vsg::mat4 mPostProcessLastView;
        float mPostProcessLastTime = 0.f;
        float mPostProcessWaterHeight = 0.f;
        bool mNeutralWaterMode = false; // task 0129: Render_Water is on because cell-bounded surfaces exist
        bool mPostProcessNight = false;
        unsigned int mPostProcessFrame = 0;
        std::unique_ptr<DistantObjects> mDistantObjects;
        // Task 0059: baked object clusters, only with `[WorldLod] object clusters`.
        std::filesystem::path mLodClusterPack;
        std::unique_ptr<LodClusters> mLodClusters;
        // Task 0136: the wind of Skyrim trees, only with `[Game] neutral world` and `neutral tree sway`.
        std::unique_ptr<SkyrimTreeSway::Driver> mTreeSway;
        std::optional<float> mTreeWindOverride; // OPENEDEN_NEUTRAL_TREE_WIND, a developer aid
        bool mLodClustersAttached = false;
        std::uint64_t mLodClustersPushed = 0;
        ActiveCellGrid mActiveGrid;
        std::set<std::pair<int, int>> mUnbuiltCells; // task 0060: active cells whose objects are still being built
        void updateDistantObjects(const vsg::vec3& cameraPosition, float dt);
        void updateLodClusters(const vsg::vec3& cameraPosition, float dt);
        std::unique_ptr<Rain> mRain;
        Rain::Parameters mRainParameters;
        std::unique_ptr<WaterRipples> mWaterRipples;
        std::unique_ptr<Projectiles> mProjectiles;
        std::unique_ptr<Player> mPlayer;
        std::unique_ptr<Camera> mCamera;
        std::unique_ptr<Groundcover> mGroundcover;
        std::atomic<const MWWorld::GroundcoverStore*> mGroundcoverStore{ nullptr };
        vsg::vec3 mGroundcoverPlayerPosition; // set in update(), read by updateSceneData()
        void updateGroundcover(const vsg::vec3& cameraPosition);
        const DetourNavigator::Navigator* mNavigator = nullptr;
        vsg::ref_ptr<vsgUtil::SuspendRenderPass> mSuspend;
        // The save thumbnail's copy (owned by the render engine; null without a window).
        Render::FrameCapture* mThumbnailCapture = nullptr;
        std::unique_ptr<ScreenshotInterface> mScreenshotInterface;
        std::unique_ptr<Preview> mPreview;
        std::unique_ptr<WorldMap> mWorldMap;
        std::unique_ptr<Map> mMap;

        vsg::vec4 mAmbientColor;
        vsg::vec4 mLightDiffuse;
        vsg::vec4 mLightSpecular;
        vsg::vec3 mLightPosition;
        vsg::vec4 mSunLightPosition;
        float mMinimumAmbientLuminance;
        float mNightEyeFactor = 0;

        float mNearClip;
        // Task 0066: the one view distance is four (Lod::computeViewLimits): the live paging range (mViewDistance, what
        // getViewDistance returns), the tile range, the far plane and the fog end. All equal unless `map view` is on.
        float mViewDistance = 0;
        float mTileRange = 0;
        float mFarPlane = 0;
        float mFogRange = 0;
        double mLandDiagonal = 0; // the loaded world's land reach, for `view distance = auto`
        bool mViewLimitsOverridden = false; // a script set the distance (Lua camera.setViewDistance)
        // Task 0066 stage c: the shaders were built with the aerial perspective in them (fog.settings, read at start),
        // so only then may the scene's fog scale carry its mark (View::Scene::setFogRange).
        bool mAerialShader = false;
        // Task 0066 stage e: the hardware tier of the whole-map view (only with `map view` on; otherwise inactive and
        // every number below is the setting's own).
        bool mFarTierActive = false;
        // Task 0080: the video memory scale-back. Null unless [VideoMemory] enabled (and a system that can be reduced).
        std::unique_ptr<Render::VideoMemory::Scaler> mVramScaler;
        void setupVideoMemory();
        // Task 0080 stage d: texture mip swaps (the `texture mips` system, or OPENEDEN_TEXTURE_MIPS_TEST). Null
        // otherwise.
        std::unique_ptr<class TextureMipsDriver> mTextureMips;
        vsg::ref_ptr<vsg::Group> mTextureMipsRoots; // scratch: the roots the swapper scans
        bool mTextureMipsWanted = false;
        std::string mTextureMipsScript;
        void updateTextureMips(float dt);
        bool ensureTextureMips();
        Lod::FarTier mFarTierName = Lod::FarTier::High;
        Lod::FarTierSettings mFarTier;
        std::uint32_t mFarLevels = 0; // the highest tile and cluster level read and drawn; 0 means all
        int mFarTextureSize = 0; // longest side of far cluster pictures; 0 means no further cap
        const Lod::FarTierSettings* farTierOrNull() const { return mFarTierActive ? &mFarTier : nullptr; }
        void chooseFarTier();
        std::uint64_t farMemoryBudgetBytes() const;
        std::uint64_t farTerrainBudgetBytes() const;
        void applyViewLimits(const Lod::ViewLimits& limits, bool updateProjection);
        std::optional<float> mFieldOfViewOverride = 0;
        float mFieldOfView;

        std::size_t mNavMeshNumber = 0;
        // std::unique_ptr<ActorsPaths> mActorsPaths;
        std::unique_ptr<NavMesh> mNavMesh;
        // std::unique_ptr<RecastMesh> mRecastMesh;
        // std::unique_ptr<Pathgrid> mPathgrid;

        void operator=(const RenderManager&);
        RenderManager(const RenderManager&);
    };
}

#endif
