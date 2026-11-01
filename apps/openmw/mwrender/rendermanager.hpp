#ifndef VSGOPENMW_MWRENDER_RENDERMANAGER_H
#define VSGOPENMW_MWRENDER_RENDERMANAGER_H

#include "distantobjectcells.hpp"
#include "rain.hpp"
#include "renderinginterface.hpp"
#include "rendermode.hpp"
#include "waterripples.hpp"

#include <array>
#include <atomic>
#include <deque>
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
#include <components/render/probes.hpp>
#include <components/render/reflectionprobes.hpp>
#include <components/render/skyvisibility.hpp>
#include <components/render/tonemap.hpp>
#include <components/settings/settings.hpp>

namespace MWWorld
{
    class Ptr;
    class ConstPtr;
    class CellStore;
    class Cell;
    class ESMStore;
    class GroundcoverStore;
}
namespace Resource
{
    class ResourceSystem;
}
namespace Render
{
    class Bloom;
    class Velocity;
    class AmbientOcclusion;
    class DebugGeometry;
    class Engine;
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
    struct CompileOp;
    class CompileContext;
    class UpdateThreads;
}
namespace View
{
    class LightGrid;
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
        void setNavMeshMode(Settings::NavMeshRenderMode value);
        Preview* getPreview() { return mPreview.get(); }
        LandManager* getLandManager() const;
        ScreenshotInterface& getScreenshotInterface() { return *mScreenshotInterface; }

        void setNightEyeFactor(float factor);
        void setAmbientColour(const vsg::vec4& colour);
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

        bool pagingEnableObject(int type, const MWWorld::ConstPtr& ptr, bool enabled);
        void pagingBlacklistObject(int type, const MWWorld::ConstPtr& ptr);
        bool pagingUnlockCache();
        void getPagedRefnums(const vsg::ivec4& activeGrid, std::set<ESM::RefNum>& out);

        void updateExtents(uint32_t w, uint32_t h);
        // Applies the swapchain's current extent to the camera, the render areas and post-processing; call once per
        // frame before recording, whatever the game state.
        void syncExtents();

        vsg::dmat4 getProjectionMatrix() const;

    private:
        vsg::ref_ptr<vsg::Node> createMapScene();
        vsg::Group::Children createCastShadowScene(bool exterior);
        void ensureCompiled();
        void createIntersectorScene();
        void updateSceneChildren();
        void updateNavMesh();
        void updateSceneData();
        void updateProjectionMatrix();
        void setupScreenshotInterface();
        void setFirstPersonMode(bool enabled);
        void updateTerrain(const vsg::dvec3& pos);

        struct Mode
        {
            bool active = true;
            bool toggled = true;
            operator bool() const { return active && toggled; }
        };
        std::vector<Mode> mRenderModes;

        Render::Engine& mEngine;
        // Null unless [Rendering] gpu timing is on and the device supports it (task 0023). Owned by the Engine.
        Render::GpuTimer* mGpuTimer = nullptr;
        static constexpr std::size_t noPass = static_cast<std::size_t>(-1);
        std::size_t mReflectPass = noPass;
        vsg::ref_ptr<vsg::StateGroup> mSceneRoot;
        vsg::ref_ptr<vsg::Group> mIntersectorScene;
        vsg::ref_ptr<vsg::Switch> mPlayerGroup;
        vsg::ref_ptr<vsg::View> mView;
        std::unique_ptr<Shadow> mShadow;
        std::unique_ptr<View::LightGrid> mLightGrid;
        std::unique_ptr<View::Scene> mSceneData;
        vsg::ref_ptr<vsg::Node> mGui;
        vsg::ref_ptr<vsg::Camera> mSceneCamera;
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
        // Stage d: the sky environment image, in the reflection probe buffer while outdoors.
        void updateSkyEnvironment();
        // Stage e: `gi debug` markers of the sky-visibility field around the camera.
        void updateSkyDebug();
        bool mSkyDebugDrawn = false;
        std::array<int, 4> mSkyDebugKey{};
        const Render::Gi::SkyVisField* mSkyDebugField = nullptr;
        bool mSkyEnvOwns = false;
        vsg::vec3 mSkyEnvZenith{ -1.f, -1.f, -1.f };
        vsg::vec3 mSkyEnvHorizon{ -1.f, -1.f, -1.f };
        vsg::vec3 mSkyEnvAmbient{ -1.f, -1.f, -1.f };
        bool mGiSkyEnabled = false;
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
        std::unique_ptr<Terrain::View> mTerrainView;
        std::unique_ptr<Sky> mSky;
        std::unique_ptr<Render::DebugGeometry> mDebugGeometry;
        std::unique_ptr<Render::DebugGeometry> mNavMeshGeometry;
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
        bool mPostProcessNight = false;
        unsigned int mPostProcessFrame = 0;
        std::unique_ptr<DistantObjects> mDistantObjects;
        ActiveCellGrid mActiveGrid;
        void updateDistantObjects(const vsg::vec3& cameraPosition);
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
        vsg::ref_ptr<vsg::Switch> mScreenshotSwitch;
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
        float mViewDistance;
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
