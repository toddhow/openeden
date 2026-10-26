#include "actorikcontroller.hpp"
#include "animation.hpp"
#include "rotatecontroller.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/class.hpp"
#include "../mwphysics/raycasting.hpp"
#include "../mwphysics/joltlayers.hpp"

#include <components/debug/debuglog.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/settings/values.hpp>

#include <osg/MatrixTransform>
#include <algorithm>
#include <cmath>

namespace MWRender
{
    ActorIkController::ActorIkController(Animation& animation)
        : mAnimation(animation)
    {
    }

    ActorIkController::~ActorIkController() = default;

    void ActorIkController::initControllers()
    {
        mInitialized = true;
        mLeftFootController = mAnimation.addRotateController("bip01 l foot");
        mLeftCalfController = mAnimation.addRotateController("bip01 l calf");
        mLeftThighController = mAnimation.addRotateController("bip01 l thigh");

        mRightFootController = mAnimation.addRotateController("bip01 r foot");
        mRightCalfController = mAnimation.addRotateController("bip01 r calf");
        mRightThighController = mAnimation.addRotateController("bip01 r thigh");

        Log(Debug::Warning) << "ActorIK initControllers for " << mAnimation.mPtr.getCellRef().getRefId()
                            << ": lFoot=" << (mLeftFootController != nullptr)
                            << " rFoot=" << (mRightFootController != nullptr);
    }

