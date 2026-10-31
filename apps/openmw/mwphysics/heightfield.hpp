#ifndef OPENMW_MWPHYSICS_HEIGHTFIELD_H
#define OPENMW_MWPHYSICS_HEIGHTFIELD_H

#include <components/misc/vsgmath.hpp>

#include <vsg/core/Object.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec3.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyManager.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <memory>
#include <vector>

namespace osg
{
    class Object;
}

namespace MWPhysics
{
    class PhysicsTaskScheduler;

    class HeightField
    {
    public:
        HeightField(const float* heights, int x, int y, int size, int verts, float minH, float maxH,
            const vsg::Object* holdObject, PhysicsTaskScheduler* scheduler);
        ~HeightField();

        const vsg::vec3& getOrigin() const { return mWorldOrigin; }

    private:
        JPH::Body* mPhysicsBody; // NOTE: memory is managed by Jolt!
        JPH::ShapeRefC mShapeReference;
        vsg::vec3 mWorldOrigin;
        vsg::ref_ptr<const vsg::Object> mHoldObject;

        PhysicsTaskScheduler* mTaskScheduler;

        void operator=(const HeightField&);
        HeightField(const HeightField&);
    };
}

#endif
