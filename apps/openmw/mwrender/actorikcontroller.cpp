#include "actorikcontroller.hpp"
#include "animation.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwphysics/actor.hpp"
#include "../mwphysics/joltlayers.hpp"
#include "../mwphysics/physicssystem.hpp"
#include "../mwphysics/raycasting.hpp"
#include "../mwworld/class.hpp"

#include <components/sceneutil/nodecallback.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/sceneutil/skeleton.hpp>
#include <components/settings/values.hpp>

#include <opas/ik/TwoBoneIk.hpp>
#include <opas/math/Vec3.hpp>

#include <osg/FrameStamp>
#include <osg/MatrixTransform>
#include <osg/NodeVisitor>

#include <algorithm>
#include <array>
#include <cmath>

namespace MWRender
{
    namespace
    {
        // Tuning (world units = Morrowind units, ~1/70 m)
        constexpr float sProbeAbove = 55.f; // ray start above actor origin
        constexpr float sProbeBelow = 90.f; // ray end below actor origin
        constexpr float sToeDistance = 10.f; // fallback toe probe distance in front of the ankle
        constexpr float sMinSoleHeight = 6.8f; // vertical distance from boot/foot sole to ankle joint
        constexpr float sStanceRadius = 11.f; // horizontal half-stance radius for yaw-invariant slope drop
        constexpr float sMaxFootOffset = 32.f; // max downward foot reach below actor origin
        constexpr float sMaxFootRaise = 36.f; // max upward foot lift above actor origin
        constexpr float sMaxPelvisDrop = 26.f;
        constexpr float sMaxFootTilt = 0.6f; // radians (~34 degrees)
        constexpr float sSnapDistance = 60.f; // teleport / large change: snap instead of filtering

        // Exponential filter rates (1/s). Ground rising is followed faster than ground falling so feet do
        // not sink into steps, and the pelvis is filtered in world space to absorb collision height pops.
        constexpr float sGroundRiseRate = 24.f;
        constexpr float sGroundFallRate = 14.f;
        constexpr float sNormalRate = 12.f;
        constexpr float sPelvisRate = 14.f;
        constexpr float sWeightRate = 8.f;

        float filterAlpha(float rate, float dt)
        {
            return std::clamp(1.f - std::exp(-rate * dt), 0.f, 1.f);
        }

        osg::Matrix worldMatrix(osg::Node* node)
        {
            const osg::NodePathList paths = node->getParentalNodePaths();
            if (paths.empty())
                return osg::Matrix::identity();
            return osg::computeLocalToWorld(paths[0]);
        }

        osg::Vec3f worldPos(osg::Node* node)
        {
            return osg::Vec3f(worldMatrix(node).getTrans());
        }

        osg::Matrix rotateAbout(const osg::Vec3f& pivot, const osg::Matrix& rotation)
        {
            return osg::Matrix::translate(-pivot) * rotation * osg::Matrix::translate(pivot);
        }

        osg::Vec3f toOsg(const opas::math::Vec3& v)
        {
            return osg::Vec3f(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
        }

        opas::math::Vec3 toOpas(const osg::Vec3f& v)
        {
            return { v.x(), v.y(), v.z() };
        }

        osg::Vec3f clampTilt(osg::Vec3f n)
        {
            const osg::Vec3f up(0.f, 0.f, 1.f);
            if (n.length2() < 1e-6f)
                return up;
            n.normalize();
            const float angle = std::acos(std::clamp(n.z(), -1.f, 1.f));
            if (angle <= sMaxFootTilt)
                return n;
            osg::Vec3f axis = up ^ n;
            if (axis.length2() < 1e-6f)
                return up;
            axis.normalize();
            return osg::Quat(sMaxFootTilt, axis) * up;
        }

        MWPhysics::Actor* getPhysicsActor(const MWWorld::Ptr& ptr)
        {
            MWBase::World* world = MWBase::Environment::get().getWorld();
            if (!world)
                return nullptr;
            const auto* rayCasting = world->getRayCasting();
            if (!rayCasting)
                return nullptr;
            auto* physics = const_cast<MWPhysics::PhysicsSystem*>(
                static_cast<const MWPhysics::PhysicsSystem*>(rayCasting));
            return physics->getActor(ptr);
        }
    }

