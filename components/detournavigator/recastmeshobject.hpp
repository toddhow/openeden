#ifndef OPENMW_COMPONENTS_DETOURNAVIGATOR_RECASTMESHOBJECT_H
#define OPENMW_COMPONENTS_DETOURNAVIGATOR_RECASTMESHOBJECT_H

#include "areatype.hpp"
#include "objecttransform.hpp"

#include <components/misc/vsgmath.hpp>
#include <components/resource/physicsshape.hpp>

#include <Jolt/Physics/Collision/Shape/CompoundShape.h>

#include <osg/ref_ptr>
#include <vsg/core/ref_ptr.h>

#include <functional>
#include <vector>

namespace DetourNavigator
{
    static inline vsg::dmat4 getSubShapeTransform(JPH::CompoundShape::SubShape subShape)
    {
        auto subRot = subShape.GetRotation();
        auto comPos = subShape.GetPositionCOM();
        auto subPos = comPos - subRot * subShape.mShape->GetCenterOfMass();
        vsg::dmat4 trans
            = Misc::VsgMath::toMatrix(vsg::dquat(subRot.GetX(), subRot.GetY(), subRot.GetZ(), subRot.GetW()));
        Misc::VsgMath::setTrans(trans, vsg::vec3(subPos.GetX(), subPos.GetY(), subPos.GetZ()));
        return trans;
    }

    class CollisionShape
    {
    public:
        CollisionShape(vsg::ref_ptr<const Resource::PhysicsShapeInstance> instance, const JPH::Shape& shape,
            const ObjectTransform& transform)
            : mInstance(std::move(instance))
            , mShape(shape)
            , mObjectTransform(transform)
        {
        }

        const vsg::ref_ptr<const Resource::PhysicsShapeInstance>& getInstance() const { return mInstance; }
        const JPH::Shape& getShape() const { return mShape; }
        const ObjectTransform& getObjectTransform() const { return mObjectTransform; }

    private:
        vsg::ref_ptr<const Resource::PhysicsShapeInstance> mInstance;
        const JPH::Shape& mShape;
        ObjectTransform mObjectTransform;
    };

    class ChildRecastMeshObject
    {
    public:
        ChildRecastMeshObject(
            const JPH::CompoundShape::SubShape& shape, const vsg::dmat4& transform, const AreaType areaType);

        bool update(const vsg::dmat4& transform, const AreaType areaType);

        const JPH::RefConst<JPH::Shape> getShape() const { return mShape.get().mShape; }

        const vsg::dmat4& getTransform() const { return mTransform; }

        AreaType getAreaType() const { return mAreaType; }

    private:
        std::reference_wrapper<const JPH::CompoundShape::SubShape> mShape;
        vsg::dmat4 mTransform;
        AreaType mAreaType;
        JPH::Vec3 mLocalScaling;
        std::vector<ChildRecastMeshObject> mChildren;
    };

    class RecastMeshObject
    {
    public:
        RecastMeshObject(const CollisionShape& shape, const vsg::dmat4& transform, const AreaType areaType);

        bool update(const vsg::dmat4& transform, const AreaType areaType);

        const vsg::ref_ptr<const Resource::PhysicsShapeInstance>& getInstance() const { return mInstance; }

        const JPH::Shape& getShape() const { return mShape; }

        const vsg::dmat4& getTransform() const { return mTransform; }

        AreaType getAreaType() const { return mAreaType; }

        const ObjectTransform& getObjectTransform() const { return mObjectTransform; }

    private:
        vsg::ref_ptr<const Resource::PhysicsShapeInstance> mInstance;
        ObjectTransform mObjectTransform;

        const JPH::Shape& mShape;
        vsg::dmat4 mTransform;
        AreaType mAreaType;
        JPH::Vec3 mLocalScaling;
        std::vector<ChildRecastMeshObject> mChildren;
    };
}

#endif
