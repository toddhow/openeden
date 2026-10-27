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

#include <opas/ik/TwoBoneIk.hpp>
#include <opas/math/Quat.hpp>
#include <opas/math/Vec3.hpp>

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
        if (mAnimation.getNodeMap().empty())
            return;

        mInitialized = true;

        mLeftFootController = mAnimation.addRotateController("bip01 l foot");
        mLeftCalfController = mAnimation.addRotateController("bip01 l calf");
        mLeftThighController = mAnimation.addRotateController("bip01 l thigh");

        mRightFootController = mAnimation.addRotateController("bip01 r foot");
        mRightCalfController = mAnimation.addRotateController("bip01 r calf");
        mRightThighController = mAnimation.addRotateController("bip01 r thigh");
    }

    void ActorIkController::reset()
    {
        mInitialized = false;
        mLeftFootController = nullptr;
        mLeftCalfController = nullptr;
        mLeftThighController = nullptr;
        mRightFootController = nullptr;
        mRightCalfController = nullptr;
        mRightThighController = nullptr;
    }

    void ActorIkController::update(float duration)
    {
        if (!mAnimation.mPtr.getClass().isActor())
            return;

        const bool footPlanting = Settings::values().mGame.mFootPlanting.get();
        const bool lookAt = Settings::values().mGame.mLookAt.get();
        const bool smootherTurns = Settings::values().mGame.mSmootherTurns.get();

        mFootPlantingActive = footPlanting;
        mLookAtActive = lookAt;
        mSmootherTurnsActive = smootherTurns;

        if (!footPlanting && !lookAt && !smootherTurns)
        {
            if (mLeftThighController) mLeftThighController->setEnabled(false);
            if (mLeftCalfController) mLeftCalfController->setEnabled(false);
            if (mLeftFootController) mLeftFootController->setEnabled(false);
            if (mRightThighController) mRightThighController->setEnabled(false);
            if (mRightCalfController) mRightCalfController->setEnabled(false);
            if (mRightFootController) mRightFootController->setEnabled(false);
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

            auto* baseNode = mAnimation.mPtr.getRefData().getBaseNode();
            osg::Vec3f actorPos = osg::Vec3f();
            osg::Quat actorAttitude = osg::Quat();
            float actorGroundZ = 0.0f;
            if (baseNode)
            {
                actorPos = baseNode->getPosition();
                actorAttitude = baseNode->getAttitude();
                actorGroundZ = actorPos.z();
            }
            const osg::Quat actorAttitudeInv = actorAttitude.inverse();
            const osg::Vec3f actorForward = actorAttitude * osg::Vec3f(0.0f, 1.0f, 0.0f);

            auto getLocalTrans = [&](osg::MatrixTransform* node) -> osg::Vec3f {
                if (!node || !baseNode)
                    return osg::Vec3f();
                osg::NodePathList nodepaths = node->getParentalNodePaths(baseNode);
                if (nodepaths.empty())
                    return node->getMatrix().getTrans();
                osg::Matrixf localMat = osg::computeLocalToWorld(nodepaths[0]) * node->getMatrix();
                return localMat.getTrans();
            };

            auto plantLeg = [&](std::string_view thighBone,
                                std::string_view calfBone,
                                std::string_view footBone,
                                osg::ref_ptr<RotateController>& footCtrl,
                                osg::ref_ptr<RotateController>& calfCtrl,
                                osg::ref_ptr<RotateController>& thighCtrl,
                                osg::Vec3f& currentOffset,
                                osg::Quat& currentFootRot,
                                osg::Quat& currentCalfRot,
                                osg::Quat& currentThighRot,
                                osg::Vec3f& thighRest,
                                osg::Vec3f& calfRest,
                                osg::Vec3f& footRest) {
                if (!footCtrl || !baseNode)
                    return;

                const auto& nodeMap = mAnimation.getNodeMap();
                auto itFoot = nodeMap.find(footBone);
                auto itCalf = nodeMap.find(calfBone);
                auto itThigh = nodeMap.find(thighBone);

                if (itFoot == nodeMap.end() || !itFoot->second ||
                    itCalf == nodeMap.end() || !itCalf->second ||
                    itThigh == nodeMap.end() || !itThigh->second)
                {
                    footCtrl->setEnabled(false);
                    if (calfCtrl) calfCtrl->setEnabled(false);
                    if (thighCtrl) thighCtrl->setEnabled(false);
                    return;
                }

                if (thighRest.length2() == 0.0f)
                    thighRest = getLocalTrans(itThigh->second);
                if (calfRest.length2() == 0.0f)
                    calfRest = getLocalTrans(itCalf->second);
                if (footRest.length2() == 0.0f)
                    footRest = getLocalTrans(itFoot->second);

                // Sample terrain straight down under foot rest stance position in world
                osg::Vec3f footWorldStance = actorPos + actorAttitude * osg::Vec3f(footRest.x(), footRest.y(), 0.0f);
                osg::Vec3f from(footWorldStance.x(), footWorldStance.y(), actorPos.z() + 60.0f);
                osg::Vec3f to(footWorldStance.x(), footWorldStance.y(), actorPos.z() - 100.0f);

                MWPhysics::RayCastingResult hit = world->getRayCasting()->castRay(
                    from, to, { mAnimation.mPtr }, {}, MWPhysics::Layers::WORLD | MWPhysics::Layers::HEIGHTMAP | MWPhysics::Layers::DOOR);

                osg::Quat targetFootRot(0.0f, 0.0f, 0.0f, 1.0f);
                osg::Quat targetCalfRot(0.0f, 0.0f, 0.0f, 1.0f);
                osg::Quat targetThighRot(0.0f, 0.0f, 0.0f, 1.0f);

                if (hit.mHit)
                {
                    float deltaZ = hit.mHitPos.z() - actorGroundZ;
                    float clampedDeltaZ = std::clamp(deltaZ, -40.0f, 40.0f);

                    opas::ik::TwoBoneChain chain{
                        { thighRest.x(), thighRest.y(), thighRest.z() },
                        { calfRest.x(), calfRest.y(), calfRest.z() },
                        { footRest.x(), footRest.y(), footRest.z() }
                    };

                    opas::math::Vec3 target = chain.end;
                    target.z = footRest.z() + clampedDeltaZ;

                    // Knee bends forward in character space (+Y)
                    opas::math::Vec3 pole{ 0.0, 1.0, 0.0 };
                    opas::ik::HingeLimits limits{ 0.02, 2.8 };

                    opas::ik::TwoBoneResult ik = opas::ik::solveTwoBone(chain, target, pole, limits);

                    // Surface normal alignment for the foot
                    osg::Vec3f worldNormal = hit.mHitNormal;
                    if (worldNormal.length2() > 1e-4f)
                        worldNormal.normalize();
                    else
                        worldNormal = osg::Vec3f(0.0f, 0.0f, 1.0f);

                    osg::Vec3f localNormal = actorAttitudeInv * worldNormal;
                    if (localNormal.length2() > 1e-4f)
                        localNormal.normalize();
                    else
                        localNormal = osg::Vec3f(0.0f, 0.0f, 1.0f);

                    float pitch = std::clamp(std::atan2(-localNormal.y(), localNormal.z()), -0.785f, 0.785f);
                    float roll  = std::clamp(std::atan2(-localNormal.x(), localNormal.z()), -0.523f, 0.523f);

                    osg::Quat groundRot = osg::Quat(pitch, osg::Vec3f(1.0f, 0.0f, 0.0f))
                                        * osg::Quat(roll, osg::Vec3f(0.0f, 1.0f, 0.0f));

                    if (ik.valid)
                    {
                        osg::Quat rootRot(
                            static_cast<float>(ik.rootRotation.x),
                            static_cast<float>(ik.rootRotation.y),
                            static_cast<float>(ik.rootRotation.z),
                            static_cast<float>(ik.rootRotation.w));
                        osg::Quat midRot(
                            static_cast<float>(ik.midRotation.x),
                            static_cast<float>(ik.midRotation.y),
                            static_cast<float>(ik.midRotation.z),
                            static_cast<float>(ik.midRotation.w));

                        // RotateController applies rotations in actor frame (mRelativeTo)
                        targetThighRot = rootRot;
                        targetCalfRot = midRot;
                        targetFootRot = groundRot;
                    }
                    else
                    {
                        targetFootRot = groundRot;
                    }

                    if (isPlayer)
                    {
                        static int sLog = 0;
                        if (++sLog % 60 == 1)
                        {
                            Log(Debug::Info) << "CREATIVEMOTION IK: " << footBone
                                             << " hit=" << hit.mHit
                                             << " deltaZ=" << clampedDeltaZ
                                             << " ikValid=" << ik.valid
                                             << " bendRad=" << ik.bendRad;
                        }
                    }
                }

                // Smooth temporal blending with exponential dampening
                const float blend = 1.0f - std::exp(-14.0f * duration);
                currentFootRot.slerp(blend, currentFootRot, targetFootRot);
                currentCalfRot.slerp(blend, currentCalfRot, targetCalfRot);
                currentThighRot.slerp(blend, currentThighRot, targetThighRot);

                footCtrl->setEnabled(true);
                footCtrl->setOffset(osg::Vec3f(0.0f, 0.0f, 0.0f));
                footCtrl->setRotate(currentFootRot);

                if (calfCtrl)
                {
                    calfCtrl->setEnabled(true);
                    calfCtrl->setOffset(osg::Vec3f(0.0f, 0.0f, 0.0f));
                    calfCtrl->setRotate(currentCalfRot);
                }
                if (thighCtrl)
                {
                    thighCtrl->setEnabled(true);
                    thighCtrl->setOffset(osg::Vec3f(0.0f, 0.0f, 0.0f));
                    thighCtrl->setRotate(currentThighRot);
                }
            };

            plantLeg("bip01 l thigh", "bip01 l calf", "bip01 l foot",
                     mLeftFootController, mLeftCalfController, mLeftThighController,
                     mLeftOffset, mLeftFootRot, mLeftCalfRot, mLeftThighRot,
                     mLeftThighRest, mLeftCalfRest, mLeftFootRest);

            plantLeg("bip01 r thigh", "bip01 r calf", "bip01 r foot",
                     mRightFootController, mRightCalfController, mRightThighController,
                     mRightOffset, mRightFootRot, mRightCalfRot, mRightThighRot,
                     mRightThighRest, mRightCalfRest, mRightFootRest);
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
