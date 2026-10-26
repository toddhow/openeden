#pragma once

#ifndef OPENMW_MWRENDER_ACTORIKCONTROLLER_H
#define OPENMW_MWRENDER_ACTORIKCONTROLLER_H

#include <osg/Quat>
#include <osg/Vec3f>
#include <osg/ref_ptr>

namespace osg
{
    class MatrixTransform;
}

namespace MWRender
{
    class Animation;
    class RotateController;

    /**
     * @brief Actor Inverse Kinematics controller for OpenEden characters.
     * Uses CreativeMotion's opas::ik::solveTwoBone for procedural foot planting
     * and knee articulation on uneven terrain, ramps, and slopes.
     *
     * Invariant: When footPlanting, lookAt, and smootherTurns are false (default),
     * update() early-exits with zero raycasts and zero scene graph modifications,
     * maintaining 100% vanilla Morrowind equivalence.
     */
    class ActorIkController
    {
    public:
        explicit ActorIkController(Animation& animation);
        ~ActorIkController();

        ActorIkController(const ActorIkController&) = delete;
        ActorIkController& operator=(const ActorIkController&) = delete;

        void initControllers();
        void reset();
        void update(float duration);

        [[nodiscard]] bool isFootPlantingActive() const noexcept { return mFootPlantingActive; }
        [[nodiscard]] bool isLookAtActive() const noexcept { return mLookAtActive; }
        [[nodiscard]] bool isSmootherTurnsActive() const noexcept { return mSmootherTurnsActive; }

    private:
        Animation& mAnimation;

        // Controllers for two-bone leg chains
        osg::ref_ptr<RotateController> mLeftThighController;
        osg::ref_ptr<RotateController> mLeftCalfController;
        osg::ref_ptr<RotateController> mLeftFootController;

        osg::ref_ptr<RotateController> mRightThighController;
        osg::ref_ptr<RotateController> mRightCalfController;
        osg::ref_ptr<RotateController> mRightFootController;

        bool mInitialized = false;
        bool mFootPlantingActive = false;
        bool mLookAtActive = false;
        bool mSmootherTurnsActive = false;

        // Smoothed state for CreativeMotion foot IK
        osg::Vec3f mLeftOffset{ 0.f, 0.f, 0.f };
        osg::Vec3f mRightOffset{ 0.f, 0.f, 0.f };

        osg::Quat mLeftFootRot{ 0.f, 0.f, 0.f, 1.f };
        osg::Quat mLeftCalfRot{ 0.f, 0.f, 0.f, 1.f };
        osg::Quat mLeftThighRot{ 0.f, 0.f, 0.f, 1.f };

        osg::Quat mRightFootRot{ 0.f, 0.f, 0.f, 1.f };
        osg::Quat mRightCalfRot{ 0.f, 0.f, 0.f, 1.f };
        osg::Quat mRightThighRot{ 0.f, 0.f, 0.f, 1.f };
    };
} // namespace MWRender

#endif
