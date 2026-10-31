#ifndef OPENMW_MWRENDER_JOLTDEBUGDRAW_H
#define OPENMW_MWRENDER_JOLTDEBUGDRAW_H

#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>


#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyManager.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Renderer/DebugRenderer.h>

#include <components/misc/vsgmath.hpp>

namespace Render
{
    class DebugGeometry;
}

namespace MWRender
{
    /**
     * @brief Draws Jolt's bodies, constraints and recent collisions (the `tcb` console toggle).
     *
     * openeden-vsg (task 0011): ported from the OSG build. Jolt's draw calls are submitted as lines to the
     * renderer's DebugGeometry; triangles and cached triangle batches (mesh shapes) are drawn as wireframe edges,
     * transformed on the CPU, as the OSG build drew them with a line polygon mode. Owned by MWPhysics::PhysicsSystem;
     * the DebugGeometry is owned by RenderManager and outlives it. step() runs on the main thread.
     */
    class JoltDebugDrawer final : public JPH::DebugRenderer
    {
    private:
        struct CollisionView
        {
            vsg::vec3 mOrig;
            vsg::vec3 mEnd;
            std::chrono::time_point<std::chrono::steady_clock> mCreated;
            CollisionView(vsg::vec3 orig, vsg::vec3 normal)
                : mOrig(orig)
                , mEnd(orig + normal * 20)
                , mCreated(std::chrono::steady_clock::now())
            {
            }
        };

        class BatchImpl : public JPH::RefTargetVirtual
        {
        public:
            JPH_OVERRIDE_NEW_DELETE

            virtual void AddRef() override { ++mRefCount; }
            virtual void Release() override
            {
                if (--mRefCount == 0)
                    delete this;
            }

            std::atomic<uint32_t> mRefCount = 0;
            // Triangle corners in the batch's model space, three per triangle.
            std::vector<JPH::Float3> mCorners;
        };

        Render::DebugGeometry& mGeometry;
        JPH::PhysicsSystem* mPhysicsSystem;
        bool mDebugOn = false;

        std::mutex mCollisionMutex;
        std::vector<CollisionView> mCollisionViews;

        JPH::BodyManager::DrawSettings mBodyDrawSettings;

        void showCollisions();
        void addEdges(const JPH::RVec3& a, const JPH::RVec3& b, const JPH::RVec3& c);

    public:
        JPH_OVERRIDE_NEW_DELETE

        JoltDebugDrawer(Render::DebugGeometry& geometry, JPH::PhysicsSystem* physicsSystem, int debugMode = 1);
        ~JoltDebugDrawer();

        void step();

        void addCollision(const vsg::vec3& orig, const vsg::vec3& normal);

        // 0 for off, anything else for on.
        void setDebugMode(int isOn);

        // 0 for off, anything else for on.
        int getDebugMode() const;

        // Jolt overrides
        void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) override;

        void DrawTriangle(JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3, JPH::ColorArg inColor,
            JPH::DebugRenderer::ECastShadow inCastShadow = JPH::DebugRenderer::ECastShadow::Off) override;

        JPH::DebugRenderer::Batch CreateTriangleBatch(
            const JPH::DebugRenderer::Triangle* inTriangles, int inTriangleCount) override;

        JPH::DebugRenderer::Batch CreateTriangleBatch(const JPH::DebugRenderer::Vertex* inVertices, int inVertexCount,
            const JPH::uint32* inIndices, int inIndexCount) override;

        void DrawGeometry(JPH::RMat44Arg inModelMatrix, const JPH::AABox& inWorldSpaceBounds, float inLODScaleSq,
            JPH::ColorArg inModelColor, const JPH::DebugRenderer::GeometryRef& inGeometry,
            JPH::DebugRenderer::ECullMode inCullMode = JPH::DebugRenderer::ECullMode::CullBackFace,
            JPH::DebugRenderer::ECastShadow inCastShadow = JPH::DebugRenderer::ECastShadow::On,
            JPH::DebugRenderer::EDrawMode inDrawMode = JPH::DebugRenderer::EDrawMode::Solid) override;

        void DrawText3D(JPH::RVec3Arg inPosition, const std::string_view& inString,
            JPH::ColorArg inColor = JPH::Color::sWhite, float inHeight = 0.5f) override;
    };
}

#endif
