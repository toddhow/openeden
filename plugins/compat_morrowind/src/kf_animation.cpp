#include "compat_morrowind/kf_animation.hpp"

#include <components/debug/debuglog.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/nif/controller.hpp>
#include <components/nif/extra.hpp>
#include <components/nif/niffile.hpp>

#include <algorithm>
#include <vector>

namespace compat_morrowind
{
    namespace
    {
        // The interpolation of NifOsg::ValueInterpolator (components/nifosg/controller.hpp at cf3dbd3aa7).
        template <class ValueType>
        ValueType interpolate(
            const Nif::KeyT<ValueType>& a, const Nif::KeyT<ValueType>& b, float fraction, unsigned int type)
        {
            switch (type)
            {
                case Nif::InterpolationType_Constant:
                    return fraction > 0.5f ? b.mValue : a.mValue;
                case Nif::InterpolationType_Quadratic:
                case Nif::InterpolationType_TCB:
                {
                    // Cubic Hermite spline.
                    const float t = fraction;
                    const float t2 = t * t;
                    const float t3 = t2 * t;
                    const float b1 = 2.f * t3 - 3.f * t2 + 1;
                    const float b2 = -2.f * t3 + 3.f * t2;
                    const float b3 = t3 - 2.f * t2 + t;
                    const float b4 = t3 - t2;
                    return a.mValue * b1 + b.mValue * b2 + a.mOutTan * b3 + b.mInTan * b4;
                }
                default:
                    return a.mValue + ((b.mValue - a.mValue) * fraction);
            }
        }

        vsg::dquat interpolate(
            const Nif::KeyT<vsg::dquat>& a, const Nif::KeyT<vsg::dquat>& b, float fraction, unsigned int type)
        {
            switch (type)
            {
                case Nif::InterpolationType_Constant:
                    return fraction > 0.5f ? b.mValue : a.mValue;
                default:
                {
                    // Quadratic and TBC quaternion keys are slerped too, as in OpenMW.
                    return Misc::VsgMath::slerp(fraction, a.mValue, b.mValue);
                }
            }
        }

        // NifOsg::ValueInterpolator::interpKey without its key cache, which only speeds up the search.
        template <class MapT>
        typename MapT::ValueType interpKey(const MapT& map, float time)
        {
            const auto& keys = map.mKeys;
            if (time <= keys.front().first)
                return keys.front().second.mValue;

            const auto high = std::lower_bound(keys.begin(), keys.end(), time,
                [](const typename MapT::MapType::value_type& key, float t) { return key.first < t; });
            if (high == keys.end())
                return keys.back().second.mValue;

            const auto low = std::prev(high);
            if (high->first == low->first)
                return low->second.mValue;
            const float a = (time - low->first) / (high->first - low->first);
            return interpolate(low->second, high->second, a, map.mInterpolationType);
        }

        void extractTextKeys(const Nif::NiTextKeyExtraData& textKeys, std::multimap<float, std::string>& target)
        {
            for (const Nif::NiTextKeyExtraData::TextKey& key : textKeys.mList)
            {
                std::vector<std::string> results;
                Misc::StringUtils::split(key.mText, results, "\r\n");
                for (std::string& result : results)
                {
                    Misc::StringUtils::trim(result);
                    Misc::StringUtils::lowerCaseInPlace(result);
                    if (!result.empty())
                        target.emplace(key.mTime, std::move(result));
                }
            }
        }
    }

    KfTrack::KfTrack(const Nif::NiKeyframeController& controller)
    {
        if (!controller.mInterpolator.empty())
        {
            if (controller.mInterpolator->mRecordType != Nif::RC_NiTransformInterpolator)
                return;
            const auto* interpolator
                = static_cast<const Nif::NiTransformInterpolator*>(controller.mInterpolator.getPtr());
            // Default values only matter where a track has no keys, and getTransform then reports nothing.
            if (interpolator->mData.empty())
                return;
            const Nif::NiKeyframeData& data = *interpolator->mData.getPtr();
            mRotations = data.mRotations;
            mXRotations = data.mXRotations;
            mYRotations = data.mYRotations;
            mZRotations = data.mZRotations;
            mTranslations = data.mTranslations;
            mScales = data.mScales;
            mAxisOrder = data.mAxisOrder;
        }
        else if (!controller.mData.empty())
        {
            const Nif::NiKeyframeData& data = *controller.mData.getPtr();
            mRotations = data.mRotations;
            mXRotations = data.mXRotations;
            mYRotations = data.mYRotations;
            mZRotations = data.mZRotations;
            mTranslations = data.mTranslations;
            mScales = data.mScales;
            mAxisOrder = data.mAxisOrder;
        }
    }

