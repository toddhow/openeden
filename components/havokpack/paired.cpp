#include "paired.hpp"

#include <cmath>
#include <vector>

namespace HavokPack::Paired
{
    namespace
    {
        constexpr std::size_t MaxPathBytes = 240;

        char lowerChar(char c)
        {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c;
        }

        std::string lowered(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
                c = lowerChar(c);
            return out;
        }

        // The path with forward slashes and lower case, or nothing when it could escape the meshes folder or is empty.
        std::optional<std::string> cleanPath(std::string_view path)
        {
            if (path.empty() || path.size() > MaxPathBytes)
                return std::nullopt;
            std::string out(path);
            for (char& c : out)
                c = c == '\\' ? '/' : lowerChar(c);
            if (out.front() == '/')
                return std::nullopt;
            std::size_t begin = 0;
            while (begin <= out.size())
            {
                std::size_t end = out.find('/', begin);
                if (end == std::string::npos)
                    end = out.size();
                const std::string_view segment(out.data() + begin, end - begin);
                if (segment.empty() || segment == ".." || segment == "." || segment.find(':') != std::string_view::npos)
                    return std::nullopt;
                begin = end + 1;
            }
            return out;
        }

        // Quaternion x y z w as doubles.
        struct Quat
        {
            double x = 0, y = 0, z = 0, w = 1;
        };

        Quat multiply(const Quat& a, const Quat& b)
        {
            return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
        }

        std::array<double, 3> rotate(const Quat& q, const std::array<double, 3>& v)
        {
            // v' = v + 2w (u x v) + 2 u x (u x v), with u the vector part
            const double cx = q.y * v[2] - q.z * v[1];
            const double cy = q.z * v[0] - q.x * v[2];
            const double cz = q.x * v[1] - q.y * v[0];
            const double dx = q.y * cz - q.z * cy;
            const double dy = q.z * cx - q.x * cz;
            const double dz = q.x * cy - q.y * cx;
            return { v[0] + 2.0 * (q.w * cx + dx), v[1] + 2.0 * (q.w * cy + dy), v[2] + 2.0 * (q.w * cz + dz) };
        }

        struct Pose
        {
            std::array<double, 3> t{ 0, 0, 0 };
            Quat q;
        };

        // parent o child
        Pose compose(const Pose& parent, const Pose& child)
        {
            Pose out;
            const auto moved = rotate(parent.q, child.t);
            out.t = { parent.t[0] + moved[0], parent.t[1] + moved[1], parent.t[2] + moved[2] };
            out.q = multiply(parent.q, child.q);
            return out;
        }

        Pose poseOf(const Transform& tr)
        {
            Pose p;
            p.t = { tr.mTranslation[0], tr.mTranslation[1], tr.mTranslation[2] };
            p.q = { tr.mRotation[0], tr.mRotation[1], tr.mRotation[2], tr.mRotation[3] };
            return p;
        }

        bool finite(const Pose& p)
        {
            return std::isfinite(p.t[0]) && std::isfinite(p.t[1]) && std::isfinite(p.t[2]) && std::isfinite(p.q.x)
                && std::isfinite(p.q.y) && std::isfinite(p.q.z) && std::isfinite(p.q.w);
        }

        RootPose rootPose(const Pose& p)
        {
            const double norm = std::sqrt(p.q.x * p.q.x + p.q.y * p.q.y + p.q.z * p.q.z + p.q.w * p.q.w);
            const double n = norm > 0 ? 1.0 / norm : 1.0;
            const double x = p.q.x * n, y = p.q.y * n, z = p.q.z * n, w = p.q.w * n;
            RootPose out;
            out.mX = p.t[0];
            out.mY = p.t[1];
            out.mZ = p.t[2];
            out.mYaw = std::atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z));
            return out;
        }
    }

    TrackRole classifyTrack(std::string_view name)
    {
        TrackRole role;
        const std::string lower = lowered(name);
        if (lower == "pairedroot")
        {
            role.mSide = Side::Shared;
            return role;
        }
        if (lower.size() >= 2 && lower[0] == '2' && lower[1] == '_')
        {
            role.mSide = Side::Second;
            role.mBone = lower.substr(2);
            return role;
        }
        role.mSide = Side::First;
        role.mBone = lower;
        return role;
    }

    std::string clipName(Side side, std::string_view pathUnderMeshes)
    {
        if (side != Side::First && side != Side::Second)
            return {};
        const auto path = cleanPath(pathUnderMeshes);
        if (!path)
            return {};
        return std::string(side == Side::First ? "~1/" : "~2/") + *path;
    }

    std::optional<ClipRef> parseClipName(std::string_view name)
    {
        if (name.size() < 4 || name[0] != '~' || (name[1] != '1' && name[1] != '2') || name[2] != '/')
            return std::nullopt;
        auto path = cleanPath(name.substr(3));
        if (!path)
            return std::nullopt;
        ClipRef ref;
        ref.mSide = name[1] == '1' ? Side::First : Side::Second;
        const bool hasExtension = path->size() > 4 && path->compare(path->size() - 4, 4, ".hkx") == 0;
        ref.mPath = "meshes/" + *path + (hasExtension ? "" : ".hkx");
        return ref;
    }

    BoundClip bindSide(Clip clip, Side side)
    {
        if (clip.mTrackNames.empty() || clip.mTrackNames.size() != clip.mNumTransformTracks)
            throw Error("the clip names none of its tracks, so it is not a paired clip");
        BoundClip out;
        out.mTrackBones.resize(clip.mTrackNames.size());
        for (std::size_t i = 0; i < clip.mTrackNames.size(); ++i)
        {
            TrackRole role = classifyTrack(clip.mTrackNames[i]);
            if (role.mSide == side)
                out.mTrackBones[i] = std::move(role.mBone);
        }
        out.mClip = std::move(clip);
        return out;
    }

    std::optional<Description> describe(const Clip& clip)
    {
        std::optional<std::size_t> root, npc;
        if (clip.mTrackNames.size() != clip.mNumTransformTracks)
            return std::nullopt;
        for (std::size_t i = 0; i < clip.mTrackNames.size(); ++i)
        {
            const TrackRole role = classifyTrack(clip.mTrackNames[i]);
            if (role.mSide != Side::First)
                continue;
            if (role.mBone == "npc root [root]")
                root = i;
            else if (role.mBone == "npc")
                npc = i;
        }
        if (!root)
            return std::nullopt;
        std::vector<Transform> out(clip.mNumTransformTracks);
        auto at = [&](float time) -> std::optional<RootPose> {
            sample(clip, time, out);
            Pose pose = poseOf(out[*root]);
            if (npc)
                pose = compose(poseOf(out[*npc]), pose);
            if (!finite(pose))
                return std::nullopt;
            return rootPose(pose);
        };
        const auto start = at(0.f);
        const auto end = at(clip.mDuration);
        if (!start || !end)
            return std::nullopt;
        Description d;
        d.mDuration = clip.mDuration;
        d.mStart = *start;
        d.mEnd = *end;
        return d;
    }
}
