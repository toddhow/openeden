#include "host_physics_world.hpp"

#include <algorithm>
#include <variant>

namespace openeden::creativemotion
{
    namespace
    {
        openeden::sdk::PhysicsVec3 toSdk(opas::physics::Vec3 v) noexcept
        {
            return { v.x, v.y, v.z };
        }

        opas::physics::Vec3 toOpas(openeden::sdk::PhysicsVec3 v) noexcept
        {
            return { v.x, v.y, v.z };
        }

        openeden::sdk::PhysicsQuat toSdk(opas::physics::Quat q) noexcept
        {
            return { q.x, q.y, q.z, q.w };
        }

        opas::physics::Quat toOpas(openeden::sdk::PhysicsQuat q) noexcept
        {
            return { q.x, q.y, q.z, q.w };
        }

        openeden::sdk::BodyId toSdk(opas::physics::BodyId id) noexcept
        {
            return { id.index, id.generation };
        }

        opas::physics::BodyId toOpas(openeden::sdk::BodyId id) noexcept
        {
            return { id.index, id.generation };
        }

        openeden::sdk::JointId toSdk(opas::physics::JointId id) noexcept
        {
            return { id.index, id.generation };
        }

        opas::physics::JointId toOpas(openeden::sdk::JointId id) noexcept
        {
            return { id.index, id.generation };
        }

        openeden::sdk::CollisionGroupId toSdk(opas::physics::CollisionGroupId id) noexcept
        {
            return { id.index, id.generation };
        }

        opas::physics::CollisionGroupId toOpas(openeden::sdk::CollisionGroupId id) noexcept
        {
            return { id.index, id.generation };
        }

        openeden::sdk::MotionType toSdk(opas::physics::MotionType m) noexcept
        {
            switch (m)
            {
                case opas::physics::MotionType::Static:
                    return openeden::sdk::MotionType::Static;
                case opas::physics::MotionType::Dynamic:
                    return openeden::sdk::MotionType::Dynamic;
                case opas::physics::MotionType::Kinematic:
                    return openeden::sdk::MotionType::Kinematic;
            }
            return openeden::sdk::MotionType::Static;
        }

        opas::physics::MotionType toOpas(openeden::sdk::MotionType m) noexcept
        {
            switch (m)
            {
                case openeden::sdk::MotionType::Static:
                    return opas::physics::MotionType::Static;
                case openeden::sdk::MotionType::Dynamic:
                    return opas::physics::MotionType::Dynamic;
                case openeden::sdk::MotionType::Kinematic:
                    return opas::physics::MotionType::Kinematic;
            }
            return opas::physics::MotionType::Static;
        }

        opas::physics::ErrorCode toOpas(openeden::sdk::PhysicsErrorCode code) noexcept
        {
            switch (code)
            {
                case openeden::sdk::PhysicsErrorCode::None:
                    return opas::physics::ErrorCode::None;
                case openeden::sdk::PhysicsErrorCode::InvalidDescription:
                    return opas::physics::ErrorCode::InvalidDescription;
                case openeden::sdk::PhysicsErrorCode::InvalidHandle:
                    return opas::physics::ErrorCode::InvalidHandle;
                case openeden::sdk::PhysicsErrorCode::CapacityExceeded:
                    return opas::physics::ErrorCode::CapacityExceeded;
                case openeden::sdk::PhysicsErrorCode::CalledDuringStep:
                    return opas::physics::ErrorCode::CalledDuringStep;
            }
            return opas::physics::ErrorCode::InvalidDescription;
        }

        openeden::sdk::PhysicsShape toSdk(const opas::physics::Shape& shape) noexcept
        {
            if (const auto* box = std::get_if<opas::physics::BoxShape>(&shape))
            {
                return openeden::sdk::BoxShape{ toSdk(box->halfExtentsM) };
            }
            if (const auto* sphere = std::get_if<opas::physics::SphereShape>(&shape))
            {
                return openeden::sdk::SphereShape{ sphere->radiusM };
            }
            const auto& capsule = std::get<opas::physics::CapsuleShape>(shape);
            return openeden::sdk::CapsuleShape{ capsule.halfHeightM, capsule.radiusM };
        }

        openeden::sdk::SwingShape toSdk(opas::physics::SwingShape s) noexcept
        {
            return s == opas::physics::SwingShape::Pyramid ? openeden::sdk::SwingShape::Pyramid
                                                           : openeden::sdk::SwingShape::Cone;
        }
    } // namespace

    HostPhysicsWorld::HostPhysicsWorld(openeden::sdk::PhysicsService& service)
        : mService(service)
    {
        mListeners.reserve(16);
    }

