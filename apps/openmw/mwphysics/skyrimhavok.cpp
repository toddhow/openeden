#include "skyrimhavok.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <vector>

#include <Jolt/Jolt.h>

#include <Jolt/Geometry/IndexedTriangle.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include <components/nifscene/havok.hpp>

namespace MWPhysics::SkyrimHavok
{
    namespace
    {
        namespace H = NifScene::Havok;

        // A rigid placement: p -> scale * R * p + t (R row-major, as the NIF stores it).
        struct Pose
        {
            std::array<double, 9> r{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
            std::array<double, 3> t{ 0, 0, 0 };
            double s = 1.0;
        };

        std::array<double, 3> apply(const Pose& p, const std::array<double, 3>& v)
        {
            std::array<double, 3> out{};
            for (int row = 0; row < 3; ++row)
                out[row] = p.s * (p.r[row * 3] * v[0] + p.r[row * 3 + 1] * v[1] + p.r[row * 3 + 2] * v[2]) + p.t[row];
            return out;
        }

        Pose compose(const Pose& parent, const Pose& child)
        {
            Pose out;
            for (int row = 0; row < 3; ++row)
                for (int column = 0; column < 3; ++column)
                {
                    double sum = 0.0;
                    for (int k = 0; k < 3; ++k)
                        sum += parent.r[row * 3 + k] * child.r[k * 3 + column];
                    out.r[row * 3 + column] = sum;
                }
            out.s = parent.s * child.s;
            out.t = apply(parent, child.t);
            return out;
        }

        Pose fromTransform(const NifScene::Transform& t)
        {
            Pose p;
            for (int i = 0; i < 9; ++i)
                p.r[i] = t.mRotation[i];
            for (int i = 0; i < 3; ++i)
                p.t[i] = t.mTranslation[i];
            p.s = t.mScale;
            return p;
        }

        // The world pose of a node, walking up its parents (a chain longer than the node count is a loop: cut).
        Pose nodeWorld(const NifScene::Scene& scene, std::uint32_t node)
        {
            std::vector<std::uint32_t> chain;
            for (std::uint32_t at = node; at < scene.mNodes.size() && chain.size() <= scene.mNodes.size();
                 at = scene.mNodes[at].mParent)
                chain.push_back(at);
            Pose world;
            for (std::size_t i = chain.size(); i-- > 0;)
                world = compose(world, fromTransform(scene.mNodes[chain[i]].mTransform));
            return world;
        }

        // The pose of a unit quaternion (x, y, z, w) as a rotation matrix.
        Pose fromQuat(const H::Vec4& q)
        {
            Pose p;
            const double x = q[0], y = q[1], z = q[2], w = q[3];
            p.r = { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w),
                1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 2 * (x * z - y * w), 2 * (y * z + x * w),
                1 - 2 * (x * x + y * y) };
            return p;
        }

        JPH::Quat toJoltQuat(const Pose& p)
        {
            const auto& m = p.r;
            const double trace = m[0] + m[4] + m[8];
            double x, y, z, w;
            if (trace > 0.0)
            {
                const double s = std::sqrt(trace + 1.0) * 2.0;
                w = 0.25 * s;
                x = (m[7] - m[5]) / s;
                y = (m[2] - m[6]) / s;
                z = (m[3] - m[1]) / s;
            }
            else if (m[0] > m[4] && m[0] > m[8])
            {
                const double s = std::sqrt(1.0 + m[0] - m[4] - m[8]) * 2.0;
                w = (m[7] - m[5]) / s;
                x = 0.25 * s;
                y = (m[1] + m[3]) / s;
                z = (m[2] + m[6]) / s;
            }
            else if (m[4] > m[8])
            {
                const double s = std::sqrt(1.0 + m[4] - m[0] - m[8]) * 2.0;
                w = (m[2] - m[6]) / s;
                x = (m[1] + m[3]) / s;
                y = 0.25 * s;
                z = (m[5] + m[7]) / s;
            }
            else
            {
                const double s = std::sqrt(1.0 + m[8] - m[0] - m[4]) * 2.0;
                w = (m[3] - m[1]) / s;
                x = (m[2] + m[6]) / s;
                y = (m[5] + m[7]) / s;
                z = 0.25 * s;
            }
            return JPH::Quat(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z),
                static_cast<float>(w))
                .Normalized();
        }

