#include "neutralplanelinks.hpp"

#include <algorithm>
#include <cmath>

namespace MWPhysics::NeutralPlaneLinks
{
    namespace
    {
        bool finite(const vsg::vec3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        struct Floor
        {
            bool mFound = false;
            float mZ = 0.f;
        };

        // The floor a walker stands on at (x, y) for a wall whose foot is at zLow: a ray from kFloorReach above the foot,
        // reaching kFloorReach below it, on a surface level enough to walk on.
        Floor floorAt(const World& world, float x, float y, float zLow)
        {
            Floor result;
            const float depth = 2.f * kFloorReach;
            const vsg::vec3 from(x, y, zLow + kFloorReach);
            const NeutralMover::Sweep hit = world.probeDown(from, depth);
            if (!hit.mHit || hit.mNormal.z < NeutralMover::kMinFloorNormalZ)
                return result;
            result.mFound = true;
            result.mZ = from.z - hit.mFraction * depth;
            return result;
        }

        bool passable(const World& world, const vsg::vec3& from, const vsg::vec3& to)
        {
            // As the mover does: sweep level, above the higher of the two floors (it steps up before it moves).
            const vsg::vec3 start(from.x, from.y, std::max(from.z, to.z) + NeutralMover::kSkin);
            const NeutralMover::Sweep sweep = world.sweep(start, vsg::vec3(to.x - from.x, to.y - from.y, 0.f));
            return !sweep.mHit;
        }

        bool alreadyHave(const std::vector<Link>& links, const Link& link)
        {
            const auto near = [](const vsg::vec3& a, const vsg::vec3& b) {
                const float dx = a.x - b.x;
                const float dy = a.y - b.y;
                const float dz = a.z - b.z;
                return dx * dx + dy * dy + dz * dz < kMergeDistance * kMergeDistance;
            };
            return std::any_of(links.begin(), links.end(),
                [&](const Link& have) { return near(have.mFrom, link.mFrom) && near(have.mTo, link.mTo); });
        }

        // Lowest height of the triangle on the vertical plane at position `s` along the wall; false if it does not reach it.
        bool lowestAt(const Triangle& t, const float (&along)[3], float s, float& zLow)
        {
            bool found = false;
            zLow = 0.f;
            const auto take = [&](float z) {
                zLow = found ? std::min(zLow, z) : z;
                found = true;
            };
            for (int i = 0; i < 3; ++i)
            {
                const int j = (i + 1) % 3;
                const float si = along[i];
                const float sj = along[j];
                if (std::fabs(si - sj) < 1e-4f)
                {
                    if (std::fabs(si - s) < 1e-3f)
                    {
                        take(t.mV[i].z);
                        take(t.mV[j].z);
                    }
                    continue;
                }
                if ((si - s) * (sj - s) <= 0.f)
                    take(t.mV[i].z + (t.mV[j].z - t.mV[i].z) * ((s - si) / (sj - si)));
            }
            return found;
        }
    }

    std::vector<Link> find(std::span<const Triangle> triangles, const World& world)
    {
        std::vector<Link> links;
        for (const Triangle& t : triangles)
        {
            if (links.size() >= kMaxLinksPerObject)
                break;
            if (!finite(t.mV[0]) || !finite(t.mV[1]) || !finite(t.mV[2]))
                continue;
            const vsg::vec3 e1 = t.mV[1] - t.mV[0];
            const vsg::vec3 e2 = t.mV[2] - t.mV[0];
            const vsg::vec3 n(
                e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x);
            const float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            const float horizontal = std::sqrt(n.x * n.x + n.y * n.y);
            if (!(length > 1e-6f) || !(horizontal > 1e-6f) || std::fabs(n.z) / length >= kWallMaxNormalZ)
                continue;
            const float hx = n.x / horizontal;
            const float hy = n.y / horizontal;
            const float tx = -hy;
            const float ty = hx;
            float along[3];
            float smin = 1e30f;
            float smax = -1e30f;
            for (int i = 0; i < 3; ++i)
            {
                along[i] = t.mV[i].x * tx + t.mV[i].y * ty;
                smin = std::min(smin, along[i]);
                smax = std::max(smax, along[i]);
            }
            const float width = smax - smin;
            if (width < kMinWallWidth)
                continue;
            const float offset = t.mV[0].x * hx + t.mV[0].y * hy;
            const int samples = std::max(1, static_cast<int>(std::floor(width / kSampleSpacing)));
            for (int k = 0; k < samples && links.size() < kMaxLinksPerObject; ++k)
            {
                const float s = smin + (static_cast<float>(k) + 0.5f) * width / static_cast<float>(samples);
                float zLow = 0.f;
                if (!lowestAt(t, along, s, zLow))
                    continue;
                const float px = tx * s + hx * offset;
                const float py = ty * s + hy * offset;
                const float ax = px - hx * kStandOff;
                const float ay = py - hy * kStandOff;
                const float bx = px + hx * kStandOff;
                const float by = py + hy * kStandOff;
                const Floor floorA = floorAt(world, ax, ay, zLow);
                const Floor floorB = floorAt(world, bx, by, zLow);
                if (!floorA.mFound || !floorB.mFound || std::fabs(floorA.mZ - floorB.mZ) > kFloorReach)
                    continue;
                const vsg::vec3 feetA(ax, ay, floorA.mZ);
                const vsg::vec3 feetB(bx, by, floorB.mZ);
                if (!world.fits(feetA) || !world.fits(feetB))
                    continue;
                for (const Link& candidate : { Link{ feetA, feetB }, Link{ feetB, feetA } })
                {
                    if (links.size() >= kMaxLinksPerObject)
                        break;
                    if (passable(world, candidate.mFrom, candidate.mTo) && !alreadyHave(links, candidate))
                        links.push_back(candidate);
                }
            }
        }
        return links;
    }
}