    class ActorIkController::PostAnimationCallback : public SceneUtil::NodeCallback<PostAnimationCallback>
    {
    public:
        explicit PostAnimationCallback(ActorIkController* owner)
            : mOwner(owner)
        {
        }

        void operator()(osg::Node* node, osg::NodeVisitor* nv)
        {
            // Let every keyframe / blend controller of the skeleton run first.
            traverse(node, nv);

            if (mOwner && nv->getFrameStamp())
                mOwner->applyPostAnimation(nv->getFrameStamp()->getSimulationTime());
        }

        ActorIkController* mOwner;
    };

    ActorIkController::ActorIkController(Animation& animation)
        : mAnimation(animation)
    {
    }

    ActorIkController::~ActorIkController()
    {
        if (mPostCallback)
            mPostCallback->mOwner = nullptr;
        detachPostCallback();
    }

    void ActorIkController::attachPostCallback()
    {
        osg::Node* root = mAnimation.mObjectRoot.get();
        if (!root)
            return;

        osg::ref_ptr<osg::Node> attached;
        if (mAttachedTo.lock(attached) && attached.get() == root)
            return;

        detachPostCallback();

        if (!mPostCallback)
            mPostCallback = new PostAnimationCallback(this);

        root->addUpdateCallback(mPostCallback);
        mAttachedTo = root;
    }

    void ActorIkController::detachPostCallback()
    {
        osg::ref_ptr<osg::Node> attached;
        if (mPostCallback && mAttachedTo.lock(attached))
            attached->removeUpdateCallback(mPostCallback);
        mAttachedTo = nullptr;
    }

    void ActorIkController::initControllers()
    {
        if (!mAnimation.mPtr.getClass().isActor() || mAnimation.getNodeMap().empty())
            return;

        mInitialized = true;

        const auto& nodeMap = mAnimation.getNodeMap();
        auto findNode = [&](std::string_view name) -> osg::MatrixTransform* {
            auto it = nodeMap.find(name);
            return (it != nodeMap.end()) ? it->second.get() : nullptr;
        };

        auto bind = [&](BoneState& bone, std::string_view name) {
            osg::MatrixTransform* node = findNode(name);
            if (bone.mNode.get() != node)
            {
                bone = BoneState{};
                bone.mNode = node;
            }
        };

        bind(mPelvis, "bip01 pelvis");
        bind(mLegs[0].mThigh, "bip01 l thigh");
        bind(mLegs[0].mCalf, "bip01 l calf");
        bind(mLegs[0].mFoot, "bip01 l foot");
        mLegs[0].mToe = findNode("bip01 l toe0");

        bind(mLegs[1].mThigh, "bip01 r thigh");
        bind(mLegs[1].mCalf, "bip01 r calf");
        bind(mLegs[1].mFoot, "bip01 r foot");
        mLegs[1].mToe = findNode("bip01 r toe0");

        bind(mSpine1, "bip01 spine1");
        mNeck = findNode("bip01 neck");
        mHead = findNode("bip01 head");

        mArms[0].mUpperArm = findNode("bip01 l upperarm");
        mArms[0].mForearm = findNode("bip01 l forearm");
        mArms[0].mHand = findNode("bip01 l hand");

        mArms[1].mUpperArm = findNode("bip01 r upperarm");
        mArms[1].mForearm = findNode("bip01 r forearm");
        mArms[1].mHand = findNode("bip01 r hand");

        attachPostCallback();
    }

    void ActorIkController::reset()
    {
        // Called whenever the active animation groups change. Bone bindings and filter state are
        // preserved (re-validated in initControllers) so animation changes do not pop the IK.
        mInitialized = false;
    }

