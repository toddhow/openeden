#ifndef OPENMW_COMPONENTS_MISC_CONVERT_H
#define OPENMW_COMPONENTS_MISC_CONVERT_H

#include <components/esm/position.hpp>
#include <components/esm3/loadpgrd.hpp>
#include <components/misc/vsgmath.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Math/Float3.h>
#include <Jolt/Math/Mat44.h>
#include <Jolt/Math/Real.h>

#include <vsg/maths/mat4.h>
#include <vsg/maths/quat.h>
#include <vsg/maths/vec2.h>
#include <vsg/maths/vec3.h>

namespace Misc::Convert
{
    inline vsg::vec3 makeOsgVec3f(const float* values)
    {
        return vsg::vec3(values[0], values[1], values[2]);
    }

    inline vsg::vec3 makeOsgVec3f(const ESM::Pathgrid::Point& value)
    {
        return vsg::vec3(static_cast<float>(value.mX), static_cast<float>(value.mY), static_cast<float>(value.mZ));
    }

    template <typename T>
    inline T toJolt(const vsg::vec3& vec)
    {
        return T(vec.x, vec.y, vec.z);
    }

    template <typename T>
    inline T toJolt(const vsg::dvec3& vec)
    {
        return T(vec.x, vec.y, vec.z);
    }

    template <>
    inline JPH::Vec3 toJolt<JPH::Vec3>(const vsg::dvec3& vec)
    {
        return JPH::Vec3(static_cast<float>(vec.x), static_cast<float>(vec.y), static_cast<float>(vec.z));
    }

    template <>
    inline JPH::Float3 toJolt<JPH::Float3>(const vsg::dvec3& vec)
    {
        return JPH::Float3(static_cast<float>(vec.x), static_cast<float>(vec.y), static_cast<float>(vec.z));
    }

    template <typename T>
    inline T toJolt(const JPH::RVec3& vec)
    {
        return T(vec.GetX(), vec.GetY(), vec.GetZ());
    }

    template <>
    inline JPH::Vec3 toJolt<JPH::Vec3>(const JPH::RVec3& vec)
    {
        return JPH::Vec3(
            static_cast<float>(vec.GetX()), static_cast<float>(vec.GetY()), static_cast<float>(vec.GetZ()));
    }

    template <>
    inline JPH::Float3 toJolt<JPH::Float3>(const JPH::RVec3& vec)
    {
        return JPH::Float3(
            static_cast<float>(vec.GetX()), static_cast<float>(vec.GetY()), static_cast<float>(vec.GetZ()));
    }

    template <typename T>
    inline T toJolt(const JPH::Float3& vec)
    {
        return T(vec.x, vec.y, vec.z);
    }

    inline JPH::Quat toJolt(const vsg::dquat& quat)
    {
        return JPH::Quat(static_cast<float>(quat.x), static_cast<float>(quat.y), static_cast<float>(quat.z),
            static_cast<float>(quat.w));
    }

    inline vsg::dmat4 toOsgNoScale(const JPH::Mat44& joltMatrix)
    {
        auto inPosition = joltMatrix.GetTranslation();
        auto subRot = joltMatrix.GetRotation().GetQuaternion();
        vsg::dmat4 mat = VsgMath::toMatrix(vsg::dquat(subRot.GetX(), subRot.GetY(), subRot.GetZ(), subRot.GetW()));
        VsgMath::setTrans(mat, vsg::vec3(inPosition.GetX(), inPosition.GetY(), inPosition.GetZ()));
        return mat;
    }

    inline JPH::RMat44 toJoltNoScale(const vsg::dmat4& mat)
    {
        JPH::Quat quat = toJolt(VsgMath::rotationOf(mat)).Normalized();
        vsg::dvec3 trans = VsgMath::getTrans(mat);
        return JPH::RMat44::sRotationTranslation(quat, toJolt<JPH::RVec3>(trans));
    }

    inline vsg::vec3 toOsg(const JPH::RVec3& vec)
    {
        return vsg::vec3(
            static_cast<float>(vec.GetX()), static_cast<float>(vec.GetY()), static_cast<float>(vec.GetZ()));
    }

#ifdef JPH_DOUBLE_PRECISION
    inline vsg::vec3 toOsg(const JPH::Vec3& vec)
    {
        return vsg::vec3(
            static_cast<float>(vec.GetX()), static_cast<float>(vec.GetY()), static_cast<float>(vec.GetZ()));
    }
#endif

    template <typename T>
    inline T* toPointerFromUserData(uint64_t userData)
    {
        if (userData > 0)
        {
            // Converts from userdata uintptr to class type
            // You should know the userdata points to the correct type!
            T* ptr = reinterpret_cast<T*>(static_cast<uintptr_t>(userData));
            return ptr;
        }
        return nullptr;
    }

    inline vsg::vec3 toOsg(const JPH::Float3& vec)
    {
        return vsg::vec3(vec.x, vec.y, vec.z);
    }

    inline vsg::dquat makeOsgQuat(const float (&rotation)[3])
    {
        return VsgMath::makeRotateFromAxis(rotation[2], vsg::vec3(0, 0, -1))
            * VsgMath::makeRotateFromAxis(rotation[1], vsg::vec3(0, -1, 0))
            * VsgMath::makeRotateFromAxis(rotation[0], vsg::vec3(-1, 0, 0));
    }

    inline vsg::dquat makeOsgQuat(const ESM::Position& position)
    {
        return makeOsgQuat(position.rot);
    }

    inline vsg::dquat makeQuaternion(const float (&rotation)[3])
    {
        return vsg::dquat(0, 0, -1, rotation[2]) * vsg::dquat(0, -1, 0, rotation[1]) * vsg::dquat(-1, 0, 0, rotation[0]);
    }

    inline vsg::dquat makeQuaternion(const ESM::Position& position)
    {
        return makeQuaternion(position.rot);
    }

    inline vsg::dmat4 makeOSGTransform(const ESM::Position& position)
    {
        vsg::dmat4 mat = VsgMath::toMatrix(makeQuaternion(position));
        VsgMath::setTrans(mat, vsg::vec3(position.asVec3()));
        return mat;
    }

    inline vsg::vec2 toOsgXY(const vsg::vec3& value)
    {
        return vsg::vec2(static_cast<float>(value.x), static_cast<float>(value.y));
    }

    inline vsg::vec2 toOsgXY(const JPH::RVec3& value)
    {
        return vsg::vec2(static_cast<float>(value.GetX()), static_cast<float>(value.GetY()));
    }

#ifdef JPH_DOUBLE_PRECISION
    inline vsg::vec2 toOsgXY(const JPH::Vec3& value)
    {
        return vsg::vec2(static_cast<float>(value.GetX()), static_cast<float>(value.GetY()));
    }
#endif
}

#endif
