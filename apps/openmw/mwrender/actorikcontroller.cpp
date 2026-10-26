#include "actorikcontroller.hpp"
#include "animation.hpp"
#include "rotatecontroller.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwphysics/raycasting.hpp"

#include <components/settings/values.hpp>

#include <osg/MatrixTransform>
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
        if (mInitialized)
            return;

        mLeftThighController = mAnimation.addRotateController("bip01 l thigh");
        mLeftCalfController = mAnimation.addRotateController("bip01 l calf");
        mLeftFootController = mAnimation.addRotateController("bip01 l foot");

        mRightThighController = mAnimation.addRotateController("bip01 r thigh");
        mRightCalfController = mAnimation.addRotateController("bip01 r calf");
        mRightFootController = mAnimation.addRotateController("bip01 r foot");

        mInitialized = true;
    }

    void ActorIkController::update(float /*duration*/)
    {
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

        initControllers();

        if (footPlanting && MWBase::Environment::get().getWorld())
        {
            auto plantFoot = [&](std::string_view footBone, osg::ref_ptr<RotateController>& footCtrl) {
                if (!footCtrl)
                    return;

                auto it = mAnimation.getNodeMap().find(footBone);
                if (it == mAnimation.getNodeMap().end() || !it->second)
                    return;

                osg::NodePathList nodepaths = it->second->getParentalNodePaths();
                if (nodepaths.empty())
                    return;

                osg::Vec3f footWorld = osg::computeLocalToWorld(nodepaths[0]).getTrans();
                osg::Vec3f from = footWorld + osg::Vec3f(0.0f, 0.0f, 40.0f);
                osg::Vec3f to = footWorld - osg::Vec3f(0.0f, 0.0f, 80.0f);

                MWPhysics::RayCastingResult hit = MWBase::Environment::get().getWorld()->getRayCasting()->castRay(
                    from, to, { mAnimation.mPtr }, {}, MWPhysics::Layers::WORLD | MWPhysics::Layers::HEIGHTMAP);

                if (hit.mHit)
                {
                    float deltaZ = hit.mHitPos.z() - footWorld.z();
                    if (std::abs(deltaZ) > 0.5f && std::abs(deltaZ) < 60.0f)
                    {
                        footCtrl->setEnabled(true);
                        footCtrl->setOffset(osg::Vec3f(0.0f, 0.0f, deltaZ));

                        osg::Vec3f up(0.0f, 0.0f, 1.0f);
                        osg::Vec3f normal = hit.mHitNormal;
                        if (normal.length2() > 1e-4f)
                        {
                            normal.normalize();
                            osg::Quat alignQuat;
                            alignQuat.makeRotate(up, normal);
                            footCtrl->setRotate(alignQuat);
                        }
                        return;
                    }
                }
                footCtrl->setEnabled(false);
            };

            plantFoot("bip01 l foot", mLeftFootController);
            plantFoot("bip01 r foot", mRightFootController);
        }
        else
        {
            if (mLeftFootController)
                mLeftFootController->setEnabled(false);
            if (mRightFootController)
                mRightFootController->setEnabled(false);
        }
    }
} // namespace MWRender
