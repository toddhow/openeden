#include "joltdebugdraw.hpp"

#include <algorithm>

#include <components/misc/convert.hpp>

#include "debuggeometry.hpp"

namespace MWRender
{
    namespace
    {
        // The OSG build drew triangles and batches without vertex colours, so white; lines keep Jolt's colour.
        const vsg::vec4 sTriangleColor(1.f, 1.f, 1.f, 1.f);
        const vsg::vec4 sCollisionColor(1.f, 0.f, 0.f, 1.f);
        constexpr auto sCollisionLifetime = std::chrono::seconds(2);

        vsg::vec3 toVsg(const JPH::RVec3& v)
        {
            return vsg::vec3(static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ()));
        }

        vsg::vec3 toVsg(const osg::Vec3f& v)
        {
            return vsg::vec3(v.x(), v.y(), v.z());
        }

        vsg::vec4 toVsg(JPH::ColorArg color)
        {
            return vsg::vec4(color.r / 255.f, color.g / 255.f, color.b / 255.f, 1.f);
        }
    }

    JoltDebugDrawer::JoltDebugDrawer(DebugGeometry& geometry, JPH::PhysicsSystem* physicsSystem, int debugMode)
        : mGeometry(geometry)
        , mPhysicsSystem(physicsSystem)
    {
        JoltDebugDrawer::setDebugMode(debugMode);

        JPH::DebugRenderer::Initialize();

        // Keep expensive/verbose overlays disabled by default for frame-time stability.
        mBodyDrawSettings.mDrawCenterOfMassTransform = false;
        mBodyDrawSettings.mDrawGetSupportFunction = false;
        mBodyDrawSettings.mDrawSupportDirection = false;
        mBodyDrawSettings.mDrawGetSupportingFace = false;
        mBodyDrawSettings.mDrawBoundingBox = false;
        mBodyDrawSettings.mDrawWorldTransform = false;
        mBodyDrawSettings.mDrawMassAndInertia = false;
        mBodyDrawSettings.mDrawSleepStats = false;
        mBodyDrawSettings.mDrawShape = true;
    }

    JoltDebugDrawer::~JoltDebugDrawer()
    {
        mGeometry.setEnabled(false);
    }

    void JoltDebugDrawer::addCollision(const osg::Vec3f& orig, const osg::Vec3f& normal)
    {
        std::scoped_lock lock(mCollisionMutex);
        mCollisionViews.emplace_back(orig, normal);
    }

    void JoltDebugDrawer::showCollisions()
    {
        std::scoped_lock lock(mCollisionMutex);
        const auto now = std::chrono::steady_clock::now();
        for (const auto& view : mCollisionViews)
        {
            if (now - view.mCreated < sCollisionLifetime)
                mGeometry.addLine(toVsg(view.mOrig), toVsg(view.mEnd), sCollisionColor);
        }
        mCollisionViews.erase(std::remove_if(mCollisionViews.begin(), mCollisionViews.end(),
                                  [&now](const CollisionView& view) { return now - view.mCreated >= sCollisionLifetime; }),
            mCollisionViews.end());
    }

    void JoltDebugDrawer::step()
    {
        if (!mDebugOn)
            return;
        mGeometry.begin();
        mPhysicsSystem->DrawBodies(mBodyDrawSettings, static_cast<JPH::DebugRenderer*>(this));
        mPhysicsSystem->DrawConstraints(static_cast<JPH::DebugRenderer*>(this));
        mPhysicsSystem->DrawConstraintLimits(static_cast<JPH::DebugRenderer*>(this));
        mPhysicsSystem->DrawConstraintReferenceFrame(static_cast<JPH::DebugRenderer*>(this));
        showCollisions();
        mGeometry.end();
    }

    void JoltDebugDrawer::setDebugMode(int isOn)
    {
        mDebugOn = (isOn != 0);
        mGeometry.setEnabled(mDebugOn);
    }

    int JoltDebugDrawer::getDebugMode() const
    {
        return mDebugOn;
    }

    void JoltDebugDrawer::addEdges(const JPH::RVec3& a, const JPH::RVec3& b, const JPH::RVec3& c)
    {
        const vsg::vec3 va = toVsg(a);
        const vsg::vec3 vb = toVsg(b);
        const vsg::vec3 vc = toVsg(c);
        mGeometry.addLine(va, vb, sTriangleColor);
        mGeometry.addLine(vb, vc, sTriangleColor);
        mGeometry.addLine(vc, va, sTriangleColor);
    }

    void JoltDebugDrawer::DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor)
    {
        mGeometry.addLine(toVsg(inFrom), toVsg(inTo), toVsg(inColor));
    }

    void JoltDebugDrawer::DrawTriangle(JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3,
        JPH::ColorArg /*inColor*/, JPH::DebugRenderer::ECastShadow /*inCastShadow*/)
    {
        addEdges(inV1, inV2, inV3);
    }

    JPH::DebugRenderer::Batch JoltDebugDrawer::CreateTriangleBatch(
        const JPH::DebugRenderer::Triangle* inTriangles, int inTriangleCount)
    {
        BatchImpl* batch = new BatchImpl;
        if (inTriangles == nullptr || inTriangleCount <= 0)
            return batch;
        batch->mCorners.reserve(static_cast<std::size_t>(inTriangleCount) * 3);
        for (int t = 0; t < inTriangleCount; ++t)
            for (const JPH::DebugRenderer::Vertex& vertex : inTriangles[t].mV)
                batch->mCorners.push_back(vertex.mPosition);
        return batch;
    }

    JPH::DebugRenderer::Batch JoltDebugDrawer::CreateTriangleBatch(const JPH::DebugRenderer::Vertex* inVertices,
        int inVertexCount, const JPH::uint32* inIndices, int inIndexCount)
    {
        BatchImpl* batch = new BatchImpl;
        if (inVertices == nullptr || inIndices == nullptr || inVertexCount <= 0 || inIndexCount <= 0)
            return batch;
        batch->mCorners.reserve(static_cast<std::size_t>(inIndexCount));
        for (int i = 0; i + 2 < inIndexCount; i += 3)
        {
            if (inIndices[i] >= static_cast<JPH::uint32>(inVertexCount)
                || inIndices[i + 1] >= static_cast<JPH::uint32>(inVertexCount)
                || inIndices[i + 2] >= static_cast<JPH::uint32>(inVertexCount))
                continue;
            for (int corner = 0; corner < 3; ++corner)
                batch->mCorners.push_back(inVertices[inIndices[i + corner]].mPosition);
        }
        return batch;
    }

    void JoltDebugDrawer::DrawGeometry(JPH::RMat44Arg inModelMatrix, const JPH::AABox& /*inWorldSpaceBounds*/,
        float /*inLODScaleSq*/, JPH::ColorArg /*inModelColor*/, const JPH::DebugRenderer::GeometryRef& inGeometry,
        JPH::DebugRenderer::ECullMode /*inCullMode*/, JPH::DebugRenderer::ECastShadow /*inCastShadow*/,
        JPH::DebugRenderer::EDrawMode /*inDrawMode*/)
    {
        // As the OSG build, every LOD of the geometry is drawn.
        for (const LOD& lod : inGeometry->mLODs)
        {
            const BatchImpl* batch = static_cast<const BatchImpl*>(lod.mTriangleBatch.GetPtr());
            if (batch == nullptr)
                continue;
            const std::vector<JPH::Float3>& corners = batch->mCorners;
            for (std::size_t i = 0; i + 2 < corners.size(); i += 3)
            {
                addEdges(inModelMatrix * JPH::Vec3(corners[i]), inModelMatrix * JPH::Vec3(corners[i + 1]),
                    inModelMatrix * JPH::Vec3(corners[i + 2]));
            }
        }
    }

    void JoltDebugDrawer::DrawText3D(
        JPH::RVec3Arg /*inPosition*/, const std::string_view& /*inString*/, JPH::ColorArg /*inColor*/, float /*inHeight*/)
    {
        // Not drawn, as on the OSG build.
    }
}