    opas::physics::CreateResult<opas::physics::BodyId> HostPhysicsWorld::createBody(
        const opas::physics::BodyDesc& desc)
    {
        openeden::sdk::BodyDesc sdkDesc{};
        sdkDesc.shape = toSdk(desc.shape);
        sdkDesc.motion = toSdk(desc.motion);
        sdkDesc.positionM = toSdk(desc.positionM);
        sdkDesc.rotation = toSdk(desc.rotation);
        sdkDesc.massKg = desc.massKg;
        sdkDesc.friction = desc.friction;
        sdkDesc.restitution = desc.restitution;
        sdkDesc.linearDamping = desc.linearDamping;
        sdkDesc.angularDamping = desc.angularDamping;
        sdkDesc.collisionGroup = toSdk(desc.collisionGroup);
        sdkDesc.subGroup = desc.subGroup;

        auto res = mService.createBody(sdkDesc);
        return { toOpas(res.id), toOpas(res.error), std::move(res.message) };
    }

    bool HostPhysicsWorld::destroyBody(opas::physics::BodyId body)
    {
        return mService.destroyBody(toSdk(body));
    }

    opas::physics::CreateResult<opas::physics::JointId> HostPhysicsWorld::createJoint(
        const opas::physics::JointDesc& desc)
    {
        openeden::sdk::JointDesc sdkDesc{};
        sdkDesc.bodyA = toSdk(desc.bodyA);
        sdkDesc.bodyB = toSdk(desc.bodyB);
        sdkDesc.anchorM = toSdk(desc.anchorM);
        sdkDesc.twistAxis = toSdk(desc.twistAxis);
        sdkDesc.planeAxis = toSdk(desc.planeAxis);
        sdkDesc.swingShape = toSdk(desc.swingShape);
        sdkDesc.planeSwingMinRad = desc.planeSwingMinRad;
        sdkDesc.planeSwingMaxRad = desc.planeSwingMaxRad;
        sdkDesc.normalSwingMinRad = desc.normalSwingMinRad;
        sdkDesc.normalSwingMaxRad = desc.normalSwingMaxRad;
        sdkDesc.twistMinRad = desc.twistMinRad;
        sdkDesc.twistMaxRad = desc.twistMaxRad;

        auto res = mService.createJoint(sdkDesc);
        return { toOpas(res.id), toOpas(res.error), std::move(res.message) };
    }

    bool HostPhysicsWorld::destroyJoint(opas::physics::JointId joint)
    {
        return mService.destroyJoint(toSdk(joint));
    }

    opas::physics::CreateResult<opas::physics::CollisionGroupId>
    HostPhysicsWorld::createCollisionGroup(std::uint32_t subGroupCount)
    {
        auto res = mService.createCollisionGroup(subGroupCount);
        return { toOpas(res.id), toOpas(res.error), std::move(res.message) };
    }

    bool HostPhysicsWorld::setSubGroupsCollide(
        opas::physics::CollisionGroupId group, std::uint32_t a, std::uint32_t b, bool collide)
    {
        return mService.setSubGroupsCollide(toSdk(group), a, b, collide);
    }

    bool HostPhysicsWorld::destroyCollisionGroup(opas::physics::CollisionGroupId group)
    {
        return mService.destroyCollisionGroup(toSdk(group));
    }

    bool HostPhysicsWorld::setJointMotor(
        opas::physics::JointId joint, const opas::physics::JointMotor& motor)
    {
        const openeden::sdk::JointMotor sdkMotor{
            motor.stiffnessNmPerRad,
            motor.dampingNmsPerRad,
            motor.maxTorqueNm,
        };
        return mService.setJointMotor(toSdk(joint), sdkMotor);
    }

    bool HostPhysicsWorld::setJointPositionSteps(opas::physics::JointId joint, std::uint32_t steps)
    {
        return mService.setJointPositionSteps(toSdk(joint), steps);
    }

    bool HostPhysicsWorld::setJointTarget(
        opas::physics::JointId joint, opas::physics::Quat targetRotation)
    {
        return mService.setJointTarget(toSdk(joint), toSdk(targetRotation));
    }

    bool HostPhysicsWorld::setJointTarget(opas::physics::JointId joint,
        opas::physics::Quat targetRotation, opas::physics::Vec3 targetAngularVelocityRadps)
    {
        return mService.setJointTarget(
            toSdk(joint), toSdk(targetRotation), toSdk(targetAngularVelocityRadps));
    }

    bool HostPhysicsWorld::applyImpulse(
        opas::physics::BodyId body, opas::physics::Vec3 impulseNs, opas::physics::Vec3 worldPointM)
    {
        return mService.applyImpulse(toSdk(body), toSdk(impulseNs), toSdk(worldPointM));
    }