    void ActorIkController::update(float /*duration*/)
    {
        if (!mAnimation.mPtr.getClass().isActor())
            return;

        const bool footPlanting = Settings::values().mGame.mFootPlanting.get();
        mFootPlantingActive = footPlanting;
        mLookAtActive = Settings::values().mGame.mLookAt.get();
        mSmootherTurnsActive = Settings::values().mGame.mSmootherTurns.get();

        MWPhysics::Actor* physicActor = getPhysicsActor(mAnimation.mPtr);
        mHasCompoundHitboxes = physicActor != nullptr && physicActor->hasCompoundHitboxes();
        const bool proceduralLean = Settings::values().mGame.mProceduralLean.get();

        if (!footPlanting && !mHasCompoundHitboxes && !proceduralLean)
        {
            mWantIk = false;
            return;
        }

        if (!mInitialized)
            initControllers();

        MWBase::World* world = MWBase::Environment::get().getWorld();
        auto* baseNode = mAnimation.mPtr.getRefData().getBaseNode();
        if (!world || !baseNode || !footPlanting)
        {
            mWantIk = false;
            return;
        }

        const MWWorld::Ptr& ptr = mAnimation.mPtr;
        mWantIk = !world->isSwimming(ptr) && !world->isFlying(ptr) && world->isOnGround(ptr);
    }

    bool ActorIkController::restoreBase(BoneState& bone)
    {
        if (!bone.mNode)
            return false;

        const osg::Matrix& current = bone.mNode->getMatrix();
        if (bone.mHasWritten && current == bone.mWritten)
        {
            // No controller re-animated this bone since our last write: undo it so IK never accumulates.
            bone.mNode->setMatrix(bone.mBase);
        }
        else
        {
            bone.mBase = current;
        }
        bone.mHasWritten = false;
        return true;
    }

    void ActorIkController::restoreBones()
    {
        restoreBase(mPelvis);
        restoreBase(mSpine1);
        for (LegState& leg : mLegs)
        {
            restoreBase(leg.mThigh);
            restoreBase(leg.mCalf);
            restoreBase(leg.mFoot);
        }
    }

    void ActorIkController::probeLegGround(
        LegState& leg, const osg::Vec3f& actorPos, const osg::Vec3f& actorForward, float actorScale)
    {
        leg.mGroundHit = false;
        if (!mWantIk || !leg.mFoot.mNode)
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        if (!world || !world->getRayCasting())
            return;

        const MWWorld::Ptr& ptr = mAnimation.mPtr;
        const osg::Vec3f ankle = worldPos(leg.mFoot.mNode.get());
        const osg::Vec3f toe = leg.mToe
            ? worldPos(leg.mToe.get())
            : (ankle + actorForward * (sToeDistance * actorScale));

        osg::Vec3f toeVec = toe - ankle;
        toeVec.z() = 0.f;
        if (toeVec.length2() < 1.f)
            toeVec = actorForward * (sToeDistance * actorScale);

        auto castDown = [&](const osg::Vec3f& xy, osg::Vec3f& hitPos, osg::Vec3f& hitNormal) -> bool {
            const osg::Vec3f from(xy.x(), xy.y(), actorPos.z() + sProbeAbove * actorScale);
            const osg::Vec3f to(xy.x(), xy.y(), actorPos.z() - sProbeBelow * actorScale);
            const MWPhysics::RayCastingResult hit = world->getRayCasting()->castRay(from, to, { ptr }, {},
                MWPhysics::Layers::WORLD | MWPhysics::Layers::HEIGHTMAP | MWPhysics::Layers::DOOR);
            if (!hit.mHit)
                return false;
            hitPos = hit.mHitPos;
            hitNormal = hit.mHitNormal;
            return true;
        };

        osg::Vec3f heelPos, heelNormal;
        if (!castDown(ankle, heelPos, heelNormal))
            return;

        if (heelNormal.length2() < 1e-6f)
            heelNormal = osg::Vec3f(0.f, 0.f, 1.f);
        heelNormal.normalize();

        float groundZ = heelPos.z();
        osg::Vec3f combinedNormal = heelNormal;

        osg::Vec3f toePos, toeNormal;
        if (castDown(ankle + toeVec, toePos, toeNormal)
            && std::abs(toePos.z() - heelPos.z()) < sSnapDistance * actorScale)
        {
            if (toeNormal.length2() > 1e-6f)
            {
                toeNormal.normalize();
                combinedNormal = (heelNormal + toeNormal) * 0.5f;
            }
            combinedNormal = clampTilt(combinedNormal);

            // Height of the tilted sole at the toe relative to the ankle:
            const float alignedRise
                = -(combinedNormal.x() * toeVec.x() + combinedNormal.y() * toeVec.y())
                / std::max(combinedNormal.z(), 0.25f);
            groundZ = std::max(heelPos.z(), toePos.z() - alignedRise);
        }
        else
        {
            combinedNormal = clampTilt(combinedNormal);
        }

        leg.mGroundHit = true;
        leg.mGroundZ = groundZ;
        leg.mGroundNormal = combinedNormal;
    }

