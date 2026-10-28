#include <components/debug/debuglog.hpp>
#include "rotatecontroller.hpp"

#include <osg/MatrixTransform>
#include <osgAnimation/Bone>

namespace MWRender
{

    RotateController::RotateController(osg::Node* relativeTo, bool hasKeyframeCtrl)
        : mEnabled(true)
        , mHasKeyframeCtrl(hasKeyframeCtrl)
        , mBaseSet(false)
        , mBaseRotation(0.f, 0.f, 0.f, 1.f)
        , mBaseTrans(0.f, 0.f, 0.f)
        , mOffset(0.f, 0.f, 0.f)
        , mRotate(0.f, 0.f, 0.f, 1.f)
        , mRelativeTo(relativeTo)
    {
    }

    void RotateController::setEnabled(bool enabled)
    {
        mEnabled = enabled;
    }

    void RotateController::setRotate(const osg::Quat& rotate)
    {
        mRotate = rotate;
    }

    void RotateController::setOffset(const osg::Vec3f& offset)
    {
        mOffset = offset;
    }

    void RotateController::operator()(osg::MatrixTransform* node, osg::NodeVisitor* nv)
    {
        if (!mBaseSet)
        {
            osg::Matrix initMat = node->getMatrix();
            mBaseRotation = initMat.getRotate();
            mBaseTrans = initMat.getTrans();
            mBaseSet = true;
        }

        if (!mHasKeyframeCtrl)
        {
            osg::Matrix resetMat = node->getMatrix();
            resetMat.setRotate(mBaseRotation);
            resetMat.setTrans(mBaseTrans);
            node->setMatrix(resetMat);
        }

        if (!mEnabled)
        {
            traverse(node, nv);
            return;
        }

        osg::Matrix matrix = node->getMatrix();

        osg::Quat worldOrient;
        osg::NodePathList nodepaths = node->getParentalNodePaths(mRelativeTo);

        if (!nodepaths.empty())
        {
            osg::Matrixf worldMat = osg::computeLocalToWorld(nodepaths[0]);
            worldOrient = worldMat.getRotate();
        }

        osg::Quat worldOrientInverse = worldOrient.inverse();

        osg::Quat baseRot = mHasKeyframeCtrl ? matrix.getRotate() : mBaseRotation;
        osg::Quat orient = worldOrient * mRotate * worldOrientInverse * baseRot;
        matrix.setRotate(orient);
        matrix.setTrans(matrix.getTrans() + worldOrientInverse * mOffset);

        node->setMatrix(matrix);



        // If we are linked to a bone we must call setMatrixInSkeletonSpace
        osgAnimation::Bone* b = dynamic_cast<osgAnimation::Bone*>(node);
        if (b)
        {
            osgAnimation::Bone* parent = b->getBoneParent();
            if (parent)
                matrix *= parent->getMatrixInSkeletonSpace();

            b->setMatrixInSkeletonSpace(matrix);
        }

        traverse(node, nv);
    }
}

