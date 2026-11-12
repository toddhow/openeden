#include "export.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace HavokPack
{
    namespace
    {
        std::array<float, 4> multiply(const std::array<float, 4>& a, const std::array<float, 4>& b)
        {
            // Hamilton product of x y z w quaternions: a then b is a * b (b applied first to a vector)
            return { a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
                a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
                a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2] };
        }

        std::array<float, 3> rotate(const std::array<float, 4>& q, const std::array<float, 3>& v)
        {
            // v + 2 * cross(q.xyz, cross(q.xyz, v) + w * v)
            const std::array<float, 3> c1 = { q[1] * v[2] - q[2] * v[1] + q[3] * v[0],
                q[2] * v[0] - q[0] * v[2] + q[3] * v[1], q[0] * v[1] - q[1] * v[0] + q[3] * v[2] };
            return { v[0] + 2 * (q[1] * c1[2] - q[2] * c1[1]), v[1] + 2 * (q[2] * c1[0] - q[0] * c1[2]),
                v[2] + 2 * (q[0] * c1[1] - q[1] * c1[0]) };
        }

        std::string number(float v)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%.9g", static_cast<double>(v));
            return buffer;
        }

        std::string quote(const std::string& s)
        {
            std::string out = "\"";
            for (const char ch : s)
            {
                switch (ch)
                {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (static_cast<unsigned char>(ch) < 0x20)
                        {
                            char buffer[8];
                            std::snprintf(buffer, sizeof buffer, "\\u%04x", static_cast<unsigned>(ch));
                            out += buffer;
                        }
                        else
                            out += ch;
                }
            }
            return out + "\"";
        }

        std::string base64(const std::vector<std::uint8_t>& bytes)
        {
            static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out;
            out.reserve((bytes.size() + 2) / 3 * 4);
            for (std::size_t i = 0; i < bytes.size(); i += 3)
            {
                const std::uint32_t n = (static_cast<std::uint32_t>(bytes[i]) << 16)
                    | (i + 1 < bytes.size() ? static_cast<std::uint32_t>(bytes[i + 1]) << 8 : 0)
                    | (i + 2 < bytes.size() ? bytes[i + 2] : 0);
                out += alphabet[(n >> 18) & 63];
                out += alphabet[(n >> 12) & 63];
                out += i + 1 < bytes.size() ? alphabet[(n >> 6) & 63] : '=';
                out += i + 2 < bytes.size() ? alphabet[n & 63] : '=';
            }
            return out;
        }

        void appendFloats(std::vector<std::uint8_t>& buffer, const float* v, std::size_t count)
        {
            const auto* b = reinterpret_cast<const std::uint8_t*>(v);
            buffer.insert(buffer.end(), b, b + 4 * count);
        }
    }

    std::vector<Transform> toWorld(const Skeleton& skeleton, std::span<const Transform> local)
    {
        if (local.size() != skeleton.mBoneNames.size())
            throw Error("toWorld: " + std::to_string(local.size()) + " transforms for "
                + std::to_string(skeleton.mBoneNames.size()) + " bones (offset 0)");
        std::vector<Transform> world(local.size());
        for (std::size_t i = 0; i < local.size(); ++i)
        {
            const std::int16_t p = skeleton.mParents[i];
            if (p < 0)
            {
                world[i] = local[i];
                continue;
            }
            const Transform& parent = world[p];
            Transform& w = world[i];
            std::array<float, 3> scaled;
            for (int k = 0; k < 3; ++k)
                scaled[k] = local[i].mTranslation[k] * parent.mScale[k];
            const auto r = rotate(parent.mRotation, scaled);
            for (int k = 0; k < 3; ++k)
            {
                w.mTranslation[k] = parent.mTranslation[k] + r[k];
                w.mScale[k] = parent.mScale[k] * local[i].mScale[k];
            }
            w.mRotation = multiply(parent.mRotation, local[i].mRotation);
        }
        return world;
    }

    std::vector<Transform> poseAt(const Skeleton& skeleton, const Clip& clip, float time)
    {
        std::vector<Transform> tracks(clip.mNumTransformTracks);
        sample(clip, time, tracks);
        std::vector<Transform> pose = skeleton.mReferencePose;
        for (std::uint32_t t = 0; t < clip.mNumTransformTracks; ++t)
        {
            const std::uint32_t bone = clip.boneOfTrack(t);
            if (bone >= pose.size())
                throw Error("track " + std::to_string(t) + " drives bone " + std::to_string(bone)
                    + ", but the skeleton has " + std::to_string(pose.size()) + " (offset 0)");
            pose[bone] = tracks[t];
        }
        return pose;
    }

    std::string writeGltf(const Skeleton& skeleton, const Clip& clip, const std::string& name)
    {
        const std::size_t bones = skeleton.mBoneNames.size();
        for (std::uint32_t t = 0; t < clip.mNumTransformTracks; ++t)
            if (clip.boneOfTrack(t) >= bones)
                throw Error("track " + std::to_string(t) + " drives bone " + std::to_string(clip.boneOfTrack(t))
                    + ", but the skeleton has " + std::to_string(bones) + " (offset 0)");

        const std::uint32_t frames = clip.mNumFrames;
        std::vector<std::uint8_t> buffer;
        struct View
        {
            std::size_t mOffset, mBytes, mCount;
            const char* mType;
            std::string mMin, mMax;
        };
        std::vector<View> views;

        // times
        {
            const std::size_t start = buffer.size();
            for (std::uint32_t f = 0; f < frames; ++f)
            {
                const float t = static_cast<float>(f) * clip.mFrameDuration;
                appendFloats(buffer, &t, 1);
            }
            const float last = static_cast<float>(frames - 1) * clip.mFrameDuration;
            views.push_back({ start, buffer.size() - start, frames, "SCALAR", "[0]", "[" + number(last) + "]" });
        }

        // sampled tracks
        std::vector<std::vector<Transform>> sampled(frames, std::vector<Transform>(clip.mNumTransformTracks));
        for (std::uint32_t f = 0; f < frames; ++f)
            sample(clip, static_cast<float>(f) * clip.mFrameDuration, sampled[f]);

        std::string channels, samplers;
        std::size_t samplerCount = 0;
        auto addChannel = [&](std::uint32_t bone, const char* path, const char* type, std::size_t components,
                              const std::vector<float>& values) {
            const std::size_t start = buffer.size();
            appendFloats(buffer, values.data(), values.size());
            views.push_back({ start, buffer.size() - start, frames, type, {}, {} });
            const std::size_t accessor = views.size() - 1;
            (void)components;
            if (samplerCount > 0)
            {
                samplers += ",";
                channels += ",";
            }
            samplers += "{\"input\":0,\"interpolation\":\"LINEAR\",\"output\":" + std::to_string(accessor) + "}";
            channels += "{\"sampler\":" + std::to_string(samplerCount) + ",\"target\":{\"node\":" + std::to_string(bone)
                + ",\"path\":\"" + path + "\"}}";
            ++samplerCount;
        };
        for (std::uint32_t t = 0; t < clip.mNumTransformTracks; ++t)
        {
            std::vector<float> translation, rotation, scale;
            for (std::uint32_t f = 0; f < frames; ++f)
            {
                const Transform& x = sampled[f][t];
                translation.insert(translation.end(), x.mTranslation.begin(), x.mTranslation.end());
                rotation.insert(rotation.end(), x.mRotation.begin(), x.mRotation.end());
                scale.insert(scale.end(), x.mScale.begin(), x.mScale.end());
            }
            const std::uint32_t bone = clip.boneOfTrack(t);
            addChannel(bone, "translation", "VEC3", 3, translation);
            addChannel(bone, "rotation", "VEC4", 4, rotation);
            addChannel(bone, "scale", "VEC3", 3, scale);
        }

        std::string nodes;
        std::string roots;
        for (std::size_t i = 0; i < bones; ++i)
        {
            const Transform& r = skeleton.mReferencePose[i];
            std::string children;
            for (std::size_t c = 0; c < bones; ++c)
                if (skeleton.mParents[c] == static_cast<std::int16_t>(i))
                    children += (children.empty() ? "" : ",") + std::to_string(c);
            if (i > 0)
                nodes += ",";
            nodes += "{\"name\":" + quote(skeleton.mBoneNames[i]);
            if (!children.empty())
                nodes += ",\"children\":[" + children + "]";
            nodes += ",\"translation\":[" + number(r.mTranslation[0]) + "," + number(r.mTranslation[1]) + ","
                + number(r.mTranslation[2]) + "],\"rotation\":[" + number(r.mRotation[0]) + "," + number(r.mRotation[1])
                + "," + number(r.mRotation[2]) + "," + number(r.mRotation[3]) + "],\"scale\":[" + number(r.mScale[0])
                + "," + number(r.mScale[1]) + "," + number(r.mScale[2]) + "]}";
            if (skeleton.mParents[i] < 0)
                roots += (roots.empty() ? "" : ",") + std::to_string(i);
        }

        std::string accessors, bufferViews;
        for (std::size_t i = 0; i < views.size(); ++i)
        {
            const View& v = views[i];
            if (i > 0)
            {
                accessors += ",";
                bufferViews += ",";
            }
            bufferViews += "{\"buffer\":0,\"byteOffset\":" + std::to_string(v.mOffset) + ",\"byteLength\":"
                + std::to_string(v.mBytes) + "}";
            accessors += "{\"bufferView\":" + std::to_string(i) + ",\"componentType\":5126,\"count\":"
                + std::to_string(v.mCount) + ",\"type\":\"" + v.mType + "\"";
            if (!v.mMin.empty())
                accessors += ",\"min\":" + v.mMin + ",\"max\":" + v.mMax;
            accessors += "}";
        }

        std::string out = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"OpenEden hkxconvert\",\"extras\":{\"units\":"
                          "\"as stored in the .hkx (Havok units)\",\"axes\":\"as stored (not converted to Y up)\"}},";
        out += "\"scene\":0,\"scenes\":[{\"nodes\":[" + roots + "]}],\"nodes\":[" + nodes + "],";
        out += "\"animations\":[{\"name\":" + quote(name) + ",\"channels\":[" + channels + "],\"samplers\":[" + samplers
            + "]}],";
        out += "\"accessors\":[" + accessors + "],\"bufferViews\":[" + bufferViews + "],";
        out += "\"buffers\":[{\"byteLength\":" + std::to_string(buffer.size())
            + ",\"uri\":\"data:application/octet-stream;base64," + base64(buffer) + "\"}]}";
        return out;
    }
}
