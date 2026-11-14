#include "physicsshape.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <components/debug/debuglog.hpp>

namespace Resource
{
    PhysicsShape::PhysicsShape(const PhysicsShape& other)
        : vsg::Object(other)
        , mCollisionShape(other.mCollisionShape)
        , mAvoidCollisionShape(other.mAvoidCollisionShape)
        , mCollisionBox(other.mCollisionBox)
        , mAnimatedShapes(other.mAnimatedShapes)
        , mCollisionSwitches(other.mCollisionSwitches)
        , mDisabledCollisionSwitches(other.mDisabledCollisionSwitches)
        , mFileName(other.mFileName)
        , mFileHash(other.mFileHash)
        , mVisualCollisionType(other.mVisualCollisionType)
    {
    }

    vsg::ref_ptr<PhysicsShapeInstance> makeInstance(vsg::ref_ptr<const PhysicsShape> source)
    {
        return vsg::ref_ptr<PhysicsShapeInstance>(new PhysicsShapeInstance(std::move(source)));
    }

    vsg::ref_ptr<PhysicsShape> makeBoxShape(const vsg::vec3& halfExtents, const vsg::vec3& centre)
    {
        for (const float v : { halfExtents.x, halfExtents.y, halfExtents.z, centre.x, centre.y, centre.z })
            if (!std::isfinite(v))
                return {};
        const JPH::Vec3 half(std::max(std::abs(halfExtents.x), kMinBoxHalfExtent),
            std::max(std::abs(halfExtents.y), kMinBoxHalfExtent), std::max(std::abs(halfExtents.z), kMinBoxHalfExtent));
        const float radius = std::min(JPH::cDefaultConvexRadius, 0.5f * half.ReduceMin());
        JPH::BoxShapeSettings box(half, radius);
        JPH::ShapeSettings::ShapeResult created = box.Create();
        if (created.HasError())
        {
            Log(Debug::Error) << "PhysicsShape box error: " << created.GetError();
            return {};
        }
        JPH::Ref<JPH::Shape> shape = created.Get();
        if (centre.x != 0.f || centre.y != 0.f || centre.z != 0.f)
        {
            JPH::RotatedTranslatedShapeSettings moved(
                JPH::Vec3(centre.x, centre.y, centre.z), JPH::Quat::sIdentity(), shape);
            JPH::ShapeSettings::ShapeResult movedResult = moved.Create();
            if (movedResult.HasError())
            {
                Log(Debug::Error) << "PhysicsShape box offset error: " << movedResult.GetError();
                return {};
            }
            shape = movedResult.Get();
        }

        vsg::ref_ptr<PhysicsShape> result(new PhysicsShape);
        result->mCollisionShape = shape;
        result->mCollisionBox.mExtents = vsg::vec3(half.GetX(), half.GetY(), half.GetZ());
        result->mCollisionBox.mCenter = centre;
        return result;
    }

    PhysicsShapeInstance::PhysicsShapeInstance(vsg::ref_ptr<const PhysicsShape> source)
        : PhysicsShape(*source)
        , mSource(std::move(source))
    {
    }
}
