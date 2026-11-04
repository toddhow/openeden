#include "skeletonmapping.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Body
{
    namespace
    {
        constexpr double sSingularDeterminant = 1e-14;

        struct Rot3
        {
            // The column-vector rotation matrix R (v' = R v), row-major.
            std::array<double, 9> m{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        };

        Rot3 rotationOf(const std::array<double, 4>& q)
        {
            double x = q[0];
            double y = q[1];
            double z = q[2];
            double w = q[3];
            const double length = std::sqrt(x * x + y * y + z * z + w * w);
            if (!(length > 0) || !std::isfinite(length))
            {
                return {};
            }
            x /= length;
            y /= length;
            z /= length;
            w /= length;
            Rot3 r;
            r.m = { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w),
                1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 2 * (x * z - y * w), 2 * (y * z + x * w),
                1 - 2 * (x * x + y * y) };
            return r;
        }

        // Quaternion (x, y, z, w with w >= 0) of a column-vector rotation matrix.
        std::array<double, 4> quaternionOf(const Rot3& r)
        {
            const auto& m = r.m;
            const double trace = m[0] + m[4] + m[8];
            double x;
            double y;
            double z;
            double w;
            if (trace > 0)
            {
                const double s = std::sqrt(trace + 1.0) * 2;
                w = 0.25 * s;
                x = (m[7] - m[5]) / s;
                y = (m[2] - m[6]) / s;
                z = (m[3] - m[1]) / s;
            }
            else if (m[0] > m[4] && m[0] > m[8])
            {
                const double s = std::sqrt(1.0 + m[0] - m[4] - m[8]) * 2;
                w = (m[7] - m[5]) / s;
                x = 0.25 * s;
                y = (m[1] + m[3]) / s;
                z = (m[2] + m[6]) / s;
            }
            else if (m[4] > m[8])
            {
                const double s = std::sqrt(1.0 + m[4] - m[0] - m[8]) * 2;
                w = (m[2] - m[6]) / s;
                x = (m[1] + m[3]) / s;
                y = 0.25 * s;
                z = (m[5] + m[7]) / s;
            }
            else
            {
                const double s = std::sqrt(1.0 + m[8] - m[0] - m[4]) * 2;
                w = (m[3] - m[1]) / s;
                x = (m[2] + m[6]) / s;
                y = (m[5] + m[7]) / s;
                z = 0.25 * s;
            }
            if (w < 0)
            {
                x = -x;
                y = -y;
                z = -z;
                w = -w;
            }
            return { x, y, z, w };
        }

        // The upper-left 3 x 3 of a row-vector matrix is the transpose of the column-vector rotation.
        Rot3 rotationOfMatrix(const Mat4& matrix)
        {
            Rot3 r;
            for (std::size_t row = 0; row < 3; ++row)
            {
                for (std::size_t col = 0; col < 3; ++col)
                {
                    r.m[row * 3 + col] = matrix.m[col * 4 + row];
                }
            }
            return r;
        }

        double rotationAngle(const Rot3& a, const Rot3& b)
        {
            // The rotation from b to a: R = a * b^T. The angle by atan2 of the skew part and the trace, which is
            // stable for tiny angles (acos of a trace near 3 would give 1e-8 for an exact match).
            std::array<double, 9> r{};
            for (std::size_t i = 0; i < 3; ++i)
            {
                for (std::size_t j = 0; j < 3; ++j)
                {
                    double sum = 0;
                    for (std::size_t k = 0; k < 3; ++k)
                    {
                        sum += a.m[i * 3 + k] * b.m[j * 3 + k];
                    }
                    r[i * 3 + j] = sum;
                }
            }
            const double sx = r[7] - r[5];
            const double sy = r[2] - r[6];
            const double sz = r[3] - r[1];
            const double sine = 0.5 * std::sqrt(sx * sx + sy * sy + sz * sz);
            const double cosine = 0.5 * (r[0] + r[4] + r[8] - 1.0);
            return std::atan2(sine, cosine);
        }

        std::array<double, 3> translationOf(const Mat4& m)
        {
            return { m.m[12], m.m[13], m.m[14] };
        }

        double distance(const std::array<double, 3>& a, const std::array<double, 3>& b)
        {
            return std::sqrt(
                (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
        }

        double length(const std::array<double, 3>& v)
        {
            return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        }

        std::string shown(const std::string& name)
        {
            std::string out = "'";
            for (const char c : name.substr(0, 60))
            {
                const auto u = static_cast<unsigned char>(c);
                out += (u < 0x20 || u == 0x7f) ? '?' : c;
            }
            out += name.size() > 60 ? "...'" : "'";
            return out;
        }

        std::string line(const MapFinding& finding)
        {
            return finding.mId + " " + finding.mPointer + ": " + finding.mMessage;
        }
    }

    // ---- Matrices ------------------------------------------------------------------------------------------------

    Mat4 multiply(const Mat4& a, const Mat4& b)
    {
        Mat4 result;
        for (std::size_t i = 0; i < 4; ++i)
        {
            for (std::size_t j = 0; j < 4; ++j)
            {
                double sum = 0;
                for (std::size_t k = 0; k < 4; ++k)
                {
                    sum += a.m[i * 4 + k] * b.m[k * 4 + j];
                }
                result.m[i * 4 + j] = sum;
            }
        }
        return result;
    }

    std::optional<Mat4> inverse(const Mat4& matrix)
    {
        for (const double v : matrix.m)
        {
            if (!std::isfinite(v))
            {
                return std::nullopt;
            }
        }
        if (matrix.m[3] != 0 || matrix.m[7] != 0 || matrix.m[11] != 0 || matrix.m[15] != 1)
        {
            return std::nullopt;
        }
        const auto& m = matrix.m;
        // The 3 x 3 block L (row-vector convention) and its inverse by the adjugate.
        const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
        const double determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
        if (!std::isfinite(determinant) || std::abs(determinant) < sSingularDeterminant)
        {
            return std::nullopt;
        }
        const double inv = 1.0 / determinant;
        Mat4 result;
        result.m[0] = (e * i - f * h) * inv;
        result.m[1] = (c * h - b * i) * inv;
        result.m[2] = (b * f - c * e) * inv;
        result.m[4] = (f * g - d * i) * inv;
        result.m[5] = (a * i - c * g) * inv;
        result.m[6] = (c * d - a * f) * inv;
        result.m[8] = (d * h - e * g) * inv;
        result.m[9] = (b * g - a * h) * inv;
        result.m[10] = (a * e - b * d) * inv;
        // p = (p' - t) * L^-1 = p' * L^-1 - t * L^-1.
        const double tx = m[12], ty = m[13], tz = m[14];
        result.m[12] = -(tx * result.m[0] + ty * result.m[4] + tz * result.m[8]);
        result.m[13] = -(tx * result.m[1] + ty * result.m[5] + tz * result.m[9]);
        result.m[14] = -(tx * result.m[2] + ty * result.m[6] + tz * result.m[10]);
        for (const double v : result.m)
        {
            if (!std::isfinite(v))
            {
                return std::nullopt;
            }
        }
        return result;
    }

    Mat4 restMatrix(const std::array<double, 3>& translation, const std::array<double, 4>& rotation)
    {
        const Rot3 r = rotationOf(rotation);
        Mat4 result;
        for (std::size_t row = 0; row < 3; ++row)
        {
            for (std::size_t col = 0; col < 3; ++col)
            {
                result.m[row * 4 + col] = r.m[col * 3 + row]; // the transpose
            }
        }
        result.m[12] = translation[0];
        result.m[13] = translation[1];
        result.m[14] = translation[2];
        return result;
    }

    std::array<double, 3> transformPoint(const std::array<double, 3>& point, const Mat4& matrix)
    {
        const auto& m = matrix.m;
        return { point[0] * m[0] + point[1] * m[4] + point[2] * m[8] + m[12],
            point[0] * m[1] + point[1] * m[5] + point[2] * m[9] + m[13],
            point[0] * m[2] + point[1] * m[6] + point[2] * m[10] + m[14] };
    }

    std::vector<Mat4> worldRestMatrices(const std::vector<RestBone>& skeleton)
    {
        std::vector<Mat4> world;
        world.reserve(skeleton.size());
        for (std::size_t i = 0; i < skeleton.size(); ++i)
        {
            const RestBone& bone = skeleton[i];
            if (bone.mParent < -1 || bone.mParent >= static_cast<int>(i))
            {
                return {};
            }
            const Mat4 local = restMatrix(bone.mTranslation, bone.mRotation);
            world.push_back(bone.mParent < 0 ? local : multiply(local, world[static_cast<std::size_t>(bone.mParent)]));
        }
        return world;
    }

    std::optional<Mat4> correctedInverseBind(
        const Mat4& inverseBind, const Mat4& sourceWorldRest, const Mat4& canonicalWorldRest)
    {
        const std::optional<Mat4> back = inverse(canonicalWorldRest);
        if (!back)
        {
            return std::nullopt;
        }
        return multiply(multiply(inverseBind, sourceWorldRest), *back);
    }

    // ---- Conversions between the conventions ---------------------------------------------------------------------

    Mat34 toPlacement(const Mat4& matrix)
    {
        // Row-vector p' = p * M: p'[c] = sum_r p[r] * M[r][c] + M[3][c]. Column-vector v' = A v + t: A[c][r] = M[r][c].
        const auto& m = matrix.m;
        return { m[0], m[4], m[8], m[12], m[1], m[5], m[9], m[13], m[2], m[6], m[10], m[14] };
    }

    Mat4 fromPlacement(const Mat34& p)
    {
        Mat4 result;
        result.m = { p[0], p[4], p[8], 0, p[1], p[5], p[9], 0, p[2], p[6], p[10], 0, p[3], p[7], p[11], 1 };
        return result;
    }

    std::array<double, 16> toColumnMajor(const Mat4& matrix)
    {
        // The column-vector matrix is the transpose of the row-vector one; storing that transpose column by column
        // gives back the row-vector matrix's row-major order.
        return matrix.m;
    }

    // ---- Rest differences ----------------------------------------------------------------------------------------

    RestComparison compareRest(const SkeletonMap& map, const std::vector<RestBone>& source,
        const std::vector<RestBone>& actor, const std::vector<RoleBinding>& actorRoles)
    {
        RestComparison comparison;
        // Fail closed (review fix 7): an invalid skeleton is an error, never "nothing differs".
        for (const auto& [skeleton, label] :
            { std::pair<const std::vector<RestBone>*, const char*>{ &source, "source" },
                std::pair<const std::vector<RestBone>*, const char*>{ &actor, "actor" } })
        {
            for (const MapFinding& f : validateSourceSkeleton(*skeleton).mFindings)
            {
                if (f.mSeverity == MapSeverity::Error)
                {
                    comparison.mErrors.push_back(std::string("the ") + label + " skeleton: " + line(f));
                }
            }
        }
        if (!comparison.mErrors.empty())
        {
            return comparison;
        }
        const std::vector<Mat4> sourceWorld = worldRestMatrices(source);
        const std::vector<Mat4> actorWorld = worldRestMatrices(actor);
        if (sourceWorld.size() != source.size() || actorWorld.size() != actor.size())
        {
            comparison.mErrors.push_back("a skeleton's parents do not come before their children");
            return comparison;
        }
        std::map<std::string, std::size_t> sourceIndex;
        for (std::size_t i = 0; i < source.size(); ++i)
        {
            sourceIndex.emplace(engineBoneKey(source[i].mName), i);
        }
        std::map<std::string, std::size_t> actorIndex;
        for (std::size_t i = 0; i < actor.size(); ++i)
        {
            actorIndex.emplace(engineBoneKey(actor[i].mName), i);
        }
        std::map<std::string, std::size_t> sourceOfRole;
        for (const MapRole& role : map.mRoles)
        {
            if (const auto it = sourceIndex.find(engineBoneKey(role.mBone)); it != sourceIndex.end())
            {
                sourceOfRole.emplace(role.mRole, it->second);
            }
            else
            {
                comparison.mErrors.push_back(
                    "the role " + role.mRole + " names " + shown(role.mBone) + ", which the source skeleton lacks");
            }
        }
        std::map<std::string, std::size_t> actorOfRole;
        for (const RoleBinding& role : actorRoles)
        {
            if (const auto it = actorIndex.find(engineBoneKey(role.mBone)); it != actorIndex.end())
            {
                actorOfRole.emplace(role.mRole, it->second);
            }
            else
            {
                comparison.mErrors.push_back(
                    "the actor's role " + role.mRole + " names " + shown(role.mBone) + ", which the actor lacks");
            }
        }
        if (!comparison.mErrors.empty())
        {
            return comparison;
        }
        for (const RoleInfo& info : canonicalRoles())
        {
            const std::string role(info.mName);
            const std::string parent(info.mParent);
            if (parent.empty() || sourceOfRole.count(role) == 0 || actorOfRole.count(role) == 0
                || sourceOfRole.count(parent) == 0 || actorOfRole.count(parent) == 0)
            {
                continue; // a role (or its parent) that the map or the actor does not list: optional, not compared
            }
            const std::optional<Mat4> sourceBack = inverse(sourceWorld[sourceOfRole[parent]]);
            const std::optional<Mat4> actorBack = inverse(actorWorld[actorOfRole[parent]]);
            if (!sourceBack || !actorBack)
            {
                comparison.mErrors.push_back("the rest pose of the role " + parent + " is singular");
                continue;
            }
            const Mat4 sourceLocal = multiply(sourceWorld[sourceOfRole[role]], *sourceBack);
            const Mat4 actorLocal = multiply(actorWorld[actorOfRole[role]], *actorBack);
            const std::array<double, 3> st = translationOf(sourceLocal);
            const std::array<double, 3> at = translationOf(actorLocal);
            RoleRestDifference difference;
            difference.mRole = role;
            difference.mTranslationDelta = distance(st, at);
            const double sl = length(st);
            const double al = length(at);
            difference.mLengthRatio = (sl > 1e-12 && al > 1e-12) ? sl / al : 1.0;
            difference.mRotationAngle = rotationAngle(rotationOfMatrix(sourceLocal), rotationOfMatrix(actorLocal));
            if (!std::isfinite(difference.mTranslationDelta) || !std::isfinite(difference.mLengthRatio)
                || !std::isfinite(difference.mRotationAngle))
            {
                comparison.mErrors.push_back("the rest difference of the role " + role + " is not a number");
                continue;
            }
            comparison.mDifferences.push_back(std::move(difference));
        }
        if (comparison.mErrors.empty() && comparison.mDifferences.empty())
        {
            comparison.mErrors.push_back("no role could be compared (the map and the actor share no role pair)");
        }
        return comparison;
    }

    RestPlan planRest(const RestComparison& comparison, double rotationTolerance)
    {
        RestPlan plan = planRest(comparison.mDifferences, rotationTolerance);
        if (!comparison.mErrors.empty())
        {
            plan.mValid = false;
            plan.mErrors = comparison.mErrors;
        }
        return plan;
    }

    RestPlan planRest(const std::vector<RoleRestDifference>& differences, double rotationTolerance)
    {
        RestPlan plan;
        plan.mDifferences = differences;
        if (differences.empty())
        {
            plan.mErrors.push_back("nothing was compared");
            return plan;
        }
        plan.mValid = true;
        for (const RoleRestDifference& d : differences)
        {
            // !(a <= b) also catches a NaN: a value that is not a number never reads as "proportions suffice".
            if (!(d.mRotationAngle <= rotationTolerance))
            {
                plan.mNeedsInverseBindCorrection = true;
                break;
            }
        }
        return plan;
    }

    // ---- The mapping ---------------------------------------------------------------------------------------------

    struct MappingBuilder
    {
        static MappingResult build(const SkeletonMap& map, const std::vector<RestBone>& source,
            const std::vector<RoleBinding>& actorRoles, const BuildOptions& options);
    };

    MappingResult MappingBuilder::build(const SkeletonMap& map, const std::vector<RestBone>& source,
        const std::vector<RoleBinding>& actorRoles, const BuildOptions& options)
    {
        MappingResult result;
        const MapReport sourceReport = validateSourceSkeleton(source);
        for (const MapFinding& f : sourceReport.mFindings)
        {
            if (f.mSeverity == MapSeverity::Error)
            {
                result.mErrors.push_back(line(f));
            }
        }
        if (!result.mErrors.empty())
        {
            return result;
        }

        SkeletonMap working = map;
        if (options.mAcceptAllSuggestions)
        {
            for (MapExtra& e : working.mExtra)
            {
                if (e.mAction != MapAction::Ask)
                {
                    e.mAccepted = true;
                }
            }
        }
        const MapReport report = validateSkeletonMap(working, &source);
        for (const MapFinding& f : report.mFindings)
        {
            if (f.mSeverity == MapSeverity::Error)
            {
                result.mErrors.push_back(line(f));
            }
        }
        if (!result.mErrors.empty())
        {
            return result;
        }

        const std::vector<Mat4> world = worldRestMatrices(source);
        // Every lookup by name goes through engineBoneKey (review fix 1).
        std::map<std::string, std::size_t> sourceIndex;
        for (std::size_t i = 0; i < source.size(); ++i)
        {
            sourceIndex.emplace(engineBoneKey(source[i].mName), i);
        }
        std::map<std::string, const RoleBinding*> actorByRole;
        std::map<std::string, std::string> actorRoleOfBoneKey; // key of an actor role bone -> its role
        for (const RoleBinding& role : actorRoles)
        {
            actorByRole.emplace(role.mRole, &role);
            actorRoleOfBoneKey.emplace(engineBoneKey(role.mBone), role.mRole);
        }
        std::map<std::string, std::string> actorBoneByKey; // the actor's other bones (review fix 3)
        for (const std::string& bone : options.mActorBones)
        {
            const std::string key = engineBoneKey(bone);
            if (actorRoleOfBoneKey.count(key) == 0)
            {
                actorBoneByKey.emplace(key, bone);
            }
        }

        SkeletonMapping mapping;
        mapping.mSourceDigest = working.mSource.mDigest;
        mapping.mMapDigest = skeletonMapDigest(working);

        // Role bones, in canonical role order. An optional role the actor lacks is folded below (review fix 9).
        std::map<std::string, std::uint32_t> targetOfRole;
        std::map<std::string, std::uint32_t> targetOfSource; // engineBoneKey of a role, actor or secondary bone
        std::map<std::string, std::string> sourceOfRole;
        std::vector<const MapRole*> rolesTheActorLacks;
        for (const RoleInfo& info : canonicalRoles())
        {
            for (const MapRole& role : working.mRoles)
            {
                if (role.mRole != info.mName)
                {
                    continue;
                }
                sourceOfRole[role.mRole] = role.mBone;
                const auto actor = actorByRole.find(role.mRole);
                if (actor == actorByRole.end())
                {
                    if (info.mOptional)
                    {
                        rolesTheActorLacks.push_back(&role);
                    }
                    else
                    {
                        result.mErrors.push_back("the actor has no bone for the core role " + role.mRole);
                    }
                    continue;
                }
                const auto index = static_cast<std::uint32_t>(mapping.mTargetBones.size());
                mapping.mTargetBones.push_back(actor->second->mBone);
                targetOfRole[role.mRole] = index;
                targetOfSource[engineBoneKey(role.mBone)] = index;
            }
        }
        if (!result.mErrors.empty())
        {
            return result;
        }
        mapping.mRoleBoneCount = mapping.mTargetBones.size();

        // The nearest canonical ancestor role that has a target, for an optional role the actor lacks.
        std::map<std::string, std::uint32_t> foldOfLackingRole;
        std::map<std::string, std::string> foldRoleOfLackingRole; // the ancestor role it folds into
        for (const MapRole* role : rolesTheActorLacks)
        {
            std::string up(findRole(role->mRole)->mParent);
            std::size_t guard = 0;
            while (!up.empty() && targetOfRole.count(up) == 0 && guard++ < canonicalRoles().size())
            {
                const RoleInfo* info = findRole(up);
                up = info == nullptr ? std::string() : std::string(info->mParent);
            }
            if (up.empty() || targetOfRole.count(up) == 0)
            {
                result.mErrors.push_back("the actor has neither the role " + role->mRole + " nor an ancestor role");
                continue;
            }
            foldOfLackingRole[role->mRole] = targetOfRole[up];
            foldRoleOfLackingRole[role->mRole] = up;
            result.mNotes.push_back("the actor has no " + role->mRole + " bone: " + shown(role->mBone)
                + " folds into its canonical parent " + up + " (" + shown(mapping.mTargetBones[targetOfRole[up]])
                + ")");
        }
        if (!result.mErrors.empty())
        {
            return result;
        }

        // Extra bones that ARE bones of the actor (review fix 3): a name of another role's bone is a refusal; any other
        // actor bone becomes a target of its own (sorted by key, after the role bones) and the extra bone maps onto it.
        std::map<std::string, const MapExtra*> extraByKey;
        for (const MapExtra& e : working.mExtra)
        {
            extraByKey.emplace(engineBoneKey(e.mBone), &e);
        }
        std::map<std::string, std::uint32_t> actorTargetOfExtra; // key of an extra bone -> target index
        for (const auto& [key, e] : extraByKey)
        {
            if (const auto role = actorRoleOfBoneKey.find(key); role != actorRoleOfBoneKey.end())
            {
                result.mErrors.push_back("the source bone " + shown(e->mBone) + " has the name of the actor's bone for "
                    + "the role " + role->second + "; it can be neither that role nor a bone of its own");
                continue;
            }
            if (const auto actor = actorBoneByKey.find(key); actor != actorBoneByKey.end())
            {
                const auto index = static_cast<std::uint32_t>(mapping.mTargetBones.size());
                mapping.mTargetBones.push_back(actor->second);
                actorTargetOfExtra[key] = index;
                targetOfSource[key] = index;
                result.mNotes.push_back("the actor has its own bone " + shown(actor->second) + ": the source bone "
                    + shown(e->mBone) + " maps onto it (the map said " + toString(e->mAction) + ")");
            }
        }
        if (!result.mErrors.empty())
        {
            return result;
        }

        // Virtual bones: secondary and attach entries the actor does not have, by (depth in the map's parent chains,
        // name).
        const auto isRoleName = [&](const std::string& name) {
            return targetOfRole.count(name) != 0 || targetOfSource.count(engineBoneKey(name)) != 0;
        };
        const auto depthFor = [&](const std::string& bone) {
            std::size_t depth = 0;
            std::string current = bone;
            for (std::size_t guard = 0; guard <= SkeletonMapLimits::sMaxDepth + 1; ++guard)
            {
                const auto it = extraByKey.find(engineBoneKey(current));
                if (it == extraByKey.end() || isRoleName(it->second->mParent))
                {
                    return depth + 1;
                }
                ++depth;
                current = it->second->mParent;
            }
            return depth;
        };
        std::map<std::string, std::size_t> depthOf;
        std::vector<const MapExtra*> virtuals;
        for (const MapExtra& e : working.mExtra)
        {
            if ((e.mAction == MapAction::Secondary || e.mAction == MapAction::Attach)
                && actorTargetOfExtra.count(engineBoneKey(e.mBone)) == 0)
            {
                virtuals.push_back(&e);
                depthOf[e.mBone] = depthFor(e.mBone);
            }
        }
        std::sort(virtuals.begin(), virtuals.end(), [&](const MapExtra* a, const MapExtra* b) {
            if (depthOf[a->mBone] != depthOf[b->mBone])
            {
                return depthOf[a->mBone] < depthOf[b->mBone];
            }
            return a->mBone < b->mBone;
        });
        for (const MapExtra* e : virtuals)
        {
            const auto index = static_cast<std::uint32_t>(mapping.mTargetBones.size());
            mapping.mTargetBones.push_back(e->mBone);
            targetOfSource[engineBoneKey(e->mBone)] = index;
        }

        // The target index of a role name, a role bone, an actor bone or a secondary bone; nullopt when none.
        const auto resolveTarget = [&](const std::string& name) -> std::optional<std::uint32_t> {
            if (const auto role = targetOfRole.find(name); role != targetOfRole.end())
            {
                return role->second;
            }
            if (const auto lacking = foldOfLackingRole.find(name); lacking != foldOfLackingRole.end())
            {
                return lacking->second;
            }
            if (const auto bone = targetOfSource.find(engineBoneKey(name)); bone != targetOfSource.end())
            {
                return bone->second;
            }
            return std::nullopt;
        };
        // The source bone a name stands for (for rest positions).
        const auto resolveSourceBone = [&](const std::string& name) -> std::optional<std::size_t> {
            // A role the actor lacks stands for the role it folds into (its rest frame is that bone's).
            const auto lacking = foldRoleOfLackingRole.find(name);
            const std::string& roleName = lacking == foldRoleOfLackingRole.end() ? name : lacking->second;
            if (const auto role = sourceOfRole.find(roleName); role != sourceOfRole.end())
            {
                const auto it = sourceIndex.find(engineBoneKey(role->second));
                return it == sourceIndex.end() ? std::nullopt : std::optional<std::size_t>(it->second);
            }
            const auto it = sourceIndex.find(engineBoneKey(name));
            return it == sourceIndex.end() ? std::nullopt : std::optional<std::size_t>(it->second);
        };

        for (const MapExtra* e : virtuals)
        {
            VirtualBone bone;
            bone.mName = e->mBone;
            const auto parent = resolveTarget(e->mParent);
            const auto parentSource = resolveSourceBone(e->mParent);
            const auto self = sourceIndex.find(engineBoneKey(e->mBone));
            if (!parent || !parentSource || self == sourceIndex.end())
            {
                result.mErrors.push_back(
                    "the parent " + shown(e->mParent) + " of " + shown(e->mBone) + " cannot be resolved");
                continue;
            }
            bone.mParent = *parent;
            bone.mAction = e->mAction;
            bone.mKind = e->mKind;
            bone.mUseClipTrack = e->mUseClipTrack;
            bone.mAttachPoint = e->mAttachPoint;
            const std::optional<Mat4> back = inverse(world[*parentSource]);
            if (!back)
            {
                result.mErrors.push_back("the rest pose of " + shown(e->mParent) + " is singular");
                continue;
            }
            const Mat4 local = multiply(world[self->second], *back);
            bone.mRestTranslation = translationOf(local);
            bone.mRestRotation = quaternionOf(rotationOfMatrix(local));
            mapping.mVirtualBones.push_back(std::move(bone));
        }
        if (!result.mErrors.empty())
        {
            return result;
        }

        // The nearest ancestor of a source bone that is a role bone, an actor bone or a secondary bone.
        const auto nearestMapped = [&](std::size_t index) -> std::optional<std::uint32_t> {
            for (int up = source[index].mParent; up >= 0; up = source[static_cast<std::size_t>(up)].mParent)
            {
                const std::string key = engineBoneKey(source[static_cast<std::size_t>(up)].mName);
                const auto target = targetOfSource.find(key);
                if (target != targetOfSource.end())
                {
                    const auto extra = extraByKey.find(key);
                    if (extra == extraByKey.end() || extra->second->mAction == MapAction::Secondary
                        || actorTargetOfExtra.count(key) != 0)
                    {
                        return target->second;
                    }
                }
            }
            return std::nullopt;
        };

        // The rule of every source bone.
        for (const MapRole& role : working.mRoles)
        {
            RemapEntry entry;
            entry.mKind = RemapEntry::Kind::Direct;
            if (const auto lacking = foldOfLackingRole.find(role.mRole); lacking != foldOfLackingRole.end())
            {
                entry.mTarget = lacking->second;
            }
            else
            {
                entry.mTarget = targetOfRole[role.mRole];
            }
            mapping.mRules.push_back({ role.mBone, entry });
            for (const std::string& alias : role.mAliases)
            {
                mapping.mRules.push_back({ alias, entry });
            }
        }
        for (const MapExtra& e : working.mExtra)
        {
            const std::string key = engineBoneKey(e.mBone);
            const auto self = sourceIndex.find(key);
            if (self == sourceIndex.end())
            {
                continue;
            }
            RemapEntry entry;
            if (const auto actor = actorTargetOfExtra.find(key); actor != actorTargetOfExtra.end())
            {
                entry.mKind = RemapEntry::Kind::Direct;
                entry.mTarget = actor->second;
                mapping.mRules.push_back({ e.mBone, entry });
                continue;
            }
            switch (e.mAction)
            {
                case MapAction::Secondary:
                case MapAction::Attach:
                    entry.mKind = RemapEntry::Kind::Direct;
                    entry.mTarget = targetOfSource[key];
                    break;
                case MapAction::Drop:
                {
                    entry.mKind = RemapEntry::Kind::Dropped;
                    const std::optional<std::uint32_t> ancestor = nearestMapped(self->second);
                    entry.mFallback = ancestor.value_or(0);
                    break;
                }
                case MapAction::Fold:
                {
                    const FoldRule rule = e.mRule.value_or(FoldRule::NearestMappedAncestor);
                    if (rule == FoldRule::NearestMappedAncestor)
                    {
                        const std::optional<std::uint32_t> ancestor = nearestMapped(self->second);
                        if (!ancestor)
                        {
                            result.mErrors.push_back(
                                "the bone " + shown(e.mBone) + " has no mapped ancestor to fold into");
                            continue;
                        }
                        entry.mKind = RemapEntry::Kind::Direct;
                        entry.mTarget = *ancestor;
                    }
                    else if (rule == FoldRule::Named)
                    {
                        const auto target = resolveTarget(e.mTo);
                        if (!target)
                        {
                            result.mErrors.push_back(
                                "the fold target " + shown(e.mTo) + " of " + shown(e.mBone) + " is not mapped");
                            continue;
                        }
                        entry.mKind = RemapEntry::Kind::Direct;
                        entry.mTarget = *target;
                    }
                    else
                    {
                        const auto first = resolveTarget(e.mTo);
                        const auto second = resolveTarget(e.mToSecond);
                        const auto firstSource = resolveSourceBone(e.mTo);
                        const auto secondSource = resolveSourceBone(e.mToSecond);
                        if (!first || !second || !firstSource || !secondSource)
                        {
                            result.mErrors.push_back("the fold targets of " + shown(e.mBone) + " are not mapped");
                            continue;
                        }
                        const std::array<double, 3> here = transformPoint({ 0, 0, 0 }, world[self->second]);
                        const double d1 = distance(here, transformPoint({ 0, 0, 0 }, world[*firstSource]));
                        const double d2 = distance(here, transformPoint({ 0, 0, 0 }, world[*secondSource]));
                        if (!std::isfinite(d1) || !std::isfinite(d2))
                        {
                            // Review fix 6: never let a NaN fraction reach the weights (and their sort).
                            result.mErrors.push_back("the rest distances of " + shown(e.mBone) + " are not finite");
                            continue;
                        }
                        entry.mKind = RemapEntry::Kind::Split;
                        entry.mTarget = *first;
                        entry.mTarget2 = *second;
                        entry.mFraction = (d1 + d2 > 0) ? d2 / (d1 + d2) : 0.5;
                    }
                    break;
                }
                case MapAction::Ask:
                    continue;
            }
            mapping.mRules.push_back({ e.mBone, entry });
        }
        if (!result.mErrors.empty())
        {
            return result;
        }
        std::sort(mapping.mRules.begin(), mapping.mRules.end(),
            [](const SkeletonMapping::SourceRule& a, const SkeletonMapping::SourceRule& b) {
                const std::string ka = engineBoneKey(a.mBone);
                const std::string kb = engineBoneKey(b.mBone);
                return ka != kb ? ka < kb : a.mBone < b.mBone;
            });

        result.mAcceptedMap = std::move(working);
        result.mValue = std::move(mapping);
        return result;
    }

    MappingResult buildMapping(const SkeletonMap& map, const std::vector<RestBone>& source,
        const std::vector<RoleBinding>& actorRoles, const BuildOptions& options)
    {
        try
        {
            return MappingBuilder::build(map, source, actorRoles, options);
        }
        catch (const std::exception& e)
        {
            MappingResult result;
            result.mErrors.push_back(std::string("cannot build the mapping: ") + e.what());
            return result;
        }
    }

    SkeletonMapping::TableResult SkeletonMapping::remapTable(const std::vector<std::string>& skinBones) const
    {
        TableResult result;
        RemapTable table;
        table.mEntries.reserve(skinBones.size());
        for (const std::string& name : skinBones)
        {
            // By engineBoneKey, as Anim::Skin binds a skin bone (review fix 1).
            const std::string key = engineBoneKey(name);
            const auto it = std::lower_bound(mRules.begin(), mRules.end(), key,
                [](const SourceRule& rule, const std::string& k) { return engineBoneKey(rule.mBone) < k; });
            if (it == mRules.end() || engineBoneKey(it->mBone) != key)
            {
                result.mErrors.push_back("the skin bone " + shown(name) + " has no rule in the map");
                continue;
            }
            table.mEntries.push_back(it->mEntry);
        }
        if (result.mErrors.empty())
        {
            result.mValue = std::move(table);
        }
        return result;
    }

    CutWeights remapVertex(const RemapTable& table, const std::vector<std::pair<std::uint32_t, float>>& influences)
    {
        WeightList raw;
        raw.reserve(influences.size() * 2);
        // The dropped weight per fallback target, a short list (a vertex has a handful of influences).
        WeightList droppedByFallback;
        for (const auto& [bone, weight] : influences)
        {
            if (bone >= table.mEntries.size() || !std::isfinite(weight) || !(weight > 0.0f))
            {
                continue;
            }
            const RemapEntry& entry = table.mEntries[bone];
            const double w = weight;
            switch (entry.mKind)
            {
                case RemapEntry::Kind::Direct:
                    raw.emplace_back(entry.mTarget, w);
                    break;
                case RemapEntry::Kind::Split:
                    raw.emplace_back(entry.mTarget, w * entry.mFraction);
                    raw.emplace_back(entry.mTarget2, w * (1.0 - entry.mFraction));
                    break;
                case RemapEntry::Kind::Dropped:
                {
                    const auto found = std::find_if(droppedByFallback.begin(), droppedByFallback.end(),
                        [&](const auto& item) { return item.first == entry.mFallback; });
                    if (found == droppedByFallback.end())
                    {
                        droppedByFallback.emplace_back(entry.mFallback, w);
                    }
                    else
                    {
                        found->second += w;
                    }
                    break;
                }
            }
        }
        WeightList weights = normalisedWeights(std::move(raw));
        if (weights.empty() && !droppedByFallback.empty())
        {
            // Nothing is left: the fallback target that received the most dropped weight takes the vertex (ties: the
            // lower target index).
            std::uint32_t best = droppedByFallback.front().first;
            double bestWeight = droppedByFallback.front().second;
            for (const auto& [target, w] : droppedByFallback)
            {
                if (w > bestWeight || (w == bestWeight && target < best))
                {
                    best = target;
                    bestWeight = w;
                }
            }
            weights.emplace_back(best, 1.0);
        }
        return cutToFourWeights(weights);
    }
}
