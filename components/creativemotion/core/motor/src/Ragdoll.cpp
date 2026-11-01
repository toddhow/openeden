#include "opas/motor/Ragdoll.hpp"

#include "Conversions.hpp"

#include <cmath>
#include <utility>
#include <variant>

namespace opas::motor
{
    namespace
    {

        // Rotation whose columns are the orthonormal axes x, y, z (Shepperd's
        // method: pick the largest diagonal term for numerical stability).
        math::Quat fromBasis(math::Vec3 x, math::Vec3 y, math::Vec3 z)
        {
            const double m00 = x.x, m10 = x.y, m20 = x.z;
            const double m01 = y.x, m11 = y.y, m21 = y.z;
            const double m02 = z.x, m12 = z.y, m22 = z.z;
            const double trace = m00 + m11 + m22;
            math::Quat q;
            if (trace > 0.0)
            {
                const double s = std::sqrt(trace + 1.0) * 2.0;
                q = { (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s };
            }
            else if (m00 > m11 && m00 > m22)
            {
                const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
                q = { 0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s };
            }
            else if (m11 > m22)
            {
                const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
                q = { (m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s };
            }
            else
            {
                const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
                q = { (m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s };
            }
            return math::normalized(q);
        }

        physics::Shape toPhysicsShape(const rig::Shape& shape)
        {
            if (const auto* capsule = std::get_if<rig::CapsuleShape>(&shape))
            {
                // Both put the capsule axis along the body's local Z.
                return physics::CapsuleShape{ static_cast<float>(capsule->halfHeightM),
                    static_cast<float>(capsule->radiusM) };
            }
            if (const auto* box = std::get_if<rig::BoxShape>(&shape))
            {
                return physics::BoxShape{ toPhysics(box->halfExtentsM) };
            }
            return physics::SphereShape{ static_cast<float>(std::get<rig::SphereShape>(shape).radiusM) };
        }

        BuildResult failure(physics::ErrorCode error, std::string message)
        {
            BuildResult result;
            result.error = error;
            result.message = std::move(message);
            return result;
        }

        // Destroys what a failed build created so far.
        void undo(physics::PhysicsWorld& world, const Ragdoll& partial)
        {
            for (const RagdollBody& body : partial.bodies)
            {
                if (body.id.isValid())
                {
                    world.destroyBody(body.id); // also destroys its joints
                }
            }
            if (partial.collisionGroup.isValid())
            {
                world.destroyCollisionGroup(partial.collisionGroup);
            }
        }

    } // namespace

    BuildResult buildRagdoll(physics::PhysicsWorld& world, const rig::Rig& rig, const rig::Profile& profile,
        const math::Transform& rootWorld, const RagdollOptions& options)
    {
        if (const std::string problem = rig::validateRig(rig); !problem.empty())
        {
            return failure(physics::ErrorCode::InvalidDescription, "rig: " + problem);
        }
        if (const std::string problem = rig::validateProfile(profile, rig); !problem.empty())
        {
            return failure(physics::ErrorCode::InvalidDescription, "profile: " + problem);
        }

        Ragdoll ragdoll;
        ragdoll.skeleton = rig.skeleton;
        ragdoll.poseModel.resize(rig.skeleton.bones.size());
        ragdoll.rootWorld = rootWorld;
        ragdoll.kinematicRoot = options.rootMotion == RootMotion::Kinematic;
        const std::vector<math::Transform> boneRest = rig::computeWorldRest(rig.skeleton);

        const auto group = world.createCollisionGroup(static_cast<std::uint32_t>(rig.bodies.size()));
        if (!group.ok())
        {
            return failure(group.error, "collision group: " + group.message);
        }
        ragdoll.collisionGroup = group.id;

        ragdoll.bodies.reserve(rig.bodies.size());
        for (std::size_t i = 0; i < rig.bodies.size(); ++i)
        {
            const rig::Body& body = rig.bodies[i];
            const std::size_t bone = rig::findBone(rig.skeleton, body.bone).value_or(0);
            const bool isRoot = !rig::parentBody(rig, i).has_value();
            if (isRoot)
            {
                ragdoll.rootBody = i;
            }
            const math::Transform model = boneRest[bone] * body.local;
            const math::Transform placed = rootWorld * model;
            const rig::Material material = rig::materialFor(profile, body.bone);

            physics::BodyDesc desc;
            desc.shape = toPhysicsShape(body.shape);
            desc.motion
                = isRoot && ragdoll.kinematicRoot ? physics::MotionType::Kinematic : physics::MotionType::Dynamic;
            desc.positionM = toPhysics(placed.translation);
            desc.rotation = toPhysics(placed.rotation);
            desc.massKg = static_cast<float>(body.massKg);
            desc.friction = static_cast<float>(material.friction);
            desc.restitution = static_cast<float>(material.restitution);
            desc.linearDamping = static_cast<float>(material.linearDamping);
            desc.angularDamping = static_cast<float>(material.angularDamping);
            desc.collisionGroup = group.id;
            desc.subGroup = static_cast<std::uint32_t>(i);

            const auto created = world.createBody(desc);
            if (!created.ok())
            {
                undo(world, ragdoll);
                return failure(created.error, "body on \"" + body.bone + "\": " + created.message);
            }
            ragdoll.bodies.push_back(RagdollBody{ created.id, bone, body.local, model.rotation });
        }

        ragdoll.joints.reserve(rig.joints.size());
        for (const rig::Joint& joint : rig.joints)
        {
            const std::size_t child = rig::findBody(rig, joint.bone).value_or(0);
            const std::size_t parent = rig::parentBody(rig, child).value_or(0);
            const math::Transform& bone = boneRest[ragdoll.bodies[child].bone];
            const math::Vec3 twistModel = math::rotate(bone.rotation, joint.twistAxis);
            const math::Vec3 planeModel = math::rotate(bone.rotation, joint.planeAxis);
            const math::Quat frameModel = fromBasis(twistModel, math::cross(planeModel, twistModel), planeModel);

            physics::JointDesc desc;
            desc.bodyA = ragdoll.bodies[parent].id;
            desc.bodyB = ragdoll.bodies[child].id;
            desc.anchorM = toPhysics(math::transformPoint(rootWorld, bone.translation));
            desc.twistAxis = toPhysics(math::rotate(rootWorld.rotation, twistModel));
            desc.planeAxis = toPhysics(math::rotate(rootWorld.rotation, planeModel));
            desc.swingShape
                = joint.swing == rig::SwingShape::Pyramid ? physics::SwingShape::Pyramid : physics::SwingShape::Cone;
            desc.planeSwingMinRad = static_cast<float>(joint.planeSwingMinRad);
            desc.planeSwingMaxRad = static_cast<float>(joint.planeSwingMaxRad);
            desc.normalSwingMinRad = static_cast<float>(joint.normalSwingMinRad);
            desc.normalSwingMaxRad = static_cast<float>(joint.normalSwingMaxRad);
            desc.twistMinRad = static_cast<float>(joint.twistMinRad);
            desc.twistMaxRad = static_cast<float>(joint.twistMaxRad);

            const auto created = world.createJoint(desc);
            if (!created.ok())
            {
                undo(world, ragdoll);
                return failure(created.error, "joint on \"" + joint.bone + "\": " + created.message);
            }
            ragdoll.joints.push_back(RagdollJoint{ created.id, parent, child, frameModel });

            const rig::MotorGains gains = rig::motorFor(profile, joint.bone);
            const physics::JointMotor motor{ static_cast<float>(gains.stiffnessNmPerRad),
                static_cast<float>(gains.dampingNmsPerRad), static_cast<float>(gains.maxTorqueNm) };
            if (!world.setJointMotor(created.id, motor))
            {
                undo(world, ragdoll);
                return failure(
                    physics::ErrorCode::InvalidDescription, "joint on \"" + joint.bone + "\": motor gains rejected");
            }
            // Jointed neighbours overlap at their shared joint; let them pass.
            if (!world.setSubGroupsCollide(
                    group.id, static_cast<std::uint32_t>(parent), static_cast<std::uint32_t>(child), false))
            {
                undo(world, ragdoll);
                return failure(
                    physics::ErrorCode::InvalidHandle, "joint on \"" + joint.bone + "\": collision filter rejected");
            }
        }

        ragdoll.lastJointTargets.assign(ragdoll.joints.size(), math::Quat{ 0.0, 0.0, 0.0, 1.0 });
        ragdoll.hasLastJointTargets = false;

        BuildResult result;
        result.ragdoll = std::move(ragdoll);
        return result;
    }

    bool destroyRagdoll(physics::PhysicsWorld& world, Ragdoll& ragdoll)
    {
        bool allDestroyed = true;
        bool anyDestroyed = false;
        for (const RagdollBody& body : ragdoll.bodies)
        {
            const bool destroyed = world.destroyBody(body.id);
            allDestroyed = allDestroyed && destroyed;
            anyDestroyed = anyDestroyed || destroyed;
        }
        const bool groupDestroyed = world.destroyCollisionGroup(ragdoll.collisionGroup);
        allDestroyed = allDestroyed && groupDestroyed;
        anyDestroyed = anyDestroyed || groupDestroyed;
        // Nothing could be destroyed (e.g. called from a step listener): keep the
        // handles so the host can try again instead of leaking the bodies.
        if (!anyDestroyed && !ragdoll.bodies.empty())
        {
            return false;
        }
        ragdoll = Ragdoll{};
        return allDestroyed;
    }

    bool makeLimp(physics::PhysicsWorld& world, const Ragdoll& ragdoll, const rig::Profile& profile)
    {
        bool ok = true;
        const physics::JointMotor off{ 0.0f, 0.0f }; // both zero: motor off (JointMotor)
        for (const RagdollJoint& joint : ragdoll.joints)
        {
            ok = world.setJointMotor(joint.id, off) && ok;
            if (profile.death)
            {
                ok = world.setJointPositionSteps(
                         joint.id, static_cast<std::uint32_t>(profile.death->jointPositionSteps))
                    && ok;
            }
        }
        if (profile.death)
        {
            const auto linear = static_cast<float>(profile.death->linearDampingPerS);
            const auto angular = static_cast<float>(profile.death->angularDampingPerS);
            for (std::size_t b = 0; b < ragdoll.bodies.size(); ++b)
            {
                // A kinematic root stays where the host puts it; it has no damping.
                if (ragdoll.kinematicRoot && b == ragdoll.rootBody)
                {
                    continue;
                }
                ok = world.setBodyDamping(ragdoll.bodies[b].id, linear, angular) && ok;
            }
        }
        return ok;
    }

    math::Quat jointRotation(
        const Ragdoll& ragdoll, std::size_t jointIndex, math::Quat parentBody, math::Quat childBody)
    {
        const RagdollJoint& joint = ragdoll.joints[jointIndex];
        const math::Quat parentRest = ragdoll.bodies[joint.parentBody].restModel;
        const math::Quat childRest = ragdoll.bodies[joint.childBody].restModel;
        // J^-1 * A_rest * (A^-1 * B) * B_rest^-1 * J: the child's joint frame
        // relative to the parent's, identity at rest (docs/tasks/0013).
        math::Quat q = math::conjugate(joint.frameModel) * parentRest * math::conjugate(parentBody) * childBody
            * math::conjugate(childRest) * joint.frameModel;
        q = math::normalized(q);
        if (q.w < 0.0)
        { // q and -q are the same rotation; keep one form
            q = math::Quat{ -q.x, -q.y, -q.z, -q.w };
        }
        return q;
    }

    void applyRootMotion(Ragdoll& ragdoll, const math::Transform& deltaRoot)
    {
        ragdoll.rootWorld = ragdoll.rootWorld * deltaRoot;
    }

    void applyRootMotion(Ragdoll& ragdoll, const math::Vec3& deltaTranslationM, double deltaYawRad)
    {
        const math::Transform deltaRoot{ deltaTranslationM,
            math::fromAxisAngle(math::Vec3{ 0.0, 0.0, 1.0 }, deltaYawRad) };
        applyRootMotion(ragdoll, deltaRoot);
    }

} // namespace opas::motor