    void ActorIkController::applyPostAnimation(double simulationTime)
    {
        float dt = 1.f / 60.f;
        if (mLastSimTime >= 0.0)
            dt = static_cast<float>(std::clamp(simulationTime - mLastSimTime, 0.0, 0.1));
        mLastSimTime = simulationTime;

        // Always start from the animated pose of this frame.
        restoreBones();

        auto* baseNode = mAnimation.mPtr.getRefData().getBaseNode();
        if (!baseNode)
            return;

        const osg::Vec3f actorPos = baseNode->getPosition();
        const float actorZ = actorPos.z();
        const float actorScale = std::max(0.25f, baseNode->getScale().z());

        osg::Vec3f forward = baseNode->getAttitude() * osg::Vec3f(0.f, 1.f, 0.f);
        forward.z() = 0.f;
        if (forward.length2() < 1e-6f)
            forward = osg::Vec3f(0.f, 1.f, 0.f);
        forward.normalize();

        MWPhysics::Actor* physicActor = getPhysicsActor(mAnimation.mPtr);
        const bool proceduralLean = Settings::values().mGame.mProceduralLean.get();
        if (proceduralLean && physicActor && mSpine1.mNode)
        {
            const auto& loc = physicActor->getLocomotionState();
            const float leanScale = Settings::values().mGame.mLeanScale.get();
            const float linearAccel = loc.getLinearAcceleration();
            const float angVel = loc.getAngularVelocity();
            const float speed = loc.getSmoothedSpeed();

            const float maxLeanRad = osg::DegreesToRadians(8.0f) * leanScale;
            const float pitch = std::clamp(linearAccel / 600.0f, -1.0f, 1.0f) * maxLeanRad;
            const float bank = std::clamp((angVel * speed) / 600.0f, -1.0f, 1.0f) * maxLeanRad;

            if (std::abs(pitch) > 1e-4f || std::abs(bank) > 1e-4f)
            {
                osg::Quat leanRot;
                leanRot.makeRotate(pitch, osg::Vec3f(1.f, 0.f, 0.f), bank, osg::Vec3f(0.f, 1.f, 0.f), 0.f, osg::Vec3f(0.f, 0.f, 1.f));

                const osg::Matrix local = mSpine1.mNode->getMatrix();
                const osg::Matrix parentWorld = osg::Matrix::inverse(local) * worldMatrix(mSpine1.mNode.get());
                const osg::Quat parentRot = parentWorld.getRotate();
                const osg::Quat actorRot = baseNode->getAttitude();
                const osg::Quat worldLean = actorRot * leanRot * actorRot.inverse();
                const osg::Quat localLean = parentRot.inverse() * worldLean * parentRot;

                osg::Matrix moved = local;
                moved.setRotate(localLean * local.getRotate());
                mSpine1.mNode->setMatrix(moved);
                mSpine1.mWritten = moved;
                mSpine1.mHasWritten = true;
            }
        }

        if (!mFootPlantingActive)
        {
            mIkWeight = 0.f;
            mPelvisInit = false;
            for (LegState& leg : mLegs)
                leg.mFilterInit = false;

            if (mAnimation.mSkeleton && mSpine1.mHasWritten)
                mAnimation.mSkeleton->markBoneMatricesDirty();

            if (mHasCompoundHitboxes)
                updateHitboxes(baseNode, forward, actorScale);
            return;
        }

        mIkWeight += ((mWantIk ? 1.f : 0.f) - mIkWeight) * filterAlpha(sWeightRate, dt);

        // 1. Probe ground directly at the current frame's animated ankle and toe XY (zero 1-frame lag),
        //    and filter ground height and normal in world space.
        for (LegState& leg : mLegs)
        {
            probeLegGround(leg, actorPos, forward, actorScale);
            if (!leg.mGroundHit)
                continue;

            if (!leg.mFilterInit || std::abs(leg.mGroundZ - leg.mFilteredGroundZ) > sSnapDistance * actorScale)
            {
                leg.mFilteredGroundZ = leg.mGroundZ;
                leg.mFilteredNormal = leg.mGroundNormal;
                leg.mFilterInit = true;
            }
            else
            {
                const float rate = leg.mGroundZ > leg.mFilteredGroundZ ? sGroundRiseRate : sGroundFallRate;
                leg.mFilteredGroundZ += (leg.mGroundZ - leg.mFilteredGroundZ) * filterAlpha(rate, dt);

                const float a = filterAlpha(sNormalRate, dt);
                leg.mFilteredNormal = leg.mFilteredNormal * (1.f - a) + leg.mGroundNormal * a;
                if (leg.mFilteredNormal.length2() < 1e-6f)
                    leg.mFilteredNormal = osg::Vec3f(0.f, 0.f, 1.f);
                leg.mFilteredNormal.normalize();
            }
        }

        // 2. Yaw-invariant pelvis drop:
        //    On a continuous slope of normal n, the average foot ground height zAvg and slope steepness
        //    tanAlpha are invariant to actor yaw rotation, so turning in place does not bob the pelvis up
        //    and down. Discrete stair steps (where |zL - zR| exceeds 2 * slopeDrop) add the remaining step drop.
        float pelvisTarget = 0.f;
        const bool leg0Valid = mLegs[0].mGroundHit && mLegs[0].mFilterInit;
        const bool leg1Valid = mLegs[1].mGroundHit && mLegs[1].mFilterInit;
        if (leg0Valid && leg1Valid)
        {
            const float zAvg = 0.5f * (mLegs[0].mFilteredGroundZ + mLegs[1].mFilteredGroundZ);
            osg::Vec3f nAvg = mLegs[0].mFilteredNormal + mLegs[1].mFilteredNormal;
            if (nAvg.length2() < 1e-6f)
                nAvg = osg::Vec3f(0.f, 0.f, 1.f);
            nAvg.normalize();

            const float tanAlpha
                = std::sqrt(nAvg.x() * nAvg.x() + nAvg.y() * nAvg.y()) / std::max(nAvg.z(), 0.35f);
            const float slopeDrop = (sStanceRadius * actorScale) * tanAlpha;
            const float stepDiff = std::max(
                0.f, std::abs(mLegs[0].mFilteredGroundZ - mLegs[1].mFilteredGroundZ) - 2.f * slopeDrop);
            pelvisTarget = (zAvg - actorZ) - slopeDrop - 0.5f * stepDiff;
        }
        else if (leg0Valid)
        {
            pelvisTarget = mLegs[0].mFilteredGroundZ - actorZ;
        }
        else if (leg1Valid)
        {
            pelvisTarget = mLegs[1].mFilteredGroundZ - actorZ;
        }
        pelvisTarget = std::clamp(pelvisTarget, -sMaxPelvisDrop * actorScale, 0.f);

        const float pelvisWorldTarget = actorZ + pelvisTarget;
        if (!mPelvisInit || std::abs(pelvisWorldTarget - mFilteredPelvisWorldZ) > sSnapDistance * actorScale)
        {
            mFilteredPelvisWorldZ = pelvisWorldTarget;
            mPelvisInit = true;
        }
        else
        {
            mFilteredPelvisWorldZ += (pelvisWorldTarget - mFilteredPelvisWorldZ) * filterAlpha(sPelvisRate, dt);
        }

        const float appliedPelvis
            = std::clamp(mFilteredPelvisWorldZ - actorZ, -(sMaxPelvisDrop + 5.f) * actorScale, 12.f * actorScale)
            * mIkWeight;

        if (mPelvis.mNode && std::abs(appliedPelvis) > 1e-3f)
        {
            const osg::Matrix local = mPelvis.mNode->getMatrix();
            const osg::Matrix parentWorld = osg::Matrix::inverse(local) * worldMatrix(mPelvis.mNode.get());
            const osg::Vec3f localDelta
                = osg::Matrix::transform3x3(osg::Vec3f(0.f, 0.f, appliedPelvis), osg::Matrix::inverse(parentWorld));

            osg::Matrix moved = local;
            moved.setTrans(local.getTrans() + localDelta);
            mPelvis.mNode->setMatrix(moved);
            mPelvis.mWritten = moved;
            mPelvis.mHasWritten = true;
        }

        // 3. Solve both legs against the actual animated pose of this frame.
        for (LegState& leg : mLegs)
            solveLeg(leg, actorZ, mPelvis.mNode ? appliedPelvis : 0.f, mIkWeight, forward, actorScale);

        if (mAnimation.mSkeleton)
            mAnimation.mSkeleton->markBoneMatricesDirty();

        // 4. Drive skeletal hitbox capsules from the final animated + IK-solved skeleton pose.
        if (mHasCompoundHitboxes)
            updateHitboxes(baseNode, forward, actorScale);
    }

