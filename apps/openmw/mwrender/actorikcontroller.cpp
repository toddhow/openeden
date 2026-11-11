#include "actorikcontroller.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwmechanics/actorutil.hpp"
#include "../mwphysics/actor.hpp"
#include "../mwphysics/joltlayers.hpp"
#include "../mwphysics/physicssystem.hpp"
#include "../mwphysics/raycasting.hpp"
#include "../mwworld/class.hpp"

#include <components/debug/debuglog.hpp>
#include <components/misc/vsgmath.hpp>

#include <components/mwanimation/object.hpp>
#include <components/mwanimation/play.hpp>
#include <components/settings/values.hpp>
#include <vsg/maths/transform.h>

#include "footikmath.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

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

        // Foot lock (task 0108): an animated ankle counts as on the ground when it is this far (times the actor's
        // scale) above the lowest ankle height the sole allows. The stance ankle of Morrowind's idle and walk poses
        // is within about 1 unit of it (docs/research/0052).
        constexpr float sFootLockPlantHeight = 2.5f;
        // The body speed (units per second) is the measured one smoothed at this rate (1/s): the physics position
        // is not updated every frame at every frame rate.
        constexpr float sBodySpeedRate = 20.f;
        // A move of the actor origin farther than this in one frame is a teleport: the locks are dropped.
        constexpr float sTeleportDistance = 100.f;

        float filterAlpha(float rate, float dt)
        {
            return std::clamp(1.f - std::exp(-rate * dt), 0.f, 1.f);
        }

        vsg::mat4 inverseOf(const vsg::mat4& matrix)
        {
            vsg::mat4 result;
            Misc::VsgMath::invert(matrix, result);
            return result;
        }

        vsg::vec3 clampTilt(vsg::vec3 n)
        {
            const vsg::vec3 up(0.f, 0.f, 1.f);
            if (vsg::length2(n) < 1e-6f)
                return up;
            Misc::VsgMath::normalizeInPlace(n);
            const float angle = std::acos(std::clamp(n.z, -1.f, 1.f));
            if (angle <= sMaxFootTilt)
                return n;
            vsg::vec3 axis = vsg::cross(up, n);
            if (vsg::length2(axis) < 1e-6f)
                return up;
            Misc::VsgMath::normalizeInPlace(axis);
            return vsg::quat(Misc::VsgMath::makeRotateFromAxis(sMaxFootTilt, axis)) * up;
        }

        MWPhysics::Actor* getPhysicsActor(const MWWorld::Ptr& ptr)
        {
            MWBase::World* world = MWBase::Environment::get().getWorld();
            if (!world)
                return nullptr;
            const auto* rayCasting = world->getRayCasting();
            if (!rayCasting)
                return nullptr;
            auto* physics
                = const_cast<MWPhysics::PhysicsSystem*>(static_cast<const MWPhysics::PhysicsSystem*>(rayCasting));
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
        vsg::vec3 scale;
        vsg::dquat scaleOrientation;
        Misc::VsgMath::decompose(mPose->getRoot(), root.mPosition, root.mAttitude, scale, scaleOrientation);
        root.mScale = scale.z;
        return root;
    }

    vsg::vec3 ActorIkController::worldPos(const BonePose::Bone& bone) const
    {
        return vsg::vec3(Misc::VsgMath::getTrans(mPose->getWorld(bone)));
    }

    bool ActorIkController::restoreBase(BoneState& bone)
    {
        if (!bone.mBone)
            return false;
        FootIk::restoreBase(*mPose, bone.mBone, bone.mBase);
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
        LegState& leg, const vsg::vec3& actorPos, const vsg::vec3& actorForward, float actorScale)
    {
        leg.mGroundHit = false;
        if (!mWantIk || !leg.mFoot.mBone)
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        if (!world || !world->getRayCasting())
            return;

        const vsg::vec3 ankle = worldPos(leg.mFoot.mBone);
        const vsg::vec3 toe = leg.mToe ? worldPos(leg.mToe) : (ankle + actorForward * (sToeDistance * actorScale));

        vsg::vec3 toeVec = toe - ankle;
        toeVec.z = 0.f;
        if (vsg::length2(toeVec) < 1.f)
            toeVec = actorForward * (sToeDistance * actorScale);

        auto castDown = [&](const vsg::vec3& xy, vsg::vec3& hitPos, vsg::vec3& hitNormal) -> bool {
            const vsg::vec3 from(xy.x, xy.y, actorPos.z + sProbeAbove * actorScale);
            const vsg::vec3 to(xy.x, xy.y, actorPos.z - sProbeBelow * actorScale);
            const MWPhysics::RayCastingResult hit = world->getRayCasting()->castRay(from, to, { mPtr }, {},
                MWPhysics::Layers::WORLD | MWPhysics::Layers::HEIGHTMAP | MWPhysics::Layers::DOOR);
            if (!hit.mHit)
                return false;
            hitPos = hit.mHitPos;
            hitNormal = hit.mHitNormal;
            return true;
        };

        vsg::vec3 heelPos, heelNormal;
        if (!castDown(ankle, heelPos, heelNormal))
            return;

        if (vsg::length2(heelNormal) < 1e-6f)
            heelNormal = vsg::vec3(0.f, 0.f, 1.f);
        Misc::VsgMath::normalizeInPlace(heelNormal);

        float groundZ = heelPos.z;
        vsg::vec3 combinedNormal = heelNormal;

        vsg::vec3 toePos, toeNormal;
        if (castDown(ankle + toeVec, toePos, toeNormal) && std::abs(toePos.z - heelPos.z) < sSnapDistance * actorScale)
        {
            if (vsg::length2(toeNormal) > 1e-6f)
            {
                Misc::VsgMath::normalizeInPlace(toeNormal);
                combinedNormal = (heelNormal + toeNormal) * 0.5f;
            }
            combinedNormal = clampTilt(combinedNormal);

            // Height of the tilted sole at the toe relative to the ankle:
            const float alignedRise
                = -(combinedNormal.x * toeVec.x + combinedNormal.y * toeVec.y) / std::max(combinedNormal.z, 0.25f);
            groundZ = std::max(heelPos.z, toePos.z - alignedRise);
        }
        else
        {
            combinedNormal = clampTilt(combinedNormal);
        }

        leg.mGroundHit = true;
        leg.mGroundZ = groundZ;
        leg.mGroundNormal = combinedNormal;
    }

    void ActorIkController::updateBodySpeed(const RootState& root, float dt)
    {
        const vsg::vec2 xy(root.mPosition.x, root.mPosition.y);
        if (mHasLastRoot && dt > 0.f)
        {
            const float moved = std::hypot(xy.x - mLastRootXY.x, xy.y - mLastRootXY.y);
            if (moved > sTeleportDistance)
            {
                mBodySpeed = 0.f;
                for (LegState& leg : mLegs)
                    leg.mLock.reset();
            }
            else
                mBodySpeed += (moved / dt - mBodySpeed) * filterAlpha(sBodySpeedRate, dt);
        }
        mHasLastRoot = true;
        mLastRootXY = xy;
    }

    void ActorIkController::applyPostAnimation(float duration)
    {
        // Nothing to do (and no bone is read or written) until a feature that needs the bones is enabled.
        if (!mInitialized || !mPose)
            return;

        const float dt = std::clamp(duration, 0.f, sMaxFrameTime);
        mFrameDt = dt;

        // Always start from the animated pose of this frame.
        restoreBones();

        const RootState root = getRoot();
        updateBodySpeed(root, dt);
        const vsg::vec3 actorPos = root.mPosition;
        const float actorZ = actorPos.z;
        const float actorScale = std::max(0.25f, root.mScale);

        vsg::vec3 forward = vsg::quat(root.mAttitude) * vsg::vec3(0.f, 1.f, 0.f);
        forward.z = 0.f;
        if (vsg::length2(forward) < 1e-6f)
            forward = vsg::vec3(0.f, 1.f, 0.f);
        Misc::VsgMath::normalizeInPlace(forward);

        MWPhysics::Actor* physicActor = getPhysicsActor(mPtr);
        const bool proceduralLean = Settings::values().mGame.mProceduralLean.get();
        if (proceduralLean && physicActor && mSpine1.mBone)
        {
            const auto& loc = physicActor->getLocomotionState();
            const float leanScale = Settings::values().mGame.mLeanScale.get();
            const float linearAccel = loc.getLinearAcceleration();
            const float angVel = loc.getAngularVelocity();
            const float speed = loc.getSmoothedSpeed();

            const float maxLeanRad = Misc::VsgMath::inDegrees(8.0f) * leanScale;
            const float targetPitch = std::clamp(linearAccel / 600.0f, -1.0f, 1.0f) * maxLeanRad;
            const float targetBank = std::clamp((angVel * speed) / 600.0f, -1.0f, 1.0f) * maxLeanRad;

            // The targets step: inertial locomotion accelerates at a constant rate, so the acceleration jumps from
            // zero to its full value on the first frame of a walk. Ease the angles toward them instead.
            const float pitch = mLeanPitch.update(targetPitch, dt, InertialMotion::LeanSmootherParameters{});
            const float bank = mLeanBank.update(targetBank, dt, InertialMotion::LeanSmootherParameters{});

            if (Settings::game().mCreativeMotionAnimationDebug)
            {
                Log(Debug::Info) << "CMLEAN ref=" << mPtr.getCellRef().getRefId().toDebugString() << " dt=" << dt
                                 << " targetpitchdeg=" << Misc::VsgMath::radiansToDegrees(targetPitch)
                                 << " accel=" << linearAccel << " angvel=" << angVel << " speed=" << speed
                                 << " pitchdeg=" << Misc::VsgMath::radiansToDegrees(pitch)
                                 << " bankdeg=" << Misc::VsgMath::radiansToDegrees(bank);
            }

            if (std::abs(pitch) > 1e-4f || std::abs(bank) > 1e-4f)
            {
                const vsg::dquat leanRot = Misc::VsgMath::makeRotateFromAxis(pitch, vsg::vec3(1.f, 0.f, 0.f))
                    * Misc::VsgMath::makeRotateFromAxis(bank, vsg::vec3(0.f, 1.f, 0.f))
                    * Misc::VsgMath::makeRotateFromAxis(0.f, vsg::vec3(0.f, 0.f, 1.f));

                FootIk::applyLean(*mPose, mSpine1.mBone, root.mAttitude, leanRot);
                FootIk::markRotationWritten(*mPose, mSpine1.mBone, mSpine1.mBase);
            }
        }
        else
        {
            mLeanPitch.reset();
            mLeanBank.reset();
        }

        if (!mFootPlantingActive)
        {
            mIkWeight = 0.f;
            mPelvisInit = false;
            for (LegState& leg : mLegs)
            {
                leg.mFilterInit = false;
                leg.mLock.reset();
            }

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
                if (vsg::length2(leg.mFilteredNormal) < 1e-6f)
                    leg.mFilteredNormal = vsg::vec3(0.f, 0.f, 1.f);
                Misc::VsgMath::normalizeInPlace(leg.mFilteredNormal);
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
            vsg::vec3 nAvg = mLegs[0].mFilteredNormal + mLegs[1].mFilteredNormal;
            if (vsg::length2(nAvg) < 1e-6f)
                nAvg = vsg::vec3(0.f, 0.f, 1.f);
            Misc::VsgMath::normalizeInPlace(nAvg);

            const float tanAlpha = std::sqrt(nAvg.x * nAvg.x + nAvg.y * nAvg.y) / std::max(nAvg.z, 0.35f);
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
            const vsg::mat4 local = mPose->getLocal(mPelvis.mBone);
            const vsg::mat4 parentWorld = mPose->getWorld(mPelvis.mBone) * inverseOf(local);
            const vsg::vec3 localDelta
                = Misc::VsgMath::transform3x3(vsg::vec3(0.f, 0.f, appliedPelvis), inverseOf(parentWorld));

            mPose->setLocalTranslation(mPelvis.mBone, mPose->getLocalTranslation(mPelvis.mBone) + localDelta);
            FootIk::markTranslationWritten(*mPose, mPelvis.mBone, mPelvis.mBase);
        }

        // 3. Solve both legs against the actual animated pose of this frame.
        std::string shownText;
        const bool trace = Settings::game().mCreativeMotionAnimationDebug && mPtr == MWMechanics::getPlayer();
        for (LegState& leg : mLegs)
        {
            solveLeg(leg, actorZ, mPelvis.mBone ? appliedPelvis : 0.f, mIkWeight, forward, actorScale);
            if (leg.mFoot.mBone)
            {
                const vsg::vec3 shown = worldPos(leg.mFoot.mBone);
                leg.mLastShown = vsg::vec2(shown.x, shown.y);
                if (trace)
                    shownText += " " + std::string(&leg == &mLegs[0] ? "L" : "R") + "=(" + std::to_string(shown.x) + " "
                        + std::to_string(shown.y) + " " + std::to_string(shown.z) + ")";
            }
        }
        // The ankles as drawn, after the leg solve: tools/footslip_check measures the foot slip from these.
        if (trace)
            Log(Debug::Info) << "CMFEETIK" << shownText;

        // 4. Drive skeletal hitbox capsules from the final animated + IK-solved skeleton pose.
        if (mHasCompoundHitboxes)
            updateHitboxes(root, forward, actorScale);
    }

    void ActorIkController::solveLeg(
        LegState& leg, float actorZ, float pelvisOffset, float weight, const vsg::vec3& actorForward, float actorScale)
    {
        if (!leg.mThigh.mBone || !leg.mCalf.mBone || !leg.mFoot.mBone)
            return;

        if (!leg.mGroundHit || !leg.mFilterInit || weight < 1e-3f)
        {
            leg.mLock.reset();
            return;
        }

        const vsg::mat4 thighWorld = mPose->getWorld(leg.mThigh.mBone);
        const vsg::mat4 calfWorld = mPose->getWorld(leg.mCalf.mBone);
        const vsg::mat4 footWorld = mPose->getWorld(leg.mFoot.mBone);

        const vsg::vec3 hip(Misc::VsgMath::getTrans(thighWorld));
        const vsg::vec3 knee(Misc::VsgMath::getTrans(calfWorld));
        const vsg::vec3 ankle(Misc::VsgMath::getTrans(footWorld));

        const float animatedAnkleZ = ankle.z - pelvisOffset;
        const vsg::vec3 clampedNormal = clampTilt(leg.mFilteredNormal);

        // Enforce minimum sole-to-ankle height so the sole never clips below the ground surface.
        const float minSoleHeight = (sMinSoleHeight * actorScale) / std::max(clampedNormal.z, 0.7f);
        const float animHeightAboveOrigin = std::max(animatedAnkleZ - actorZ, minSoleHeight);
        const float desiredAnkleZ = std::clamp(leg.mFilteredGroundZ + animHeightAboveOrigin,
            actorZ - sMaxFootOffset * actorScale, actorZ + sMaxFootRaise * actorScale);

        vsg::vec3 target(ankle.x, ankle.y, animatedAnkleZ + (desiredAnkleZ - animatedAnkleZ) * weight);

        // Foot lock (task 0108): while the body stands still, a foot on the ground is held where it was drawn.
        if (Settings::game().mFootLock)
        {
            const bool planted
                = animatedAnkleZ - leg.mFilteredGroundZ <= (sMinSoleHeight + sFootLockPlantHeight) * actorScale;
            const InertialMotion::FootLockResult lock = leg.mLock.update(vsg::vec2(ankle.x, ankle.y), leg.mLastShown,
                planted, mBodySpeed, mFrameDt, InertialMotion::FootLockParameters{});
            target.x = ankle.x + (lock.mTarget.x - ankle.x) * lock.mWeight;
            target.y = ankle.y + (lock.mTarget.y - ankle.y) * lock.mWeight;
            if (Settings::game().mCreativeMotionAnimationDebug && mPtr == MWMechanics::getPlayer())
            {
                Log(Debug::Info) << "CMLOCK foot=" << (&leg == &mLegs[0] ? 'L' : 'R') << " planted=" << planted
                                 << " bodyspeed=" << mBodySpeed << " engaged=" << leg.mLock.isEngaged()
                                 << " weight=" << lock.mWeight << " held=(" << lock.mTarget.x << " " << lock.mTarget.y
                                 << ") animated=(" << ankle.x << " " << ankle.y << ")";
            }
        }
        else
            leg.mLock.reset();

        // Bias the knee pole vector along actorForward so nearly-straight legs do not wobble laterally.
        const vsg::vec3 pole = (knee - (hip + ankle) * 0.5f) + actorForward * (8.0f * actorScale);

        const FootIk::LegSolution solution
            = FootIk::solveLegPose({ thighWorld, calfWorld, footWorld }, target, pole, clampedNormal, weight);
        if (!solution.mValid)
            return;

        // Only the three bones' rotations change; their translations and scales stay as animated.
        FootIk::applyLeg(*mPose, leg.mThigh.mBone, leg.mCalf.mBone, leg.mFoot.mBone, solution);
        FootIk::markRotationWritten(*mPose, leg.mThigh.mBone, leg.mThigh.mBase);
        FootIk::markRotationWritten(*mPose, leg.mCalf.mBone, leg.mCalf.mBase);
        FootIk::markRotationWritten(*mPose, leg.mFoot.mBone, leg.mFoot.mBase);
    }

    void ActorIkController::updateHitboxes(const RootState& root, const vsg::vec3& actorForward, float actorScale)
    {
        if (!mLegs[0].mThigh.mBone || !mLegs[0].mCalf.mBone || !mLegs[0].mFoot.mBone || !mLegs[1].mThigh.mBone
            || !mLegs[1].mCalf.mBone || !mLegs[1].mFoot.mBone || !mPelvis.mBone)
        {
            return;
        }

        MWPhysics::Actor* physicActor = getPhysicsActor(mPtr);
        if (!physicActor || !physicActor->hasCompoundHitboxes())
            return;

        const vsg::vec3 actorPos = root.mPosition;
        const vsg::dquat invActorRot = Misc::VsgMath::inverse(root.mAttitude);
        auto toActorLocal
            = [&](const vsg::vec3& worldPt) -> vsg::vec3 { return vsg::quat(invActorRot) * (worldPt - actorPos); };

        std::array<MWPhysics::Actor::BoneSegment, MWPhysics::Actor::sNumBoneSegments> segments{};

        // 0..2: Left Leg, 3..5: Right Leg
        for (int i = 0; i < 2; ++i)
        {
            const LegState& leg = mLegs[i];
            const std::size_t baseIdx = static_cast<std::size_t>(i * 3);

            const vsg::vec3 hip = worldPos(leg.mThigh.mBone);
            const vsg::vec3 knee = worldPos(leg.mCalf.mBone);
            const vsg::vec3 ankle = worldPos(leg.mFoot.mBone);

            vsg::vec3 footStart = ankle;
            vsg::vec3 footEnd = ankle + actorForward * (10.f * actorScale);
            if (leg.mToe)
            {
                const vsg::vec3 toe = worldPos(leg.mToe);
                vsg::vec3 dir = toe - ankle;
                const float len = vsg::length(dir);
                if (len > 1e-3f)
                {
                    dir = Misc::VsgMath::divide(dir, len);
                    footStart = ankle - dir * (2.5f * actorScale);
                    footEnd = toe + dir * (2.5f * actorScale);
                }
            }
            else
            {
                footStart = ankle - actorForward * (3.0f * actorScale) - vsg::vec3(0.f, 0.f, 3.0f * actorScale);
                footEnd = ankle + actorForward * (9.0f * actorScale) - vsg::vec3(0.f, 0.f, 3.0f * actorScale);
            }

            segments[baseIdx + 0] = { toActorLocal(footStart), toActorLocal(footEnd) };
            segments[baseIdx + 1] = { toActorLocal(ankle), toActorLocal(knee) };
            segments[baseIdx + 2] = { toActorLocal(knee), toActorLocal(hip) };
        }

        // 6: Pelvis, 7: Chest, 8: Head
        const vsg::vec3 hipsCenter = (worldPos(mLegs[0].mThigh.mBone) + worldPos(mLegs[1].mThigh.mBone)) * 0.5f;
        const vsg::vec3 spine1Pos
            = mSpine1.mBone ? worldPos(mSpine1.mBone) : (hipsCenter + vsg::vec3(0.f, 0.f, 16.f * actorScale));
        const vsg::vec3 neckPos = mNeck ? worldPos(mNeck) : (spine1Pos + vsg::vec3(0.f, 0.f, 26.f * actorScale));
        const vsg::vec3 headBase = mHead ? worldPos(mHead) : (neckPos + vsg::vec3(0.f, 0.f, 4.f * actorScale));

        vsg::vec3 pelvisDir = spine1Pos - hipsCenter;
        if (vsg::length2(pelvisDir) < 1e-4f)
            pelvisDir = vsg::vec3(0.f, 0.f, 1.f);
        else
            Misc::VsgMath::normalizeInPlace(pelvisDir);

        vsg::vec3 headUp = headBase - neckPos;
        if (vsg::length2(headUp) < 1e-4f)
            headUp = vsg::vec3(0.f, 0.f, 1.f);
        else
            Misc::VsgMath::normalizeInPlace(headUp);

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

            const vsg::vec3 shoulder = arm.mUpperArm
                ? worldPos(arm.mUpperArm)
                : (neckPos + vsg::vec3(side * 12.f * actorScale, 0.f, -4.f * actorScale));
            const vsg::vec3 elbow
                = arm.mForearm ? worldPos(arm.mForearm) : (shoulder + vsg::vec3(0.f, 0.f, -20.f * actorScale));
            const vsg::vec3 wrist = arm.mHand ? worldPos(arm.mHand) : (elbow + vsg::vec3(0.f, 0.f, -18.f * actorScale));

            segments[baseIdx + 0] = { toActorLocal(shoulder), toActorLocal(elbow) };
            segments[baseIdx + 1] = { toActorLocal(elbow), toActorLocal(wrist) };
        }

        physicActor->updateBoneSegments(segments);
    }
} // namespace MWRender
