#include "sequence.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace NifScene::Sequences
{
    namespace
    {
        // The NIF writes "no value" for a channel as the most negative float; anything beyond this is not a pose.
        constexpr float sNoValueLimit = 1.0e30f;

        bool usable(float value)
        {
            return std::isfinite(value) && std::fabs(value) < sNoValueLimit;
        }

        bool usable(const Vec3& v)
        {
            return usable(v[0]) && usable(v[1]) && usable(v[2]);
        }

        bool usable(const Vec4& v)
        {
            return usable(v[0]) && usable(v[1]) && usable(v[2]) && usable(v[3]);
        }

        // The cubic Hermite blend OpenMW's keyframe code uses (components/animation/interpolate.hpp): tangents are
        // taken as stored, not scaled by the key distance.
        float hermite(float a, float b, float outTanA, float inTanB, float t)
        {
            const float t2 = t * t;
            const float t3 = t2 * t;
            return a * (2.f * t3 - 3.f * t2 + 1.f) + b * (-2.f * t3 + 3.f * t2) + outTanA * (t3 - 2.f * t2 + t)
                + inTanB * (t3 - t2);
        }

        // Which two keys bracket `time`, and the fraction between them. `keys` is non-empty.
        template <class Keys>
        void bracket(const Keys& keys, float time, std::size_t& low, std::size_t& high, float& fraction)
        {
            low = high = 0;
            fraction = 0.f;
            if (time <= keys.front().mTime)
                return;
            if (time >= keys.back().mTime)
            {
                low = high = keys.size() - 1;
                return;
            }
            std::size_t i = 1;
            while (i < keys.size() && keys[i].mTime < time)
                ++i;
            low = i - 1;
            high = i;
            const float span = keys[high].mTime - keys[low].mTime;
            fraction = span > 0.f ? (time - keys[low].mTime) / span : 0.f;
        }

        bool sampleVec3(const std::vector<KeyVec3>& keys, std::uint32_t interpolation, float time, Vec3& out)
        {
            // Keys with a value that is not a number are not used.
            if (keys.empty())
                return false;
            std::size_t low, high;
            float t;
            bracket(keys, time, low, high, t);
            const KeyVec3& a = keys[low];
            const KeyVec3& b = keys[high];
            if (!usable(a.mValue) || !usable(b.mValue))
                return usable(a.mValue) ? (out = a.mValue, true) : (usable(b.mValue) ? (out = b.mValue, true) : false);
            if (low == high || interpolation == 5)
            {
                out = a.mValue;
                return true;
            }
            for (int i = 0; i < 3; ++i)
            {
                if (interpolation == 2 || interpolation == 3)
                    out[i] = hermite(a.mValue[i], b.mValue[i], usable(a.mOutTan[i]) ? a.mOutTan[i] : 0.f,
                        usable(b.mInTan[i]) ? b.mInTan[i] : 0.f, t);
                else
                    out[i] = a.mValue[i] + (b.mValue[i] - a.mValue[i]) * t;
            }
            return true;
        }

        bool sampleFloat(const std::vector<KeyFloat>& keys, std::uint32_t interpolation, float time, float& out)
        {
            if (keys.empty())
                return false;
            std::size_t low, high;
            float t;
            bracket(keys, time, low, high, t);
            const KeyFloat& a = keys[low];
            const KeyFloat& b = keys[high];
            if (!usable(a.mValue) || !usable(b.mValue))
                return usable(a.mValue) ? (out = a.mValue, true) : (usable(b.mValue) ? (out = b.mValue, true) : false);
            if (low == high || interpolation == 5)
            {
                out = a.mValue;
                return true;
            }
            if (interpolation == 2 || interpolation == 3)
                out = hermite(a.mValue, b.mValue, usable(a.mOutTan) ? a.mOutTan : 0.f,
                    usable(b.mInTan) ? b.mInTan : 0.f, t);
            else
                out = a.mValue + (b.mValue - a.mValue) * t;
            return true;
        }

        bool sampleQuat(const std::vector<KeyQuat>& keys, std::uint32_t interpolation, float time, Vec4& out)
        {
            if (keys.empty())
                return false;
            std::size_t low, high;
            float t;
            bracket(keys, time, low, high, t);
            const Vec4& a = keys[low].mValue;
            Vec4 b = keys[high].mValue;
            if (!usable(a) || !usable(b))
                return usable(a) ? (out = a, true) : (usable(b) ? (out = b, true) : false);
            if (low == high || interpolation == 5)
            {
                out = a;
                return true;
            }
            // Shortest way round, then a normalised linear blend (the angles between keys are small).
            const float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
            if (dot < 0.f)
            {
                for (float& c : b)
                    c = -c;
            }
            Vec4 q;
            double length = 0.0;
            for (int i = 0; i < 4; ++i)
            {
                q[i] = a[i] + (b[i] - a[i]) * t;
                length += static_cast<double>(q[i]) * q[i];
            }
            if (length <= 1e-12)
            {
                out = a;
                return true;
            }
            const float inverse = static_cast<float>(1.0 / std::sqrt(length));
            for (int i = 0; i < 4; ++i)
                out[i] = q[i] * inverse;
            return true;
        }

        std::array<float, 9> rotationOf(const Vec4& q)
        {
            const double x = q[0], y = q[1], z = q[2], w = q[3];
            const double length = std::sqrt(x * x + y * y + z * z + w * w);
            if (length <= 1e-12)
                return { 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f };
            const double s = 1.0 / length;
            const double qx = x * s, qy = y * s, qz = z * s, qw = w * s;
            return { static_cast<float>(1.0 - 2.0 * (qy * qy + qz * qz)), static_cast<float>(2.0 * (qx * qy - qz * qw)),
                static_cast<float>(2.0 * (qx * qz + qy * qw)), static_cast<float>(2.0 * (qx * qy + qz * qw)),
                static_cast<float>(1.0 - 2.0 * (qx * qx + qz * qz)), static_cast<float>(2.0 * (qy * qz - qx * qw)),
                static_cast<float>(2.0 * (qx * qz - qy * qw)), static_cast<float>(2.0 * (qy * qz + qx * qw)),
                static_cast<float>(1.0 - 2.0 * (qx * qx + qy * qy)) };
        }

        char lowerAscii(char c)
        {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        bool equalNoCase(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (lowerAscii(a[i]) != lowerAscii(b[i]))
                    return false;
            return true;
        }

        const Track* trackOf(const Sequence& sequence, const std::string& nodeName)
        {
            for (const Track& track : sequence.mTracks)
                if (track.mNodeName == nodeName)
                    return &track;
            return nullptr;
        }
    }

    Mat4 identity()
    {
        return { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    }

    Mat4 toMat4(const Transform& transform)
    {
        Mat4 m = identity();
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column)
                m[column * 4 + row] = transform.mScale * transform.mRotation[row * 3 + column];
        m[12] = transform.mTranslation[0];
        m[13] = transform.mTranslation[1];
        m[14] = transform.mTranslation[2];
        return m;
    }

    Mat4 multiply(const Mat4& left, const Mat4& right)
    {
        Mat4 result{};
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
            {
                float sum = 0.f;
                for (int k = 0; k < 4; ++k)
                    sum += left[k * 4 + row] * right[column * 4 + k];
                result[column * 4 + row] = sum;
            }
        return result;
    }

    Mat4 inverseAffine(const Mat4& m)
    {
        // The upper 3x3 is s * R: its inverse is the transpose over s squared.
        const double s2 = static_cast<double>(m[0]) * m[0] + static_cast<double>(m[1]) * m[1]
            + static_cast<double>(m[2]) * m[2];
        if (s2 <= 1e-20)
            return identity();
        const float inverseScale2 = static_cast<float>(1.0 / s2);
        Mat4 result = identity();
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column)
                result[column * 4 + row] = m[row * 4 + column] * inverseScale2;
        for (int row = 0; row < 3; ++row)
            result[12 + row] = -(result[row] * m[12] + result[4 + row] * m[13] + result[8 + row] * m[14]);
        return result;
    }

    std::size_t find(const Scene& scene, std::string_view name)
    {
        for (std::size_t i = 0; i < scene.mSequences.size(); ++i)
            if (equalNoCase(scene.mSequences[i].mName, name))
                return i;
        return sNoSequence;
    }

    float duration(const Sequence& sequence)
    {
        if (!usable(sequence.mStart) || !usable(sequence.mStop) || !usable(sequence.mFrequency)
            || sequence.mFrequency <= 0.f || sequence.mStop <= sequence.mStart)
            return 0.f;
        return (sequence.mStop - sequence.mStart) / sequence.mFrequency;
    }

    float timeAt(const Sequence& sequence, float elapsed)
    {
        if (!usable(elapsed) || !usable(sequence.mStart) || !usable(sequence.mStop) || !usable(sequence.mFrequency))
            return usable(sequence.mStart) ? sequence.mStart : 0.f;
        const float time = sequence.mStart + elapsed * sequence.mFrequency;
        return std::clamp(time, std::min(sequence.mStart, sequence.mStop), std::max(sequence.mStart, sequence.mStop));
    }

    float stepProgress(float progress, bool open, float dt, float seconds)
    {
        const float target = open ? 1.f : 0.f;
        const float current = usable(progress) ? std::clamp(progress, 0.f, 1.f) : 0.f;
        if (!usable(dt) || dt <= 0.f)
            return current;
        if (!usable(seconds) || seconds <= 0.f)
            return target;
        const float step = dt / seconds;
        if (current < target)
            return std::min(target, current + step);
        return std::max(target, current - step);
    }

    Pose poseFor(const Scene& scene, std::size_t open, std::size_t close, float progress, bool closing)
    {
        Pose pose;
        if (open >= scene.mSequences.size())
            return pose;
        const float p = usable(progress) ? std::clamp(progress, 0.f, 1.f) : 0.f;
        std::size_t index = open;
        float fraction = p;
        if (closing && close < scene.mSequences.size())
        {
            index = close;
            fraction = 1.f - p;
        }
        const Sequence& sequence = scene.mSequences[index];
        pose.mSequence = index;
        const float start = usable(sequence.mStart) ? sequence.mStart : 0.f;
        const float stop = usable(sequence.mStop) ? sequence.mStop : start;
        pose.mTime = start + (stop - start) * fraction;
        return pose;
    }

    Transform sampleLocal(const Track& track, const Transform& rest, float time)
    {
        Transform result = rest;
        Vec3 translation;
        if (sampleVec3(track.mTranslations, track.mTranslationInterpolation, time, translation))
            result.mTranslation = translation;
        else if (track.mHasDefault && usable(track.mDefaultTranslation))
            result.mTranslation = track.mDefaultTranslation;
        Vec4 rotation;
        if (sampleQuat(track.mRotations, 1, time, rotation))
            result.mRotation = rotationOf(rotation);
        else if (!track.mEuler[0].empty() || !track.mEuler[1].empty() || !track.mEuler[2].empty())
        {
            float angle[3] = { 0.f, 0.f, 0.f };
            for (int axis = 0; axis < 3; ++axis)
                (void)sampleFloat(track.mEuler[axis], track.mEulerInterpolation[axis], time, angle[axis]);
            const float cx = std::cos(angle[0]), sx = std::sin(angle[0]);
            const float cy = std::cos(angle[1]), sy = std::sin(angle[1]);
            const float cz = std::cos(angle[2]), sz = std::sin(angle[2]);
            // R = Rz * Ry * Rx, row-major
            result.mRotation = { cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx, sz * cy,
                sz * sy * sx + cz * cx, sz * sy * cx - cz * sx, -sy, cy * sx, cy * cx };
        }
        else if (track.mHasDefault && usable(track.mDefaultRotation))
            result.mRotation = rotationOf(track.mDefaultRotation);
        float scale;
        if (sampleFloat(track.mScales, track.mScaleInterpolation, time, scale))
            result.mScale = scale;
        else if (track.mHasDefault && usable(track.mDefaultScale))
            result.mScale = track.mDefaultScale;
        return result;
    }

    std::vector<std::uint32_t> animatedNodes(const Scene& scene, const Sequence& sequence)
    {
        std::vector<std::uint32_t> nodes;
        for (const Track& track : sequence.mTracks)
        {
            for (std::uint32_t i = 0; i < scene.mNodes.size(); ++i)
            {
                if (scene.mNodes[i].mName == track.mNodeName)
                {
                    if (std::find(nodes.begin(), nodes.end(), i) == nodes.end())
                        nodes.push_back(i);
                    break;
                }
            }
        }
        return nodes;
    }

    namespace
    {
        // The walk up is bounded by the node count: a parent cycle ends it.
        template <class LocalOf>
        Mat4 worldOf(const Scene& scene, std::uint32_t node, LocalOf&& localOf)
        {
            Mat4 result = identity();
            std::uint32_t at = node;
            for (std::size_t steps = 0; at < scene.mNodes.size() && steps <= scene.mNodes.size(); ++steps)
            {
                result = multiply(localOf(at), result);
                at = scene.mNodes[at].mParent;
            }
            return result;
        }
    }

    Mat4 restWorld(const Scene& scene, std::uint32_t node)
    {
        if (node >= scene.mNodes.size())
            return identity();
        return worldOf(scene, node, [&scene](std::uint32_t at) { return toMat4(scene.mNodes[at].mTransform); });
    }

    Mat4 animatedWorld(const Scene& scene, const Sequence& sequence, std::uint32_t node, float time)
    {
        if (node >= scene.mNodes.size())
            return identity();
        return worldOf(scene, node, [&](std::uint32_t at) {
            const Node& n = scene.mNodes[at];
            const Track* track = trackOf(sequence, n.mName);
            return toMat4(track ? sampleLocal(*track, n.mTransform, time) : n.mTransform);
        });
    }

    Mat4 delta(const Scene& scene, const Sequence& sequence, std::uint32_t node, float time)
    {
        if (node >= scene.mNodes.size())
            return identity();
        return multiply(animatedWorld(scene, sequence, node, time), inverseAffine(restWorld(scene, node)));
    }
}