    void ActorIkController::solveLeg(LegState& leg, float actorZ, float pelvisOffset, float weight,
        const osg::Vec3f& actorForward, float actorScale)
    {
        if (!leg.mThigh.mNode || !leg.mCalf.mNode || !leg.mFoot.mNode)
            return;

        if (!leg.mGroundHit || !leg.mFilterInit || weight < 1e-3f)
            return;

        const osg::Matrix thighWorld = worldMatrix(leg.mThigh.mNode.get());
        const osg::Matrix calfWorld = worldMatrix(leg.mCalf.mNode.get());
        const osg::Matrix footWorld = worldMatrix(leg.mFoot.mNode.get());

        const osg::Vec3f hip = thighWorld.getTrans();
        const osg::Vec3f knee = calfWorld.getTrans();
        const osg::Vec3f ankle = footWorld.getTrans();

        const float animatedAnkleZ = ankle.z() - pelvisOffset;
        const osg::Vec3f clampedNormal = clampTilt(leg.mFilteredNormal);

        // Enforce minimum sole-to-ankle height so the sole never clips below the ground surface.
        const float minSoleHeight = (sMinSoleHeight * actorScale) / std::max(clampedNormal.z(), 0.7f);
        const float animHeightAboveOrigin = std::max(animatedAnkleZ - actorZ, minSoleHeight);
        const float desiredAnkleZ = std::clamp(leg.mFilteredGroundZ + animHeightAboveOrigin,
            actorZ - sMaxFootOffset * actorScale, actorZ + sMaxFootRaise * actorScale);

        const osg::Vec3f target(
            ankle.x(), ankle.y(), animatedAnkleZ + (desiredAnkleZ - animatedAnkleZ) * weight);

        // Bias the knee pole vector along actorForward so nearly-straight legs do not wobble laterally.
        const osg::Vec3f pole = (knee - (hip + ankle) * 0.5f) + actorForward * (8.0f * actorScale);

        const opas::ik::TwoBoneChain chain{ toOpas(hip), toOpas(knee), toOpas(ankle) };
        const opas::ik::HingeLimits limits{ 0.0, 2.8 };
        const opas::ik::TwoBoneResult ik = opas::ik::solveTwoBone(chain, toOpas(target), toOpas(pole), limits);
        if (!ik.valid)
            return;

        const osg::Vec3f newKnee = toOsg(ik.mid);
        const osg::Vec3f newAnkle = toOsg(ik.end);

        // World-space joint rotations derived from CreativeMotion's solved joint positions.
        const osg::Matrix rot1 = osg::Matrix::rotate(knee - hip, newKnee - hip);
        const osg::Vec3f calfDirAfterHip = osg::Matrix::transform3x3(ankle - knee, rot1);
        const osg::Matrix rot2 = osg::Matrix::rotate(calfDirAfterHip, newAnkle - newKnee);

        const osg::Matrix aroundHip = rotateAbout(hip, rot1);
        const osg::Matrix aroundKnee = rotateAbout(newKnee, rot2);

        // Foot keeps its animated world orientation, then is tilted to the (filtered) ground normal.
        osg::Quat tilt;
        tilt.makeRotate(osg::Vec3f(0.f, 0.f, 1.f), clampedNormal);
        osg::Quat weightedTilt;
        weightedTilt.slerp(weight, osg::Quat(), tilt);

        const osg::Matrix newThighWorld = thighWorld * aroundHip;
        const osg::Matrix newCalfWorld = calfWorld * aroundHip * aroundKnee;
        const osg::Matrix newFootWorld = footWorld * osg::Matrix::translate(-ankle)
            * osg::Matrix::rotate(weightedTilt) * osg::Matrix::translate(newAnkle);

        auto writeLocal = [](BoneState& bone, const osg::Matrix& oldWorld, const osg::Matrix& newWorld,
                              const osg::Matrix& parentDelta) {
            const osg::Matrix& local = bone.mNode->getMatrix();
            const osg::Matrix parentWorld = osg::Matrix::inverse(local) * oldWorld;
            const osg::Matrix newParentWorld = parentWorld * parentDelta;
            const osg::Matrix newLocal = newWorld * osg::Matrix::inverse(newParentWorld);
            bone.mNode->setMatrix(newLocal);
            bone.mWritten = newLocal;
            bone.mHasWritten = true;
        };

        writeLocal(leg.mThigh, thighWorld, newThighWorld, osg::Matrix::identity());
        writeLocal(leg.mCalf, calfWorld, newCalfWorld, aroundHip);
        writeLocal(leg.mFoot, footWorld, newFootWorld, aroundHip * aroundKnee);
    }

