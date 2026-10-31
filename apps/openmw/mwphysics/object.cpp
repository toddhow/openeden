#include "object.hpp"
#include "mtphysics.hpp"

#include <vsg/maths/transform.h>
#include <vsg/nodes/Transform.h>

#include <components/animation/transform.hpp>
#include <components/debug/debuglog.hpp>
#include <components/misc/convert.hpp>
#include <components/mwanimation/object.hpp>
#include <components/physicshelpers/collisionobject.hpp>
#include <components/resource/physicsshape.hpp>
#include <components/vsgutil/id.hpp>
#include <components/vsgutil/nodepath.hpp>
#include <components/vsgutil/traverse.hpp>

#include <Jolt/Physics/Collision/Shape/MutableCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

namespace MWPhysics
{
    namespace
    {
        std::mutex sCollidedObjectsMutex;
        std::vector<const MWWorld::LiveCellRefBase*> sCollidedObjects;

        // Finds the path to the transform tagged with a NIF record index by vsgopenmw's NIF reader.
        class GetNodePath : public vsgUtil::SearchPath<const vsg::Transform*>,
                            public vsgUtil::TConstTraverse<vsg::Node>
        {
        public:
            int needle{};
            using vsg::ConstVisitor::apply;
            void apply(const vsg::Transform& node) override
            {
                auto ppn = pushPop(&node);
                auto id = vsgUtil::ID::get(node);
                if (id && id->id == needle)
                    foundPath = this->path;
                else
                    traverseNode(node, *this);
            }
        };
    }

    Object::Object(const MWWorld::Ptr& ptr, vsg::ref_ptr<Resource::PhysicsShapeInstance> shapeInstance,
        vsg::dquat rotation, int collisionType, PhysicsTaskScheduler* scheduler)
        : PtrHolder(ptr, vsg::vec3())
        , mShapeInstance(std::move(shapeInstance))
        , mSolid(true)
        , mScale(ptr.getCellRef().getScale(), ptr.getCellRef().getScale(), ptr.getCellRef().getScale())
        , mPosition(ptr.getRefData().getPosition().asVec3())
        , mRotation(rotation)
        , mTaskScheduler(scheduler)
        , mCollidedWith(ScriptedCollisionType_None)
    {
        // Create a new shape instance from the settings
        // mBasePhysicsShape = mShapeInstance->mCollisionShape.get()->Create().Get();
        mBasePhysicsShape = mShapeInstance->mCollisionShape.GetPtr();
        mUsesScaledShape = !isScaleIdentity();

        JPH::Shape* finalShape = mUsesScaledShape
            ? new JPH::ScaledShape(mBasePhysicsShape.GetPtr(), Misc::Convert::toJolt<JPH::Vec3>(mScale))
            : mBasePhysicsShape.GetPtr();

        JPH::BodyCreationSettings bodyCreationSettings = PhysicsSystemHelpers::makePhysicsBodySettings(
            finalShape, mPosition, rotation, static_cast<JPH::ObjectLayer>(collisionType));
        mPhysicsBody = mTaskScheduler->createPhysicsBody(bodyCreationSettings);
        mPhysicsBody->SetUserData(reinterpret_cast<uintptr_t>(this));
        mTaskScheduler->addCollisionObject(mPhysicsBody, false);
    }

    Object::~Object()
    {
        mTaskScheduler->removeCollisionObject(mPhysicsBody);
        mTaskScheduler->destroyCollisionObject(mPhysicsBody);
    }

    vsg::ref_ptr<const Resource::PhysicsShapeInstance> Object::getShapeInstance() const
    {
        return mShapeInstance;
    }

    void Object::setScale(float scale)
    {
        std::unique_lock<std::mutex> lock(mPositionMutex);
        vsg::vec3 newScale = { scale, scale, scale };
        if (mScale != newScale)
        {
            mScale = { scale, scale, scale };
            mScaleUpdatePending = true;
        }
    }

    void Object::setRotation(vsg::dquat quat)
    {
        std::unique_lock<std::mutex> lock(mPositionMutex);
        mRotation = quat;
        mTransformUpdatePending = true;
    }

