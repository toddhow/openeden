#pragma once

#include <components/nif/data.hpp>
#include <components/nif/nifkey.hpp>

#include <vsg/maths/quat.h>
#include <vsg/maths/vec3.h>

#include <map>
#include <memory>
#include <optional>
#include <string>

namespace Nif
{
    struct NiKeyframeController;
    class FileView;
}

namespace compat_morrowind
{
    /**
     * @brief One bone's keyframe track from a .kf file's Nif records, with no renderer (OpenEden task 0011).
     *
     * Evaluates exactly as OpenMW's NifOsg::KeyframeController::getTransform (components/nifosg/controller.{hpp,cpp}
     * at OpenEden cf3dbd3aa7): constant keys switch at the midpoint, quadratic and TCB vector keys use a cubic Hermite
     * spline, quaternion keys slerp, and X/Y/Z rotation keys combine in the data's axis order. The track shares the key
     * maps of the Nif file, so it may outlive the file.
     */
    class KfTrack
    {
    public:
        struct Transform
        {
            std::optional<vsg::dquat> mRotation;
            std::optional<vsg::vec3> mTranslation;
            std::optional<float> mScale;
        };

        KfTrack() = default;
        explicit KfTrack(const Nif::NiKeyframeController& controller);

        Transform getTransform(float time) const;

        bool hasTranslation() const { return hasKeys(mTranslations); }
        /// True when the track has quaternion or X/Y/Z rotation keys.
        bool hasRotation() const
        {
            return hasKeys(mRotations) || hasKeys(mXRotations) || hasKeys(mYRotations) || hasKeys(mZRotations);
        }

        const Nif::Vector3KeyMap* getTranslationKeys() const { return mTranslations.get(); }
        const Nif::QuaternionKeyMap* getRotationKeys() const { return mRotations.get(); }

    private:
        template <class MapT>
        static bool hasKeys(const std::shared_ptr<const MapT>& keys)
        {
            return keys && !keys->mKeys.empty();
        }

        vsg::dquat getXYZRotation(float time) const;

        std::shared_ptr<const Nif::QuaternionKeyMap> mRotations;
        std::shared_ptr<const Nif::FloatKeyMap> mXRotations;
        std::shared_ptr<const Nif::FloatKeyMap> mYRotations;
        std::shared_ptr<const Nif::FloatKeyMap> mZRotations;
        std::shared_ptr<const Nif::Vector3KeyMap> mTranslations;
        std::shared_ptr<const Nif::FloatKeyMap> mScales;
        Nif::NiKeyframeData::AxisOrder mAxisOrder = Nif::NiKeyframeData::AxisOrder::Order_XYZ;
    };

    /// A .kf file's text keys (lower case, one per line of each key, as OpenMW) and tracks by bone name.
    struct KfAnimation
    {
        std::multimap<float, std::string> mTextKeys;
        std::map<std::string, KfTrack> mTracks;
    };

    /// Reads a parsed .kf file as NifOsg::Loader::loadKf did; problems it logged are logged the same way.
    KfAnimation loadKfAnimation(Nif::FileView kf);
} // namespace compat_morrowind
