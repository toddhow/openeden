#include "actorikcontroller.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwphysics/actor.hpp"
#include "../mwphysics/joltlayers.hpp"
#include "../mwphysics/physicssystem.hpp"
#include "../mwphysics/raycasting.hpp"
#include "../mwworld/class.hpp"

#include <components/mwanimation/object.hpp>
#include <components/mwanimation/play.hpp>
#include <components/settings/values.hpp>

#include <opas/ik/TwoBoneIk.hpp>
#include <opas/math/Vec3.hpp>

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
        // The OSG build's post-animation pass clamped its frame time from the simulation time to this.
        constexpr float sMaxFrameTime = 0.1f;

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

        osg::Matrixf rotateAbout(const osg::Vec3f& pivot, const osg::Matrixf& rotation)
        {
            return osg::Matrixf::translate(-pivot) * rotation * osg::Matrixf::translate(pivot);
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

    ActorIkController::ActorIkController(const MWWorld::Ptr& ptr, MWAnim::Object& object)
        : mPtr(ptr)
        , mObject(object)
    {
    }

    ActorIkController::~ActorIkController() = default;

    void ActorIkController::initControllers()
    {
        if (!mPtr.getClass().isActor() || !mObject.animation)
            return;

        mInitialized = true;
        if (!mPose)
            mPose = std::make_unique<BonePose>(mObject.animation->bones, mObject.transform());

        auto bind = [&](BoneState& bone, const std::string& name) {
            BonePose::Bone found = mPose->find(name);
            if (bone.mBone.mNode != found.mNode)
            {
                bone = BoneState{};
                bone.mBone = std::move(found);
            }
        };

        bind(mPelvis, "bip01 pelvis");
        bind(mLegs[0].mThigh, "bip01 l thigh");
        bind(mLegs[0].mCalf, "bip01 l calf");
        bind(mLegs[0].mFoot, "bip01 l foot");
        mLegs[0].mToe = mPose->find("bip01 l toe0");

        bind(mLegs[1].mThigh, "bip01 r thigh");
        bind(mLegs[1].mCalf, "bip01 r calf");
        bind(mLegs[1].mFoot, "bip01 r foot");
        mLegs[1].mToe = mPose->find("bip01 r toe0");

        bind(mSpine1, "bip01 spine1");
        mNeck = mPose->find("bip01 neck");
        mHead = mPose->find("bip01 head");

        mArms[0].mUpperArm = mPose->find("bip01 l upperarm");
        mArms[0].mForearm = mPose->find("bip01 l forearm");
        mArms[0].mHand = mPose->find("bip01 l hand");

        mArms[1].mUpperArm = mPose->find("bip01 r upperarm");
        mArms[1].mForearm = mPose->find("bip01 r forearm");
        mArms[1].mHand = mPose->find("bip01 r hand");
    }

    void ActorIkController::reset()
    {
        // Called whenever the active animation groups change. Bone bindings and filter state are
        // preserved (re-validated in initControllers) so animation changes do not pop the IK.
        mInitialized = false;
    }

    void ActorIkController::update(float /*duration*/)
    {
        if (!mPtr.getClass().isActor())
            return;

        const bool footPlanting = Settings::values().mGame.mFootPlanting.get();
        mFootPlantingActive = footPlanting;
        mLookAtActive = Settings::values().mGame.mLookAt.get();
        mSmootherTurnsActive = Settings::values().mGame.mSmootherTurns.get();

        MWPhysics::Actor* physicActor = getPhysicsActor(mPtr);
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
        if (!world || !mPose || !footPlanting)
        {
            mWantIk = false;
            return;
        }

        mWantIk = !world->isSwimming(mPtr) && !world->isFlying(mPtr) && world->isOnGround(mPtr);
    }

    ActorIkController::RootState ActorIkController::getRoot() const
    {
        // The actor's root transform takes the place of the OSG build's base node (position, attitude, scale).
        RootState root;
        if (!mPose)
            return root;
        osg::Vec3f scale;
        osg::Quat scaleOrientation;
        mPose->getRoot().decompose(root.mPosition, root.mAttitude, scale, scaleOrientation);
        root.mScale = scale.z();
        return root;
    }

    osg::Vec3f ActorIkController::worldPos(const BonePose::Bone& bone) const
    {
        return mPose->getWorld(bone).getTrans();
    }

    void ActorIkController::writeBone(BoneState& bone, const osg::Matrixf& local)
    {
        mPose->setLocal(bone.mBone, local);
        // Store the matrix as the bone gives it back (it keeps translation, rotation and scale separately), so the
        // next frame's restoreBase can tell exactly whether the animation wrote this bone again.
        bone.mWritten = mPose->getLocal(bone.mBone);
        bone.mHasWritten = true;
    }

    bool ActorIkController::restoreBase(BoneState& bone)
    {
        if (!bone.mBone)
            return false;

        const osg::Matrixf current = mPose->getLocal(bone.mBone);
        if (bone.mHasWritten && current == bone.mWritten)
        {
            // No controller re-animated this bone since our last write: undo it so IK never accumulates.
            mPose->setLocal(bone.mBone, bone.mBase);
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
        if (!mWantIk || !leg.mFoot.mBone)
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        if (!world || !world->getRayCasting())
            return;

        const osg::Vec3f ankle = worldPos(leg.mFoot.mBone);
        const osg::Vec3f toe = leg.mToe ? worldPos(leg.mToe) : (ankle + actorForward * (sToeDistance * actorScale));

        osg::Vec3f toeVec = toe - ankle;
        toeVec.z() = 0.f;
        if (toeVec.length2() < 1.f)
            toeVec = actorForward * (sToeDistance * actorScale);

        auto castDown = [&](const osg::Vec3f& xy, osg::Vec3f& hitPos, osg::Vec3f& hitNormal) -> bool {
            const osg::Vec3f from(xy.x(), xy.y(), actorPos.z() + sProbeAbove * actorScale);
            const osg::Vec3f to(xy.x(), xy.y(), actorPos.z() - sProbeBelow * actorScale);
            const MWPhysics::RayCastingResult hit = world->getRayCasting()->castRay(from, to, { mPtr }, {},
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
            const float alignedRise = -(combinedNormal.x() * toeVec.x() + combinedNormal.y() * toeVec.y())
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

    void ActorIkController::applyPostAnimation(float duration)
    {
        // Nothing to do (and no bone is read or written) until a feature that needs the bones is enabled.
        if (!mInitialized || !mPose)
            return;

        const float dt = std::clamp(duration, 0.f, sMaxFrameTime);

        // Always start from the animated pose of this frame.
        restoreBones();

        const RootState root = getRoot();
        const osg::Vec3f actorPos = root.mPosition;
        const float actorZ = actorPos.z();
        const float actorScale = std::max(0.25f, root.mScale);

        osg::Vec3f forward = root.mAttitude * osg::Vec3f(0.f, 1.f, 0.f);
        forward.z() = 0.f;
        if (forward.length2() < 1e-6f)
            forward = osg::Vec3f(0.f, 1.f, 0.f);
        forward.normalize();

        MWPhysics::Actor* physicActor = getPhysicsActor(mPtr);
        const bool proceduralLean = Settings::values().mGame.mProceduralLean.get();
        if (proceduralLean && physicActor && mSpine1.mBone)
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
                leanRot.makeRotate(pitch, osg::Vec3f(1.f, 0.f, 0.f), bank, osg::Vec3f(0.f, 1.f, 0.f), 0.f,
                    osg::Vec3f(0.f, 0.f, 1.f));

                const osg::Matrixf local = mPose->getLocal(mSpine1.mBone);
                const osg::Matrixf parentWorld = osg::Matrixf::inverse(local) * mPose->getWorld(mSpine1.mBone);
                const osg::Quat parentRot = parentWorld.getRotate();
                const osg::Quat actorRot = root.mAttitude;
                const osg::Quat worldLean = actorRot * leanRot * actorRot.inverse();
                const osg::Quat localLean = parentRot.inverse() * worldLean * parentRot;

                osg::Matrixf moved = local;
                moved.setRotate(localLean * local.getRotate());
                writeBone(mSpine1, moved);
            }
        }

        if (!mFootPlantingActive)
        {
            mIkWeight = 0.f;
            mPelvisInit = false;
            for (LegState& leg : mLegs)
                leg.mFilterInit = false;

            if (mHasCompoundHitboxes)
                updateHitboxes(root, forward, actorScale);
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

            const float tanAlpha = std::sqrt(nAvg.x() * nAvg.x() + nAvg.y() * nAvg.y()) / std::max(nAvg.z(), 0.35f);
            const float slopeDrop = (sStanceRadius * actorScale) * tanAlpha;
            const float stepDiff
                = std::max(0.f, std::abs(mLegs[0].mFilteredGroundZ - mLegs[1].mFilteredGroundZ) - 2.f * slopeDrop);
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

        if (mPelvis.mBone && std::abs(appliedPelvis) > 1e-3f)
        {
            const osg::Matrixf local = mPose->getLocal(mPelvis.mBone);
            const osg::Matrixf parentWorld = osg::Matrixf::inverse(local) * mPose->getWorld(mPelvis.mBone);
            const osg::Vec3f localDelta = osg::Matrixf::transform3x3(
                osg::Vec3f(0.f, 0.f, appliedPelvis), osg::Matrixf::inverse(parentWorld));

            osg::Matrixf moved = local;
            moved.setTrans(local.getTrans() + localDelta);
            writeBone(mPelvis, moved);
        }

        // 3. Solve both legs against the actual animated pose of this frame.
        for (LegState& leg : mLegs)
            solveLeg(leg, actorZ, mPelvis.mBone ? appliedPelvis : 0.f, mIkWeight, forward, actorScale);

        // 4. Drive skeletal hitbox capsules from the final animated + IK-solved skeleton pose.
        if (mHasCompoundHitboxes)
            updateHitboxes(root, forward, actorScale);
    }

    void ActorIkController::solveLeg(LegState& leg, float actorZ, float pelvisOffset, float weight,
        const osg::Vec3f& actorForward, float actorScale)
    {
        if (!leg.mThigh.mBone || !leg.mCalf.mBone || !leg.mFoot.mBone)
            return;

        if (!leg.mGroundHit || !leg.mFilterInit || weight < 1e-3f)
            return;

        const osg::Matrixf thighWorld = mPose->getWorld(leg.mThigh.mBone);
        const osg::Matrixf calfWorld = mPose->getWorld(leg.mCalf.mBone);
        const osg::Matrixf footWorld = mPose->getWorld(leg.mFoot.mBone);

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

        const osg::Vec3f target(ankle.x(), ankle.y(), animatedAnkleZ + (desiredAnkleZ - animatedAnkleZ) * weight);

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
        const osg::Matrixf rot1 = osg::Matrixf::rotate(knee - hip, newKnee - hip);
        const osg::Vec3f calfDirAfterHip = osg::Matrixf::transform3x3(ankle - knee, rot1);
        const osg::Matrixf rot2 = osg::Matrixf::rotate(calfDirAfterHip, newAnkle - newKnee);

        const osg::Matrixf aroundHip = rotateAbout(hip, rot1);
        const osg::Matrixf aroundKnee = rotateAbout(newKnee, rot2);

        // Foot keeps its animated world orientation, then is tilted to the (filtered) ground normal.
        osg::Quat tilt;
        tilt.makeRotate(osg::Vec3f(0.f, 0.f, 1.f), clampedNormal);
        osg::Quat weightedTilt;
        weightedTilt.slerp(weight, osg::Quat(), tilt);

        const osg::Matrixf newThighWorld = thighWorld * aroundHip;
        const osg::Matrixf newCalfWorld = calfWorld * aroundHip * aroundKnee;
        const osg::Matrixf newFootWorld = footWorld * osg::Matrixf::translate(-ankle)
            * osg::Matrixf::rotate(weightedTilt) * osg::Matrixf::translate(newAnkle);

        auto writeLocal = [this](BoneState& bone, const osg::Matrixf& oldWorld, const osg::Matrixf& newWorld,
                              const osg::Matrixf& parentDelta) {
            const osg::Matrixf local = mPose->getLocal(bone.mBone);
            const osg::Matrixf parentWorld = osg::Matrixf::inverse(local) * oldWorld;
            const osg::Matrixf newParentWorld = parentWorld * parentDelta;
            writeBone(bone, newWorld * osg::Matrixf::inverse(newParentWorld));
        };

        writeLocal(leg.mThigh, thighWorld, newThighWorld, osg::Matrixf::identity());
        writeLocal(leg.mCalf, calfWorld, newCalfWorld, aroundHip);
        writeLocal(leg.mFoot, footWorld, newFootWorld, aroundHip * aroundKnee);
    }

    void ActorIkController::updateHitboxes(const RootState& root, const osg::Vec3f& actorForward, float actorScale)
    {
        if (!mLegs[0].mThigh.mBone || !mLegs[0].mCalf.mBone || !mLegs[0].mFoot.mBone || !mLegs[1].mThigh.mBone
            || !mLegs[1].mCalf.mBone || !mLegs[1].mFoot.mBone || !mPelvis.mBone)
        {
            return;
        }

        MWPhysics::Actor* physicActor = getPhysicsActor(mPtr);
        if (!physicActor || !physicActor->hasCompoundHitboxes())
            return;

        const osg::Vec3f actorPos = root.mPosition;
        const osg::Quat invActorRot = root.mAttitude.inverse();
        auto toActorLocal = [&](const osg::Vec3f& worldPt) -> osg::Vec3f { return invActorRot * (worldPt - actorPos); };

        std::array<MWPhysics::Actor::BoneSegment, MWPhysics::Actor::sNumBoneSegments> segments{};

        // 0..2: Left Leg, 3..5: Right Leg
        for (int i = 0; i < 2; ++i)
        {
            const LegState& leg = mLegs[i];
            const std::size_t baseIdx = static_cast<std::size_t>(i * 3);

            const osg::Vec3f hip = worldPos(leg.mThigh.mBone);
            const osg::Vec3f knee = worldPos(leg.mCalf.mBone);
            const osg::Vec3f ankle = worldPos(leg.mFoot.mBone);

            osg::Vec3f footStart = ankle;
            osg::Vec3f footEnd = ankle + actorForward * (10.f * actorScale);
            if (leg.mToe)
            {
                const osg::Vec3f toe = worldPos(leg.mToe);
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
        const osg::Vec3f hipsCenter = (worldPos(mLegs[0].mThigh.mBone) + worldPos(mLegs[1].mThigh.mBone)) * 0.5f;
        const osg::Vec3f spine1Pos
            = mSpine1.mBone ? worldPos(mSpine1.mBone) : (hipsCenter + osg::Vec3f(0.f, 0.f, 16.f * actorScale));
        const osg::Vec3f neckPos = mNeck ? worldPos(mNeck) : (spine1Pos + osg::Vec3f(0.f, 0.f, 26.f * actorScale));
        const osg::Vec3f headBase = mHead ? worldPos(mHead) : (neckPos + osg::Vec3f(0.f, 0.f, 4.f * actorScale));

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

        segments[6] = { toActorLocal(hipsCenter - pelvisDir * (3.0f * actorScale)),
            toActorLocal(spine1Pos + pelvisDir * (3.0f * actorScale)) };
        segments[7] = { toActorLocal(spine1Pos), toActorLocal(neckPos) };
        segments[8] = { toActorLocal(headBase + headUp * (1.5f * actorScale)),
            toActorLocal(headBase + headUp * (12.0f * actorScale)) };

        // 9..10: Left Arm, 11..12: Right Arm
        for (int i = 0; i < 2; ++i)
        {
            const ArmNodes& arm = mArms[i];
            const std::size_t baseIdx = static_cast<std::size_t>(9 + i * 2);
            const float side = (i == 0) ? -1.f : 1.f;

            const osg::Vec3f shoulder = arm.mUpperArm
                ? worldPos(arm.mUpperArm)
                : (neckPos + osg::Vec3f(side * 12.f * actorScale, 0.f, -4.f * actorScale));
            const osg::Vec3f elbow
                = arm.mForearm ? worldPos(arm.mForearm) : (shoulder + osg::Vec3f(0.f, 0.f, -20.f * actorScale));
            const osg::Vec3f wrist
                = arm.mHand ? worldPos(arm.mHand) : (elbow + osg::Vec3f(0.f, 0.f, -18.f * actorScale));

            segments[baseIdx + 0] = { toActorLocal(shoulder), toActorLocal(elbow) };
            segments[baseIdx + 1] = { toActorLocal(elbow), toActorLocal(wrist) };
        }

        physicActor->updateBoneSegments(segments);
    }
} // namespace MWRender