    void ActorIkController::update(float /*duration*/)
    {
        // Early out for non-actors (doors, containers, flora) - IK only applies to actors!
        if (!mAnimation.mPtr.getClass().isActor())
            return;

        const bool footPlanting = Settings::values().mGame.mFootPlanting.get();
        const bool lookAt = Settings::values().mGame.mLookAt.get();
        const bool smootherTurns = Settings::values().mGame.mSmootherTurns.get();

        mFootPlantingActive = footPlanting;
        mLookAtActive = lookAt;
        mSmootherTurnsActive = smootherTurns;

        // Invariant: Off-by-default. If all IK features are disabled, early-exit immediately!
        // Zero raycasts, zero scene graph mutations.
        if (!footPlanting && !lookAt && !smootherTurns)
        {
            if (mLeftThighController)
                mLeftThighController->setEnabled(false);
            if (mLeftCalfController)
                mLeftCalfController->setEnabled(false);
            if (mLeftFootController)
                mLeftFootController->setEnabled(false);
            if (mRightThighController)
                mRightThighController->setEnabled(false);
            if (mRightCalfController)
                mRightCalfController->setEnabled(false);
            if (mRightFootController)
                mRightFootController->setEnabled(false);
            return;
        }

        if (!mInitialized)
            initControllers();

        if (!mLeftFootController && !mRightFootController)
            return;

        auto world = MWBase::Environment::get().getWorld();
        if (!world)
            return;

        const bool isPlayer = (mAnimation.mPtr == world->getPlayerPtr());

        if (footPlanting)
        {
            const bool disableDueToState = world->isSwimming(mAnimation.mPtr) || world->isFlying(mAnimation.mPtr);

            if (disableDueToState)
            {
                if (mLeftThighController) mLeftThighController->setEnabled(false);
                if (mLeftCalfController) mLeftCalfController->setEnabled(false);
                if (mLeftFootController) mLeftFootController->setEnabled(false);
                if (mRightThighController) mRightThighController->setEnabled(false);
                if (mRightCalfController) mRightCalfController->setEnabled(false);
                if (mRightFootController) mRightFootController->setEnabled(false);
                return;
            }

            osg::Quat actorAttitude = osg::Quat();
            float actorGroundZ = 0.0f;
            if (auto* baseNode = mAnimation.mPtr.getRefData().getBaseNode())
            {
                actorAttitude = baseNode->getAttitude();
                actorGroundZ = baseNode->getPosition().z();
            }
            const osg::Quat actorAttitudeInv = actorAttitude.inverse();
            const osg::Vec3f actorForward = actorAttitude * osg::Vec3f(0.0f, 1.0f, 0.0f);

            auto plantLeg = [&](std::string_view footBone,
                                osg::ref_ptr<RotateController>& footCtrl,
                                osg::ref_ptr<RotateController>& calfCtrl,
                                osg::ref_ptr<RotateController>& thighCtrl) {
                if (!footCtrl)
                    return;

                auto it = mAnimation.getNodeMap().find(footBone);
                if (it == mAnimation.getNodeMap().end() || !it->second)
                {
                    footCtrl->setEnabled(false);
                    if (calfCtrl) calfCtrl->setEnabled(false);
                    if (thighCtrl) thighCtrl->setEnabled(false);
                    return;
                }

                osg::NodePathList nodepaths = it->second->getParentalNodePaths();
                if (nodepaths.empty())
                {
                    footCtrl->setEnabled(false);
                    if (calfCtrl) calfCtrl->setEnabled(false);
                    if (thighCtrl) thighCtrl->setEnabled(false);
                    return;
                }

                osg::Matrixf worldMat = osg::computeLocalToWorld(nodepaths[0]) * it->second->getMatrix();
                osg::Vec3f footWorld = worldMat.getTrans();

                // Sample ground slightly in front of the ankle (towards the ball of the foot / toe)
                // so walking up inclines, stairs, and vertical obstacles anticipates ground slope.
                osg::Vec3f samplePos = footWorld + actorForward * 10.0f;
                osg::Vec3f from = samplePos + osg::Vec3f(0.0f, 0.0f, 80.0f);
                osg::Vec3f to = samplePos - osg::Vec3f(0.0f, 0.0f, 100.0f);

                MWPhysics::RayCastingResult hit = world->getRayCasting()->castRay(
                    from, to, { mAnimation.mPtr }, {}, MWPhysics::Layers::WORLD | MWPhysics::Layers::HEIGHTMAP | MWPhysics::Layers::DOOR);

                if (hit.mHit)
                {
                    // Delta Z relative to character base ground height (so flat terrain yields deltaZ ≈ 0)
                    float deltaZ = hit.mHitPos.z() - actorGroundZ;

                    osg::Vec3f worldNormal = hit.mHitNormal;
                    if (worldNormal.length2() > 1e-4f)
                        worldNormal.normalize();
                    else
                        worldNormal = osg::Vec3f(0.0f, 0.0f, 1.0f);

                    // Transform ground normal into actor local coordinate space
                    osg::Vec3f localNormal = actorAttitudeInv * worldNormal;
                    if (localNormal.length2() > 1e-4f)
                        localNormal.normalize();
                    else
                        localNormal = osg::Vec3f(0.0f, 0.0f, 1.0f);

                    // Local pitch (tilt toe up/down around actor X axis)
                    // Local roll (tilt foot laterally around actor Y axis)
                    float pitch = std::atan2(-localNormal.y(), localNormal.z());
                    float roll = std::atan2(-localNormal.x(), localNormal.z());

                    // Clamp to natural human ankle articulation range
                    pitch = std::clamp(pitch, -0.785f, 0.785f); // ~45 deg
                    roll = std::clamp(roll, -0.523f, 0.523f);   // ~30 deg

                    osg::Quat footRot = osg::Quat(pitch, osg::Vec3f(1.0f, 0.0f, 0.0f))
                        * osg::Quat(roll, osg::Vec3f(0.0f, 1.0f, 0.0f));

                    const bool hasSlope = (std::abs(pitch) > 0.01f || std::abs(roll) > 0.01f);
                    const bool hasDeltaZ = (std::abs(deltaZ) > 0.5f && std::abs(deltaZ) < 60.0f);

                    if (isPlayer)
                    {
                        static int sPlayerLog = 0;
                        if (++sPlayerLog % 60 == 1)
                        {
                            Log(Debug::Warning) << "PLAYER IK: " << footBone
                                                << " hit=" << hit.mHit
                                                << " deltaZ=" << deltaZ
                                                << " pitch=" << pitch
                                                << " roll=" << roll
                                                << " footWorldZ=" << footWorld.z()
                                                << " actorGroundZ=" << actorGroundZ;
                        }
                    }

                    if (hasSlope || hasDeltaZ)
                    {
                        footCtrl->setEnabled(true);
                        footCtrl->setRotate(footRot);
                        if (hasDeltaZ)
                        {
                            float clampedDeltaZ = std::clamp(deltaZ, -25.0f, 35.0f);
                            footCtrl->setOffset(osg::Vec3f(0.0f, 0.0f, clampedDeltaZ));
                        }
                        else
                        {
                            footCtrl->setOffset(osg::Vec3f(0.0f, 0.0f, 0.0f));
                        }
                    }
                    else
                    {
                        footCtrl->setEnabled(false);
                    }

                    // Procedural knee bend (flex) and hip pitch adjustment
                    if (hasDeltaZ)
                    {
                        float clampedDeltaZ = std::clamp(deltaZ, -25.0f, 35.0f);
                        float thighAngle = std::clamp(clampedDeltaZ * 0.015f, -0.45f, 0.45f);
                        float calfAngle = -2.0f * thighAngle;

                        if (thighCtrl)
                        {
                            thighCtrl->setEnabled(true);
                            thighCtrl->setRotate(osg::Quat(thighAngle, osg::Vec3f(1.0f, 0.0f, 0.0f)));
                        }
                        if (calfCtrl)
                        {
                            calfCtrl->setEnabled(true);
                            calfCtrl->setRotate(osg::Quat(calfAngle, osg::Vec3f(1.0f, 0.0f, 0.0f)));
                        }
                    }
                    else
                    {
                        if (thighCtrl) thighCtrl->setEnabled(false);
                        if (calfCtrl) calfCtrl->setEnabled(false);
                    }
                    return;
                }

                footCtrl->setEnabled(false);
                if (calfCtrl) calfCtrl->setEnabled(false);
                if (thighCtrl) thighCtrl->setEnabled(false);
            };

            plantLeg("bip01 l foot", mLeftFootController, mLeftCalfController, mLeftThighController);
            plantLeg("bip01 r foot", mRightFootController, mRightCalfController, mRightThighController);
        }
        else
        {
            if (mLeftThighController) mLeftThighController->setEnabled(false);
            if (mLeftCalfController) mLeftCalfController->setEnabled(false);
            if (mLeftFootController) mLeftFootController->setEnabled(false);
            if (mRightThighController) mRightThighController->setEnabled(false);
            if (mRightCalfController) mRightCalfController->setEnabled(false);
            if (mRightFootController) mRightFootController->setEnabled(false);
        }
    }
} // namespace MWRender