    osg::Quat KfTrack::getXYZRotation(float time) const
    {
        const float xrot = hasKeys(mXRotations) ? interpKey(*mXRotations, time) : 0.f;
        const float yrot = hasKeys(mYRotations) ? interpKey(*mYRotations, time) : 0.f;
        const float zrot = hasKeys(mZRotations) ? interpKey(*mZRotations, time) : 0.f;
        const osg::Quat xr(xrot, osg::X_AXIS);
        const osg::Quat yr(yrot, osg::Y_AXIS);
        const osg::Quat zr(zrot, osg::Z_AXIS);
        using AxisOrder = Nif::NiKeyframeData::AxisOrder;
        switch (mAxisOrder)
        {
            case AxisOrder::Order_XYZ:
                return xr * yr * zr;
            case AxisOrder::Order_XZY:
                return xr * zr * yr;
            case AxisOrder::Order_YZX:
                return yr * zr * xr;
            case AxisOrder::Order_YXZ:
                return yr * xr * zr;
            case AxisOrder::Order_ZXY:
                return zr * xr * yr;
            case AxisOrder::Order_ZYX:
                return zr * yr * xr;
            case AxisOrder::Order_XYX:
                return xr * yr * xr;
            case AxisOrder::Order_YZY:
                return yr * zr * yr;
            case AxisOrder::Order_ZXZ:
                return zr * xr * zr;
        }
        return xr * yr * zr;
    }

    KfTrack::Transform KfTrack::getTransform(float time) const
    {
        Transform out;
        if (hasKeys(mRotations))
            out.mRotation = interpKey(*mRotations, time);
        else if (hasKeys(mXRotations) || hasKeys(mYRotations) || hasKeys(mZRotations))
            out.mRotation = getXYZRotation(time);
        if (hasKeys(mTranslations))
            out.mTranslation = interpKey(*mTranslations, time);
        if (hasKeys(mScales))
            out.mScale = interpKey(*mScales, time);
        return out;
    }

    KfAnimation loadKfAnimation(Nif::FileView kf)
    {
        KfAnimation result;

        const Nif::NiSequenceStreamHelper* seq = nullptr;
        for (size_t i = 0; i < kf.numRoots(); ++i)
        {
            const Nif::Record* root = kf.getRoot(i);
            if (root && root->mRecordType == Nif::RC_NiSequenceStreamHelper)
            {
                seq = static_cast<const Nif::NiSequenceStreamHelper*>(root);
                break;
            }
        }
        if (!seq)
        {
            Log(Debug::Warning) << "NIFFile Warning: Found no NiSequenceStreamHelper root record. File: "
                                << kf.getFilename();
            return result;
        }

        Nif::ExtraList extraList = seq->getExtraList();
        if (extraList.empty() || extraList[0].empty())
        {
            Log(Debug::Warning) << "NIFFile Warning: NiSequenceStreamHelper has no text keys. File: "
                                << kf.getFilename();
            return result;
        }
        if (extraList[0]->mRecordType != Nif::RC_NiTextKeyExtraData)
        {
            Log(Debug::Warning) << "NIFFile Warning: First extra data was not a NiTextKeyExtraData, but a "
                                << std::string_view(extraList[0]->mRecordName) << ". File: " << kf.getFilename();
            return result;
        }
        extractTextKeys(*static_cast<const Nif::NiTextKeyExtraData*>(extraList[0].getPtr()), result.mTextKeys);

        Nif::NiTimeControllerPtr ctrl = seq->mController;
        for (size_t i = 1; i < extraList.size() && !ctrl.empty(); i++, (ctrl = ctrl->mNext))
        {
            Nif::ExtraPtr extra = extraList[i];
            if (extra.empty())
                continue;
            if (extra->mRecordType != Nif::RC_NiStringExtraData || ctrl->mRecordType != Nif::RC_NiKeyframeController)
            {
                Log(Debug::Warning) << "NIFFile Warning: Unexpected extra data " << extra->mRecordName
                                    << " with controller " << ctrl->mRecordName << ". File: " << kf.getFilename();
                continue;
            }

            const auto* name = static_cast<const Nif::NiStringExtraData*>(extra.getPtr());
            const auto* key = static_cast<const Nif::NiKeyframeController*>(ctrl.getPtr());
            if (key->mData.empty() && key->mInterpolator.empty())
                continue;
            if (!key->mInterpolator.empty() && key->mInterpolator->mRecordType != Nif::RC_NiTransformInterpolator)
            {
                Log(Debug::Error) << "Unsupported interpolator type for NiKeyframeController " << key->mRecordIndex
                                  << " in " << kf.getFilename();
                continue;
            }
            if (!result.mTracks.emplace(name->mData, KfTrack(*key)).second)
                Log(Debug::Verbose) << "Controller " << name->mData << " present more than once in " << kf.getFilename()
                                    << ", ignoring later version";
        }
        return result;
    }
} // namespace compat_morrowind
