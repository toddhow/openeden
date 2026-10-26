#include "actorikcontroller.hpp"
#include "animation.hpp"
#include "rotatecontroller.hpp"

#include <components/settings/values.hpp>

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

        if (footPlanting)
        {
            if (mLeftThighController)
                mLeftThighController->setEnabled(true);
            if (mLeftCalfController)
                mLeftCalfController->setEnabled(true);
            if (mLeftFootController)
                mLeftFootController->setEnabled(true);
            if (mRightThighController)
                mRightThighController->setEnabled(true);
            if (mRightCalfController)
                mRightCalfController->setEnabled(true);
            if (mRightFootController)
                mRightFootController->setEnabled(true);
        }
        else
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
        }
    }
} // namespace MWRender
