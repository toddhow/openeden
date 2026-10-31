#ifndef OPENMW_MWRENDER_NAVMESH_H
#define OPENMW_MWRENDER_NAVMESH_H

#include <components/detournavigator/tileposition.hpp>
#include <components/detournavigator/version.hpp>
#include <components/misc/guarded.hpp>
#include <components/sceneutil/detourdebugdraw.hpp>
#include <components/settings/navmeshrendermode.hpp>

#include <cstddef>
#include <map>
#include <memory>
#include <vector>

namespace DetourNavigator
{
    class NavMeshCacheItem;
    struct Settings;
}

namespace Render
{
    class DebugGeometry;
}

namespace MWRender
{
    /**
     * @brief Draws one navmesh as debug geometry (OpenEden task 0011; OpenMW's OSG version built per-tile scene
     * graphs on a work queue).
     *
     * Each tile is drawn into a cache of primitives when its version changes, and the cached tiles are submitted to
     * the DebugGeometry it owns the contents of (RenderManager owns both). Runs on the main thread from
     * RenderManager::update; while drawing it holds the navmesh's read lock, so the navmesh updater waits. Nothing
     * runs and nothing is allocated while disabled.
     */
    class NavMesh
    {
    public:
        explicit NavMesh(Render::DebugGeometry& geometry, bool enabled, Settings::NavMeshRenderMode mode);
        ~NavMesh();

        bool toggle();

        void update(const std::shared_ptr<Misc::ScopeGuarded<DetourNavigator::NavMeshCacheItem>>& navMesh,
            std::size_t id, const DetourNavigator::Settings& settings);

        void reset();

        void enable();

        void disable();

        bool isEnabled() const { return mEnabled; }

        void setMode(Settings::NavMeshRenderMode value);

    private:
        /// Primitives of one tile, kept to submit again when other tiles change.
        struct Primitives final : SceneUtil::DebugPrimitiveSink
        {
            std::vector<vsg::vec3> mLineVertices;
            std::vector<vsg::vec4> mLineColors;
            std::vector<vsg::vec3> mTriangleVertices;
            std::vector<vsg::vec4> mTriangleColors;

            void addLine(const vsg::vec3& from, const vsg::vec3& to, const vsg::vec4& color) override;
            void addTriangle(
                const vsg::vec3& a, const vsg::vec3& b, const vsg::vec3& c, const vsg::vec4& color) override;
            void submit(SceneUtil::DebugPrimitiveSink& sink) const;
        };

        struct Tile
        {
            DetourNavigator::Version mVersion;
            Primitives mPrimitives;
        };

        void submit();

        Render::DebugGeometry& mGeometry;
        bool mEnabled;
        Settings::NavMeshRenderMode mMode;
        std::size_t mId;
        DetourNavigator::Version mVersion;
        std::map<DetourNavigator::TilePosition, Tile> mTiles;
    };
}

#endif
