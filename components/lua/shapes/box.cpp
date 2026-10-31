#include "box.hpp"

namespace LuaUtil
{
    Box::Box(const vsg::vec3& center, const vsg::vec3& halfSize, const vsg::dquat& rotation)
        : mCenter(center)
        , mHalfSize(halfSize)
        , mRotation(rotation)
    {
    }

    Box::Box(const vsg::dmat4& transform)
    {
        vsg::dquat _;
        Misc::VsgMath::decompose(transform, mCenter, mRotation, mHalfSize, _);
    }

    std::array<vsg::vec3, 8> Box::vertices() const
    {
        return {
            mCenter + vsg::quat(mRotation) * vsg::vec3(-mHalfSize.x, -mHalfSize.y, -mHalfSize.z),
            mCenter + vsg::quat(mRotation) * vsg::vec3(mHalfSize.x, -mHalfSize.y, -mHalfSize.z),
            mCenter + vsg::quat(mRotation) * vsg::vec3(mHalfSize.x, mHalfSize.y, -mHalfSize.z),
            mCenter + vsg::quat(mRotation) * vsg::vec3(-mHalfSize.x, mHalfSize.y, -mHalfSize.z),
            mCenter + vsg::quat(mRotation) * vsg::vec3(-mHalfSize.x, -mHalfSize.y, mHalfSize.z),
            mCenter + vsg::quat(mRotation) * vsg::vec3(mHalfSize.x, -mHalfSize.y, mHalfSize.z),
            mCenter + vsg::quat(mRotation) * vsg::vec3(mHalfSize.x, mHalfSize.y, mHalfSize.z),
            mCenter + vsg::quat(mRotation) * vsg::vec3(-mHalfSize.x, mHalfSize.y, mHalfSize.z),
        };
    }

    vsg::dmat4 Box::asTransform() const
    {
        vsg::dmat4 transform;
        Misc::VsgMath::preMultTranslate(transform, mCenter);
        Misc::VsgMath::preMultRotate(transform, mRotation);
        Misc::VsgMath::preMultScale(transform, mHalfSize);
        return transform;
    }

    bool Box::operator==(const Box& other) const
    {
        return mCenter == other.mCenter && mHalfSize == other.mHalfSize && mRotation == other.mRotation;
    }
}