    void Object::updatePosition()
    {
        std::unique_lock<std::mutex> lock(mPositionMutex);
        mPosition = mPtr.getRefData().getPosition().asVec3();
        mTransformUpdatePending = true;
    }

    void Object::commitPositionChange()
    {
        std::unique_lock<std::mutex> lock(mPositionMutex);
        if (mScaleUpdatePending)
        {
            JPH::BodyInterface& bodyInterface = mTaskScheduler->getBodyInterface();
            JPH::ScaledShape* newShape;
            JPH::ShapeRefC shapeRef;
            shapeRef = mPhysicsBody->GetShape();
            if (!mUsesScaledShape) // Was not originally a scaled shape
            {
                newShape = new JPH::ScaledShape(shapeRef.GetPtr(), Misc::Convert::toJolt<JPH::Vec3>(mScale));
            }
            else
            {
                const JPH::ScaledShape* bodyShape = reinterpret_cast<const JPH::ScaledShape*>(shapeRef.GetPtr());
                const JPH::Shape* innerShape = bodyShape->GetInnerShape();
                newShape = new JPH::ScaledShape(innerShape, Misc::Convert::toJolt<JPH::Vec3>(mScale));
            }

            mUsesScaledShape = true;

            // NOTE: SetShape will destroy the original shape if required, no need to do it after
            bodyInterface.SetShape(getPhysicsBody(), newShape, false, JPH::EActivation::DontActivate);
            mScaleUpdatePending = false;
        }

        if (mTransformUpdatePending)
        {
            // SetPositionAndRotation is thread safe
            JPH::BodyInterface& bodyInterface = mTaskScheduler->getBodyInterface();
            bodyInterface.SetPositionAndRotation(getPhysicsBody(), Misc::Convert::toJolt<JPH::RVec3>(mPosition),
                Misc::Convert::toJolt(mRotation), JPH::EActivation::Activate);

            mTransformUpdatePending = false;
        }
    }

    vsg::dmat4 Object::getTransform() const
    {
        std::unique_lock<std::mutex> lock(mPositionMutex);
        vsg::dmat4 trans;
        trans = Misc::VsgMath::toMatrix(mRotation);
        Misc::VsgMath::setTrans(trans, mPosition);
        return trans * vsg::scale(vsg::dvec3(mScale));
    }

    bool Object::isSolid() const
    {
        return mSolid;
    }

    void Object::setSolid(bool solid)
    {
        mSolid = solid;
    }

    bool Object::isAnimated() const
    {
        return mShapeInstance->isAnimated();
    }