    bool HostPhysicsWorld::setVelocity(
        opas::physics::BodyId body, opas::physics::Vec3 linearMps, opas::physics::Vec3 angularRadps)
    {
        return mService.setVelocity(toSdk(body), toSdk(linearMps), toSdk(angularRadps));
    }

    bool HostPhysicsWorld::setBodyDamping(
        opas::physics::BodyId body, float linearPerS, float angularPerS)
    {
        return mService.setBodyDamping(toSdk(body), linearPerS, angularPerS);
    }

    bool HostPhysicsWorld::setKinematicTarget(
        opas::physics::BodyId body, opas::physics::Vec3 positionM, opas::physics::Quat rotation)
    {
        return mService.setKinematicTarget(toSdk(body), toSdk(positionM), toSdk(rotation));
    }

    bool HostPhysicsWorld::setMotionType(
        opas::physics::BodyId body, opas::physics::MotionType motion)
    {
        return mService.setMotionType(toSdk(body), toSdk(motion));
    }

    std::optional<opas::physics::MotionType> HostPhysicsWorld::getMotionType(
        opas::physics::BodyId body) const
    {
        const auto m = mService.getMotionType(toSdk(body));
        if (!m)
        {
            return std::nullopt;
        }
        return toOpas(*m);
    }

    bool HostPhysicsWorld::readBody(opas::physics::BodyId body, opas::physics::BodyState& out) const
    {
        openeden::sdk::BodyState sdkState{};
        if (!mService.readBody(toSdk(body), sdkState))
        {
            return false;
        }
        out.positionM = toOpas(sdkState.positionM);
        out.rotation = toOpas(sdkState.rotation);
        out.linearVelocityMps = toOpas(sdkState.linearVelocityMps);
        out.angularVelocityRadps = toOpas(sdkState.angularVelocityRadps);
        out.awake = sdkState.awake;
        return true;
    }

    std::size_t HostPhysicsWorld::readBodies(
        std::span<const opas::physics::BodyId> bodies, std::span<opas::physics::BodyState> out) const
    {
        const std::size_t count = std::min(bodies.size(), out.size());
        std::size_t read = 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (readBody(bodies[i], out[i]))
            {
                ++read;
            }
        }
        return read;
    }

    std::optional<opas::physics::RayHit> HostPhysicsWorld::castRay(
        opas::physics::Vec3 originM, opas::physics::Vec3 directionTimesLengthM,
        opas::physics::BodyId ignore) const
    {
        const auto hit
            = mService.castRay(toSdk(originM), toSdk(directionTimesLengthM), toSdk(ignore));
        if (!hit)
        {
            return std::nullopt;
        }
        opas::physics::RayHit result{};
        result.body = toOpas(hit->body);
        result.pointM = toOpas(hit->pointM);
        result.normal = toOpas(hit->normal);
        result.fraction = hit->fraction;
        return result;
    }

    void HostPhysicsWorld::step()
    {
        if (mInStep)
        {
            return;
        }
        const float dt = fixedStepSeconds();
        runPreStepListeners(dt);
        mService.step(dt);
        runPostStepListeners(dt);
    }

    float HostPhysicsWorld::fixedStepSeconds() const
    {
        return mService.fixedStepSeconds();
    }

    opas::physics::StepListenerId HostPhysicsWorld::addStepListener(
        opas::physics::StepListener& listener)
    {
        const opas::physics::StepListenerId id{ mNextListenerSerial++, 1 };
        mListeners.push_back(ListenerEntry{ &listener, id });
        return id;
    }

    bool HostPhysicsWorld::removeStepListener(opas::physics::StepListenerId id)
    {
        const auto it = std::find_if(mListeners.begin(), mListeners.end(),
            [id](const ListenerEntry& e) { return e.listener != nullptr && e.id == id; });
        if (it == mListeners.end())
        {
            return false;
        }
        if (mInStep)
        {
            it->listener = nullptr;
        }
        else
        {
            mListeners.erase(it);
        }
        return true;
    }

    void HostPhysicsWorld::runPreStepListeners(float dtSeconds)
    {
        mInStep = true;
        const std::size_t listenerCount = mListeners.size();
        for (std::size_t i = 0; i < listenerCount; ++i)
        {
            if (mListeners[i].listener != nullptr)
            {
                mListeners[i].listener->onPreStep(*this, dtSeconds);
            }
        }
    }

    void HostPhysicsWorld::runPostStepListeners(float dtSeconds)
    {
        const std::size_t listenerCount = mListeners.size();
        for (std::size_t i = 0; i < listenerCount; ++i)
        {
            if (mListeners[i].listener != nullptr)
            {
                mListeners[i].listener->onPostStep(*this, dtSeconds);
            }
        }
        std::erase_if(mListeners, [](const ListenerEntry& entry) { return entry.listener == nullptr; });
        mInStep = false;
    }
} // namespace openeden::creativemotion
