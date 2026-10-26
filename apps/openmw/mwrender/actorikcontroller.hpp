#ifndef OPENMW_MWRENDER_ACTORIKCONTROLLER_H
#define OPENMW_MWRENDER_ACTORIKCONTROLLER_H

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
     * @brief Milestone M9: Actor Inverse Kinematics controller for Morrowind characters.
     * Manages procedural foot planting on uneven terrain, gaze tracking (look-at),
     * and rotational turn smoothing behind off-by-default engine settings.
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
    };
} // namespace MWRender

#endif
