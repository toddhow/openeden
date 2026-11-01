#ifndef CSM_PHYSICS_DEBUGLINEDRAWER_H
#define CSM_PHYSICS_DEBUGLINEDRAWER_H

#include <vector>

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyManager.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Renderer/DebugRenderer.h>
// clang-format on

namespace CSMPhysics
{
    /// One coloured line, in the world's length unit (Morrowind units in the editor).
    struct DebugLine
    {
        float mFrom[3];
        float mTo[3];
        float mColour[4];
    };

    /// Turns what Jolt would draw for a world into a list of lines (task 0030). It is a copy of the drawing methods of
    /// MWRender::JoltDebugDrawer (apps/openmw/mwrender/joltdebugdraw.cpp) without that class's renderer, so it needs no
    /// GPU: triangles and triangle batches become wire edges, lines keep Jolt's colour, text is not drawn. A later task
    /// should give the game and the editor one class in components/.
    ///
    /// JPH::DebugRenderer is a process singleton, so only one DebugLineDrawer may exist at a time. Main thread only.
    class DebugLineDrawer final : public JPH::DebugRenderer
    {
    public:
        DebugLineDrawer();
        ~DebugLineDrawer() override;

        DebugLineDrawer(const DebugLineDrawer&) = delete;
        DebugLineDrawer& operator=(const DebugLineDrawer&) = delete;

        /// Replaces the list with the world's bodies' shapes, constraints and constraint limits.
        void rebuild(JPH::PhysicsSystem& world);

        const std::vector<DebugLine>& lines() const { return mLines; }

        // JPH::DebugRenderer
        void DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to, JPH::ColorArg colour) override;
        void DrawTriangle(JPH::RVec3Arg v1, JPH::RVec3Arg v2, JPH::RVec3Arg v3, JPH::ColorArg colour,
            ECastShadow castShadow = ECastShadow::Off) override;
        Batch CreateTriangleBatch(const Triangle* triangles, int triangleCount) override;
        Batch CreateTriangleBatch(
            const Vertex* vertices, int vertexCount, const JPH::uint32* indices, int indexCount) override;
        void DrawGeometry(JPH::RMat44Arg modelMatrix, const JPH::AABox& worldSpaceBounds, float lodScaleSq,
            JPH::ColorArg modelColour, const GeometryRef& geometry, ECullMode cullMode = ECullMode::CullBackFace,
            ECastShadow castShadow = ECastShadow::On, EDrawMode drawMode = EDrawMode::Solid) override;
        void DrawText3D(JPH::RVec3Arg position, const std::string_view& text, JPH::ColorArg colour = JPH::Color::sWhite,
            float height = 0.5f) override;

    private:
        class BatchImpl;

        void addEdges(const JPH::RVec3& a, const JPH::RVec3& b, const JPH::RVec3& c);

        std::vector<DebugLine> mLines;
        JPH::BodyManager::DrawSettings mDrawSettings;
    };
}

#endif