    bool Object::animateCollisionShapes()
    {
        if (mShapeInstance->mAnimatedShapes.empty())
            return false;

        const MWAnim::Object* animation = MWBase::Environment::get().getWorld()->getAnimation(mPtr);
        if (!animation)
            return false;

        JPH::BodyLockWrite lock(mTaskScheduler->getBodyLockInterface(), getPhysicsBody());
        assert(lock.Succeeded());
        if (!lock.Succeeded())
            return false;

        JPH::MutableCompoundShape* compound = static_cast<JPH::MutableCompoundShape*>(mBasePhysicsShape.GetPtr());

        using NodePath = std::vector<const vsg::Transform*>;
        const auto getNodePathByRecordIndex = [&](int recordIndex) -> NodePath* {
            auto nodePathFound = mRecIndexToNodePath.find(recordIndex);
            if (nodePathFound == mRecIndexToNodePath.end())
            {
                GetNodePath visitor;
                visitor.needle = recordIndex;
                animation->transform()->traverse(visitor);
                if (visitor.foundPath.empty())
                    return nullptr;

                nodePathFound = mRecIndexToNodePath.emplace(recordIndex, std::move(visitor.foundPath)).first;
            }

            return &nodePathFound->second;
        };

        bool result = false;
        for (const auto& [recordIndex, shapeIndex] : mShapeInstance->mAnimatedShapes)
        {
            NodePath* nodePath = getNodePathByRecordIndex(recordIndex);
            if (!nodePath)
            {
                Log(Debug::Warning) << "Warning: animateCollisionShapes can't find node " << recordIndex << " for "
                                    << mPtr.getCellRef().getRefId();

                // Remove nonexistent nodes from animated shapes map and early out
                mShapeInstance->mAnimatedShapes.erase(recordIndex);
                return false;
            }

            assert(static_cast<int>(compound->GetNumSubShapes()) > shapeIndex);

            const JPH::uint shapeIndexUnsigned = static_cast<JPH::uint>(shapeIndex);
            const JPH::CompoundShape::SubShape& subShape = compound->GetSubShape(shapeIndexUnsigned);
            JPH::RefConst<JPH::Shape> childShape = subShape.mShape;
            auto subShapePosition = subShape.GetPositionCOM();
            auto subShapeRotation = subShape.GetRotation();

            if (const auto collisionSwitch = mShapeInstance->mCollisionSwitches.find(recordIndex);
                collisionSwitch != mShapeInstance->mCollisionSwitches.end())
            {
                // The switch state comes from the NIF record, not the render graph, which no longer marks it.
                if (mShapeInstance->mDisabledCollisionSwitches.contains(collisionSwitch->second))
                {
                    static const JPH::Vec3 disabledPosition(1.0e8f, 1.0e8f, 1.0e8f);
                    static const JPH::Quat disabledRotation = JPH::Quat::sIdentity();
                    if (subShapePosition != disabledPosition || subShapeRotation != disabledRotation)
                    {
                        compound->ModifyShape(shapeIndexUnsigned, disabledPosition, disabledRotation);
                        result = true;
                    }
                    continue;
                }
            }

            vsg::mat4 matrix = vsg::mat4(vsg::computeTransform(*nodePath));
            vsg::vec3 scale(Misc::VsgMath::getScale(matrix));
            matrix = Misc::VsgMath::orthoNormalize(matrix);

            auto origin = Misc::Convert::toJolt<JPH::Vec3>(Misc::VsgMath::getTrans(matrix));
            auto rotation = Misc::Convert::toJolt(Misc::VsgMath::rotationOf(matrix));

            const JPH::ScaledShape* scaledPtr = dynamic_cast<const JPH::ScaledShape*>(subShape.mShape.GetPtr());
            const bool isScaledSubshape = scaledPtr != nullptr;
            const JPH::Vec3 currentScale = isScaledSubshape ? scaledPtr->GetScale() : JPH::Vec3(1.0f, 1.0f, 1.0f);
            const JPH::Vec3 newScale = Misc::Convert::toJolt<JPH::Vec3>(scale);

            // In Jolt to change the scale requires creating a new ScaledShape and replacing
            // any position/origin change is also applied at the same time
            if (currentScale != newScale)
            {
                const JPH::Shape* baseSubshape = isScaledSubshape ? scaledPtr->GetInnerShape() : childShape.GetPtr();
                JPH::ScaledShape* newShape = new JPH::ScaledShape(baseSubshape, newScale);
                compound->ModifyShape(shapeIndexUnsigned, origin, rotation, newShape);
                result = true;
            }
            else if (subShapeRotation != rotation || origin != subShapePosition)
            {
                compound->ModifyShape(shapeIndexUnsigned, origin, rotation);
                result = true;
            }
        }
        return result;
    }

    bool Object::collidedWith(ScriptedCollisionType type) const
    {
        return mCollidedWith & type;
    }

    void Object::addCollision(ScriptedCollisionType type)
    {
        bool firstCollisionForThisFrame = false;
        {
            std::unique_lock<std::mutex> lock(mPositionMutex);
            firstCollisionForThisFrame = mCollidedWith == ScriptedCollisionType_None;
            mCollidedWith |= type;
        }

        if (firstCollisionForThisFrame)
        {
            std::unique_lock<std::mutex> lock(sCollidedObjectsMutex);
            sCollidedObjects.push_back(mPtr.mRef);
        }
    }

    void Object::resetCollisions()
    {
        std::unique_lock<std::mutex> lock(mPositionMutex);
        mCollidedWith = ScriptedCollisionType_None;
    }

    std::vector<const MWWorld::LiveCellRefBase*> Object::consumeCollidedObjects()
    {
        std::unique_lock<std::mutex> lock(sCollidedObjectsMutex);
        auto result = std::move(sCollidedObjects);
        sCollidedObjects.clear();
        return result;
    }
}