    void ActorIkController::updateHitboxes(
        const SceneUtil::PositionAttitudeTransform* baseNode, const osg::Vec3f& actorForward, float actorScale)
    {
        if (!mLegs[0].mThigh.mNode || !mLegs[0].mCalf.mNode || !mLegs[0].mFoot.mNode
            || !mLegs[1].mThigh.mNode || !mLegs[1].mCalf.mNode || !mLegs[1].mFoot.mNode
            || !mPelvis.mNode)
        {
            return;
        }

        MWPhysics::Actor* physicActor = getPhysicsActor(mAnimation.mPtr);
        if (!physicActor || !physicActor->hasCompoundHitboxes())
            return;

        const osg::Vec3f actorPos = baseNode->getPosition();
        const osg::Quat invActorRot = baseNode->getAttitude().inverse();
        auto toActorLocal = [&](const osg::Vec3f& worldPt) -> osg::Vec3f {
            return invActorRot * (worldPt - actorPos);
        };

        std::array<MWPhysics::Actor::BoneSegment, MWPhysics::Actor::sNumBoneSegments> segments{};

        // 0..2: Left Leg, 3..5: Right Leg
        for (int i = 0; i < 2; ++i)
        {
            const LegState& leg = mLegs[i];
            const std::size_t baseIdx = static_cast<std::size_t>(i * 3);

            const osg::Vec3f hip = worldPos(leg.mThigh.mNode.get());
            const osg::Vec3f knee = worldPos(leg.mCalf.mNode.get());
            const osg::Vec3f ankle = worldPos(leg.mFoot.mNode.get());

            osg::Vec3f footStart = ankle;
            osg::Vec3f footEnd = ankle + actorForward * (10.f * actorScale);
            if (leg.mToe)
            {
                const osg::Vec3f toe = worldPos(leg.mToe.get());
                osg::Vec3f dir = toe - ankle;
                const float len = dir.length();
                if (len > 1e-3f)
                {
                    dir /= len;
                    footStart = ankle - dir * (2.5f * actorScale);
                    footEnd = toe + dir * (2.5f * actorScale);
                }
            }
            else
            {
                footStart = ankle - actorForward * (3.0f * actorScale) - osg::Vec3f(0.f, 0.f, 3.0f * actorScale);
                footEnd = ankle + actorForward * (9.0f * actorScale) - osg::Vec3f(0.f, 0.f, 3.0f * actorScale);
            }

            segments[baseIdx + 0] = { toActorLocal(footStart), toActorLocal(footEnd) };
            segments[baseIdx + 1] = { toActorLocal(ankle), toActorLocal(knee) };
            segments[baseIdx + 2] = { toActorLocal(knee), toActorLocal(hip) };
        }

        // 6: Pelvis, 7: Chest, 8: Head
        const osg::Vec3f hipsCenter = (worldPos(mLegs[0].mThigh.mNode.get())
                                          + worldPos(mLegs[1].mThigh.mNode.get()))
            * 0.5f;
        const osg::Vec3f spine1Pos = mSpine1.mNode
            ? worldPos(mSpine1.mNode.get())
            : (hipsCenter + osg::Vec3f(0.f, 0.f, 16.f * actorScale));
        const osg::Vec3f neckPos = mNeck
            ? worldPos(mNeck.get())
            : (spine1Pos + osg::Vec3f(0.f, 0.f, 26.f * actorScale));
        const osg::Vec3f headBase = mHead
            ? worldPos(mHead.get())
            : (neckPos + osg::Vec3f(0.f, 0.f, 4.f * actorScale));

        osg::Vec3f pelvisDir = spine1Pos - hipsCenter;
        if (pelvisDir.length2() < 1e-4f)
            pelvisDir = osg::Vec3f(0.f, 0.f, 1.f);
        else
            pelvisDir.normalize();

        osg::Vec3f headUp = headBase - neckPos;
        if (headUp.length2() < 1e-4f)
            headUp = osg::Vec3f(0.f, 0.f, 1.f);
        else
            headUp.normalize();

        segments[6] = {
            toActorLocal(hipsCenter - pelvisDir * (3.0f * actorScale)),
            toActorLocal(spine1Pos + pelvisDir * (3.0f * actorScale))
        };
        segments[7] = { toActorLocal(spine1Pos), toActorLocal(neckPos) };
        segments[8] = {
            toActorLocal(headBase + headUp * (1.5f * actorScale)),
            toActorLocal(headBase + headUp * (12.0f * actorScale))
        };

        // 9..10: Left Arm, 11..12: Right Arm
        for (int i = 0; i < 2; ++i)
        {
            const ArmNodes& arm = mArms[i];
            const std::size_t baseIdx = static_cast<std::size_t>(9 + i * 2);
            const float side = (i == 0) ? -1.f : 1.f;

            const osg::Vec3f shoulder = arm.mUpperArm
                ? worldPos(arm.mUpperArm.get())
                : (neckPos + osg::Vec3f(side * 12.f * actorScale, 0.f, -4.f * actorScale));
            const osg::Vec3f elbow = arm.mForearm
                ? worldPos(arm.mForearm.get())
                : (shoulder + osg::Vec3f(0.f, 0.f, -20.f * actorScale));
            const osg::Vec3f wrist = arm.mHand
                ? worldPos(arm.mHand.get())
                : (elbow + osg::Vec3f(0.f, 0.f, -18.f * actorScale));

            segments[baseIdx + 0] = { toActorLocal(shoulder), toActorLocal(elbow) };
            segments[baseIdx + 1] = { toActorLocal(elbow), toActorLocal(wrist) };
        }

        physicActor->updateBoneSegments(segments);
    }
} // namespace MWRender