        JPH::Vec3 jv(const H::Vec3& v, double u)
        {
            return JPH::Vec3(static_cast<float>(v[0] * u), static_cast<float>(v[1] * u), static_cast<float>(v[2] * u));
        }

        class Builder
        {
        public:
            Builder(const H::Collision& collision, BuildReport& report, std::string& why)
                : mCollision(collision)
                , mReport(report)
                , mWhy(why)
            {
            }

            // The Jolt shape of a Havok shape with all lengths multiplied by `u`; null (with mWhy set) when it cannot
            // be represented.
            JPH::RefConst<JPH::Shape> build(std::uint32_t index, double u, std::size_t depth)
            {
                if (index >= mCollision.mShapes.size() || depth > 32)
                    return fail("shape index or depth out of range");
                const H::Shape& s = mCollision.mShapes[index];
                switch (s.mKind)
                {
                    case H::ShapeKind::Box:
                    {
                        const JPH::Vec3 half = jv(s.mHalfExtents, u);
                        const float smallest = std::min({ half.GetX(), half.GetY(), half.GetZ() });
                        if (!(smallest > 0.f))
                            return fail("a box with a side of no length");
                        // The convex radius never exceeds the smallest half extent (Jolt asserts it).
                        const float radius = std::min(JPH::cDefaultConvexRadius, smallest);
                        return create(JPH::BoxShapeSettings(half, radius));
                    }
                    case H::ShapeKind::Sphere:
                    {
                        const float radius = static_cast<float>(s.mRadius * u);
                        if (!(radius > 0.f))
                            return fail("a sphere with no radius");
                        return create(JPH::SphereShapeSettings(radius));
                    }
                    case H::ShapeKind::Capsule:
                        return capsule(s, u);
                    case H::ShapeKind::Cylinder:
                        return cylinder(s, u);
                    case H::ShapeKind::ConvexHull:
                    {
                        if (s.mPoints.size() < 4)
                            return fail("a convex hull of fewer than four points");
                        JPH::Array<JPH::Vec3> points;
                        points.reserve(s.mPoints.size());
                        for (const H::Vec3& p : s.mPoints)
                            points.push_back(jv(p, u));
                        // A hull Jolt cannot build (flat or degenerate points) is counted and skipped, not fatal.
                        JPH::ConvexHullShapeSettings settings(points, JPH::cDefaultConvexRadius);
                        const auto result = settings.Create();
                        if (result.HasError())
                        {
                            ++mReport.mHullsRefused;
                            return nullptr;
                        }
                        ++mReport.mJoltShapes;
                        return result.Get();
                    }
                    case H::ShapeKind::TriMesh:
                    {
                        if (s.mTriangles.empty())
                            return nullptr; // nothing to collide with
                        JPH::MeshShapeSettings settings;
                        settings.SetEmbedded();
                        settings.mBuildQuality = JPH::MeshShapeSettings::EBuildQuality::FavorRuntimePerformance;
                        settings.mTriangleVertices.reserve(s.mVertices.size());
                        for (const H::Vec3& v : s.mVertices)
                        {
                            const JPH::Vec3 p = jv(v, u);
                            settings.mTriangleVertices.emplace_back(p.GetX(), p.GetY(), p.GetZ());
                        }
                        settings.mIndexedTriangles.reserve(s.mTriangles.size());
                        for (const auto& t : s.mTriangles)
                            settings.mIndexedTriangles.emplace_back(t[0], t[1], t[2]);
                        auto result = settings.Create();
                        if (result.HasError())
                        {
                            settings.ClearCachedResult();
                            settings.Sanitize();
                            result = settings.Create();
                            if (result.HasError())
                                return fail(result.GetError().c_str());
                        }
                        ++mReport.mJoltShapes;
                        return result.Get();
                    }
                    case H::ShapeKind::List:
                    {
                        std::vector<JPH::RefConst<JPH::Shape>> children;
                        for (std::uint32_t child : s.mChildren)
                        {
                            JPH::RefConst<JPH::Shape> built = build(child, u, depth + 1);
                            if (!mWhy.empty())
                                return nullptr;
                            if (built != nullptr)
                                children.push_back(std::move(built));
                        }
                        return combine(children);
                    }
                    case H::ShapeKind::Transform:
                    {
                        if (s.mChildren.empty())
                            return nullptr;
                        JPH::RefConst<JPH::Shape> child = build(s.mChildren[0], u, depth + 1);
                        if (!mWhy.empty() || child == nullptr)
                            return nullptr;
                        // Row-vector layout: a point is p * M and the translation is the last row (elements 12 to 14),
                        // so the rotation matrix of the usual column convention is M transposed. The survey shows
                        // this layout fits the drawn geometry (654 of 682 points inside the drawn bounds against 550
                        // for the other); the provenance log item U6 has the numbers and says what is not proven.
                        Pose placement;
                        for (int row = 0; row < 3; ++row)
                            for (int col = 0; col < 3; ++col)
                                placement.r[col * 3 + row] = s.mTransform[row * 4 + col];
                        const H::Vec3 t{ s.mTransform[12], s.mTransform[13], s.mTransform[14] };
                        return placed(child, jv(t, u), toJoltQuat(placement));
                    }
                    case H::ShapeKind::Unsupported:
                        return fail("an unsupported shape block: " + s.mTypeName);
                }
                return fail("unknown shape kind");
            }

