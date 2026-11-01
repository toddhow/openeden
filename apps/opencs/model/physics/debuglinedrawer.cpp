#include "debuglinedrawer.hpp"

#include <atomic>
#include <cstddef>

namespace CSMPhysics
{
    namespace
    {
        // Triangles and batches are drawn white, as the game's drawer does.
        constexpr float cTriangleColour[4] = { 1.f, 1.f, 1.f, 1.f };

        DebugLine makeLine(const JPH::RVec3& from, const JPH::RVec3& to, const float colour[4])
        {
            DebugLine line;
            line.mFrom[0] = static_cast<float>(from.GetX());
            line.mFrom[1] = static_cast<float>(from.GetY());
            line.mFrom[2] = static_cast<float>(from.GetZ());
            line.mTo[0] = static_cast<float>(to.GetX());
            line.mTo[1] = static_cast<float>(to.GetY());
            line.mTo[2] = static_cast<float>(to.GetZ());
            for (int i = 0; i < 4; ++i)
                line.mColour[i] = colour[i];
            return line;
        }
    }

    /// A triangle batch kept as plain corners (three per triangle, in the batch's model space).
    class DebugLineDrawer::BatchImpl : public JPH::RefTargetVirtual
    {
    public:
        JPH_OVERRIDE_NEW_DELETE

        void AddRef() override { ++mRefCount; }
        void Release() override
        {
            if (--mRefCount == 0)
                delete this;
        }

        std::atomic<uint32_t> mRefCount = 0;
        std::vector<JPH::Float3> mCorners;
    };

    DebugLineDrawer::DebugLineDrawer()
    {
        JPH::DebugRenderer::Initialize();

        // As the game's drawer: only the shapes, the verbose overlays are off.
        mDrawSettings.mDrawCenterOfMassTransform = false;
        mDrawSettings.mDrawGetSupportFunction = false;
        mDrawSettings.mDrawSupportDirection = false;
        mDrawSettings.mDrawGetSupportingFace = false;
        mDrawSettings.mDrawBoundingBox = false;
        mDrawSettings.mDrawWorldTransform = false;
        mDrawSettings.mDrawMassAndInertia = false;
        mDrawSettings.mDrawSleepStats = false;
        mDrawSettings.mDrawShape = true;
    }

    DebugLineDrawer::~DebugLineDrawer() = default;

    void DebugLineDrawer::rebuild(JPH::PhysicsSystem& world)
    {
        mLines.clear();
        world.DrawBodies(mDrawSettings, this);
        world.DrawConstraints(this);
        world.DrawConstraintLimits(this);
    }

    void DebugLineDrawer::addEdges(const JPH::RVec3& a, const JPH::RVec3& b, const JPH::RVec3& c)
    {
        mLines.push_back(makeLine(a, b, cTriangleColour));
        mLines.push_back(makeLine(b, c, cTriangleColour));
        mLines.push_back(makeLine(c, a, cTriangleColour));
    }

    void DebugLineDrawer::DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to, JPH::ColorArg colour)
    {
        const float rgba[4] = { colour.r / 255.f, colour.g / 255.f, colour.b / 255.f, 1.f };
        mLines.push_back(makeLine(from, to, rgba));
    }

    void DebugLineDrawer::DrawTriangle(JPH::RVec3Arg v1, JPH::RVec3Arg v2, JPH::RVec3Arg v3, JPH::ColorArg, ECastShadow)
    {
        addEdges(v1, v2, v3);
    }

    JPH::DebugRenderer::Batch DebugLineDrawer::CreateTriangleBatch(const Triangle* triangles, int triangleCount)
    {
        BatchImpl* batch = new BatchImpl;
        if (triangles == nullptr || triangleCount <= 0)
            return batch;
        batch->mCorners.reserve(static_cast<std::size_t>(triangleCount) * 3);
        for (int t = 0; t < triangleCount; ++t)
            for (const Vertex& vertex : triangles[t].mV)
                batch->mCorners.push_back(vertex.mPosition);
        return batch;
    }

    JPH::DebugRenderer::Batch DebugLineDrawer::CreateTriangleBatch(
        const Vertex* vertices, int vertexCount, const JPH::uint32* indices, int indexCount)
    {
        BatchImpl* batch = new BatchImpl;
        if (vertices == nullptr || indices == nullptr || vertexCount <= 0 || indexCount <= 0)
            return batch;
        batch->mCorners.reserve(static_cast<std::size_t>(indexCount));
        for (int i = 0; i + 2 < indexCount; i += 3)
        {
            if (indices[i] >= static_cast<JPH::uint32>(vertexCount)
                || indices[i + 1] >= static_cast<JPH::uint32>(vertexCount)
                || indices[i + 2] >= static_cast<JPH::uint32>(vertexCount))
                continue;
            for (int corner = 0; corner < 3; ++corner)
                batch->mCorners.push_back(vertices[indices[i + corner]].mPosition);
        }
        return batch;
    }

    void DebugLineDrawer::DrawGeometry(JPH::RMat44Arg modelMatrix, const JPH::AABox&, float, JPH::ColorArg,
        const GeometryRef& geometry, ECullMode, ECastShadow, EDrawMode)
    {
        // Every level of detail is drawn, as the game's drawer does.
        for (const LOD& lod : geometry->mLODs)
        {
            const BatchImpl* batch = static_cast<const BatchImpl*>(lod.mTriangleBatch.GetPtr());
            if (batch == nullptr)
                continue;
            const std::vector<JPH::Float3>& corners = batch->mCorners;
            for (std::size_t i = 0; i + 2 < corners.size(); i += 3)
                addEdges(modelMatrix * JPH::Vec3(corners[i]), modelMatrix * JPH::Vec3(corners[i + 1]),
                    modelMatrix * JPH::Vec3(corners[i + 2]));
        }
    }

    void DebugLineDrawer::DrawText3D(JPH::RVec3Arg, const std::string_view&, JPH::ColorArg, float)
    {
        // Not drawn.
    }
}
