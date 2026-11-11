#ifndef OPENMW_COMPONENTS_RESOURCE_PHYSICSSHAPE_H
#define OPENMW_COMPONENTS_RESOURCE_PHYSICSSHAPE_H

#include <array>
#include <map>
#include <memory>
#include <set>

#include <vsg/core/Object.h>
#include <vsg/core/ref_ptr.h>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>

#include <components/misc/vsgmath.hpp>
#include <components/vfs/pathutil.hpp>

namespace NifJolt
{
    class JoltNifLoader;
}

namespace Resource
{
    using CollisionShapePtr = JPH::Ref<JPH::Shape>;

    struct CollisionBox
    {
        vsg::vec3 mExtents;
        vsg::vec3 mCenter;
    };

    enum class VisualCollisionType
    {
        None,
        Default,
        Camera
    };

    // openeden-vsg: reference counted by vsg::Object, as the OSG library is no longer linked (Track V).
    struct PhysicsShape : public vsg::Object
    {
        CollisionShapePtr mCollisionShape;
        CollisionShapePtr mAvoidCollisionShape;

        // Used for actors and projectiles. mCollisionShape is used for actors only when we need to autogenerate
        // collision box for creatures. For now, use one file <-> one resource for simplicity.
        CollisionBox mCollisionBox;

        // Stores animated collision shapes.
        // mCollisionShape is a JPH::MutableCompoundShape (which consists of one or more child shapes).
        // In this map, for each animated collision shape,
        // we store the node's record index mapped to the child index of the shape in the JPH::MutableCompoundShape.
        std::map<int, int> mAnimatedShapes;
        // Maps shape record index to controlling NiCollisionSwitch record index.
        std::map<int, int> mCollisionSwitches;
        // Record indices of NiCollisionSwitch nodes whose collision is switched off in the NIF.
        std::set<int> mDisabledCollisionSwitches;

        VFS::Path::Normalized mFileName;
        std::string mFileHash;

        VisualCollisionType mVisualCollisionType = VisualCollisionType::None;

        PhysicsShape() = default;
        // Note this is always a shallow copy and the copy will not autodelete underlying vertex data
        PhysicsShape(const PhysicsShape& other);

        bool isAnimated() const { return !mAnimatedShapes.empty(); }
    };

    // An instance of a PhysicsShape that may have its own unique scaling set on collision shapes.
    // Vertex data is shallow-copied where possible. A ref_ptr to the original shape is held to keep vertex pointers
    // intact.
    class PhysicsShapeInstance : public PhysicsShape
    {
    public:
        explicit PhysicsShapeInstance(vsg::ref_ptr<const PhysicsShape> source);

        const vsg::ref_ptr<const PhysicsShape>& getSource() const { return mSource; }

    private:
        vsg::ref_ptr<const PhysicsShape> mSource;
    };

    vsg::ref_ptr<PhysicsShapeInstance> makeInstance(vsg::ref_ptr<const PhysicsShape> source);

    /// The smallest half extent a box shape takes (game units): a box thinner than the convex radius is refused by
    /// Jolt, and a floor slab of a few hundredths of a unit would let a character through anyway.
    inline constexpr float kMinBoxHalfExtent = 0.25f;

    /// Task 0109 (neutral world, stage N3a-3): a box shape for an object that has bounds and no collision mesh yet.
    /// `halfExtents` and `centre` are in the object's own frame (before its transform); a half extent below
    /// kMinBoxHalfExtent is raised to it. Null when a value is not finite. The convex radius is capped at half of the
    /// smallest half extent (Jolt requires a box to be at least as large as its radius).
    [[nodiscard]] vsg::ref_ptr<PhysicsShape> makeBoxShape(const vsg::vec3& halfExtents, const vsg::vec3& centre);

    struct TriangleMeshShape
    {
        JPH::MeshShapeSettings* m_meshInterface;
        TriangleMeshShape(JPH::MeshShapeSettings* meshInterface, bool useQuantizedAabbCompression, bool buildBvh = true)
        {
            m_meshInterface = meshInterface;
        }

        virtual ~TriangleMeshShape() { delete m_meshInterface; }
    };
}

#endif