            JPH::RefConst<JPH::Shape> placed(JPH::RefConst<JPH::Shape> shape, JPH::Vec3 position, JPH::Quat rotation)
            {
                if (position.IsNearZero(1e-6f) && rotation.IsClose(JPH::Quat::sIdentity(), 1e-9f))
                    return shape;
                JPH::RotatedTranslatedShapeSettings settings(position, rotation, shape.GetPtr());
                return create(settings);
            }

            JPH::RefConst<JPH::Shape> combine(const std::vector<JPH::RefConst<JPH::Shape>>& parts)
            {
                if (parts.empty())
                    return nullptr;
                if (parts.size() == 1)
                    return parts[0];
                JPH::StaticCompoundShapeSettings settings;
                for (const auto& part : parts)
                    settings.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), part.GetPtr());
                return create(settings);
            }

        private:
            const H::Collision& mCollision;
            BuildReport& mReport;
            std::string& mWhy;

            JPH::RefConst<JPH::Shape> fail(const std::string& why)
            {
                if (mWhy.empty())
                    mWhy = why;
                return nullptr;
            }

            template <class Settings>
            JPH::RefConst<JPH::Shape> create(const Settings& settings)
            {
                const auto result = settings.Create();
                if (result.HasError())
                    return fail(result.GetError().c_str());
                ++mReport.mJoltShapes;
                return result.Get();
            }

            JPH::RefConst<JPH::Shape> capsule(const H::Shape& s, double u)
            {
                const JPH::Vec3 a = jv(s.mPoint1, u), b = jv(s.mPoint2, u);
                const float radius = static_cast<float>(std::max(s.mRadius1, s.mRadius2) * u);
                if (!(radius > 0.f))
                    return fail("a capsule with no radius");
                const JPH::Vec3 axis = b - a;
                const float length = axis.Length();
                if (length < 1e-4f)
                    return create(JPH::SphereShapeSettings(radius));
                JPH::RefConst<JPH::Shape> shape = create(JPH::CapsuleShapeSettings(0.5f * length, radius));
                if (shape == nullptr)
                    return nullptr;
                return placed(shape, 0.5f * (a + b), JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), axis / length));
            }

            JPH::RefConst<JPH::Shape> cylinder(const H::Shape& s, double u)
            {
                const JPH::Vec3 a = jv(s.mPoint1, u), b = jv(s.mPoint2, u);
                const float radius = static_cast<float>(s.mRadius1 * u);
                const JPH::Vec3 axis = b - a;
                const float length = axis.Length();
                if (!(radius > 0.f) || length < 1e-4f)
                    return fail("a cylinder with no radius or no length");
                const float convex = std::min({ JPH::cDefaultConvexRadius, radius, 0.5f * length });
                JPH::RefConst<JPH::Shape> shape = create(JPH::CylinderShapeSettings(0.5f * length, radius, convex));
                if (shape == nullptr)
                    return nullptr;
                return placed(shape, 0.5f * (a + b), JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), axis / length));
            }
        };
    }

    Outcome makeShape(const NifScene::Scene& scene, vsg::ref_ptr<Resource::PhysicsShape>& shape, BuildReport& report,
        std::string& why)
    {
        const H::Collision& collision = scene.mHavok;
        why.clear();
        if (collision.mBodies.empty())
        {
            why = "no bhk bodies";
            return Outcome::Unusable;
        }
        if (collision.mReport.mOverCap)
        {
            why = "the collision data went over a limit";
            return Outcome::Unusable;
        }

        std::unordered_map<std::uint32_t, std::uint32_t> nodeOfRecord;
        for (std::uint32_t i = 0; i < scene.mNodes.size(); ++i)
            nodeOfRecord.emplace(scene.mNodes[i].mRecordIndex, i);

        Builder builder(collision, report, why);
        JPH::StaticCompoundShapeSettings compound;
        std::vector<JPH::RefConst<JPH::Shape>> parts;
        for (const H::Body& body : collision.mBodies)
        {
            if (!H::bodyBlocksActors(body))
            {
                ++report.mBodiesNotSolid;
                continue;
            }
            if (body.mShape == H::sNone)
            {
                why = "a body whose shape could not be read";
                return Outcome::Unusable;
            }
            const auto node = nodeOfRecord.find(body.mNodeRecord);
            if (node == nodeOfRecord.end())
            {
                why = "a collision object on a block that is not a scene node";
                return Outcome::Unusable;
            }
            const Pose world = nodeWorld(scene, node->second);
            const double u = world.s * H::kHavokToGame;
            JPH::RefConst<JPH::Shape> built = builder.build(body.mShape, u, 0);
            if (!why.empty())
                return Outcome::Unusable;
            if (built == nullptr)
                continue;
            // Body placement: only a bhkRigidBodyT carries one that moves the shape (RECALLED-VERIFY, item U7).
            Pose local;
            if (body.mHasTransform)
            {
                local = fromQuat(body.mRotation);
                local.t = { body.mTranslation[0] * H::kHavokToGame, body.mTranslation[1] * H::kHavokToGame,
                    body.mTranslation[2] * H::kHavokToGame };
            }
            Pose total = compose(world, local);
            total.s = 1.0; // the node's scale is already in the shape's lengths (u) and in total.t via apply
            const JPH::Vec3 position(
                static_cast<float>(total.t[0]), static_cast<float>(total.t[1]), static_cast<float>(total.t[2]));
            parts.push_back(builder.placed(std::move(built), position, toJoltQuat(total)));
            ++report.mBodies;
        }

        if (parts.empty())
        {
            // Bodies exist and none gives geometry: either none is solid (the game gives it no collision) or the
            // shapes were empty.
            if (report.mBodiesNotSolid == collision.mBodies.size())
                return Outcome::NoCollision;
            why = "the solid bodies gave no geometry";
            return Outcome::Unusable;
        }
        JPH::RefConst<JPH::Shape> whole = builder.combine(parts);
        if (!why.empty() || whole == nullptr)
        {
            if (why.empty())
                why = "no shape";
            return Outcome::Unusable;
        }

        shape = new Resource::PhysicsShape;
        shape->mCollisionShape = const_cast<JPH::Shape*>(whole.GetPtr());
        const JPH::AABox bounds = whole->GetLocalBounds();
        const JPH::Vec3 extents = bounds.GetExtent();
        const JPH::Vec3 centre = bounds.GetCenter();
        shape->mCollisionBox.mExtents = vsg::vec3(extents.GetX(), extents.GetY(), extents.GetZ());
        shape->mCollisionBox.mCenter = vsg::vec3(centre.GetX(), centre.GetY(), centre.GetZ());
        return Outcome::Shape;
    }
}
