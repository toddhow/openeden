#ifndef OPENEDEN_RENDER_POINTSHADOWSELECT_H
#define OPENEDEN_RENDER_POINTSHADOWSELECT_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <tuple>
#include <vector>

#include <vsg/maths/vec3.h>

namespace Render::PointShadow
{
    /*
     * Which point lights get a shadow (task 0077, research note 0039 section 3). A pure function of its inputs: no
     * clock, no pointer order, no unordered container. The caller's `id` only recognises a holder from the last call.
     */

    struct Candidate
    {
        std::uint64_t id = 0;
        vsg::dvec3 position;
        float radius = 0.f;
        bool casts = true;
    };

    struct Slot
    {
        bool used = false;
        std::uint64_t id = 0;
        vsg::dvec3 position;
        float radius = 0.f;
        std::uint32_t heldFrames = 0;
        float fade = 0.f;
    };

    struct SelectionParams
    {
        int maxSlots = 4;
        double holdFactor = 1.25;
        std::uint32_t minHoldFrames = 30;
        double maxDistance = 2048.0;
        std::uint32_t fadeFrames = 8;
    };

    /// radius^2 / (distance^2 + 1): from the radius and the camera distance only (the colour flickers).
    inline double importance(float radius, double distance)
    {
        return static_cast<double>(radius) * radius / (distance * distance + 1.0);
    }

    /// A light ends at twice its radius (files/shaders/lib/light/equations.slang, `lightRange`).
    inline double reach(float radius)
    {
        return 2.0 * radius;
    }

    struct Selection
    {
        std::vector<Slot> slots;
        std::vector<int> slotOfCandidate;
    };

    namespace Detail
    {
        inline double distanceBetween(const vsg::dvec3& a, const vsg::dvec3& b)
        {
            const double x = a.x - b.x;
            const double y = a.y - b.y;
            const double z = a.z - b.z;
            return std::sqrt(x * x + y * y + z * z);
        }

        inline std::int64_t quantise(double value)
        {
            return static_cast<std::int64_t>(std::floor(value * 16.0));
        }
    }

    inline Selection select(const std::vector<Candidate>& candidates, const vsg::dvec3& camera,
        const std::vector<Slot>& previous, const SelectionParams& params)
    {
        const std::size_t slotCount = static_cast<std::size_t>(std::max(params.maxSlots, 0));
        Selection result;
        result.slots.assign(slotCount, Slot{});
        result.slotOfCandidate.assign(candidates.size(), -1);

        struct Entry
        {
            std::size_t index;
            double score;
        };
        std::vector<Entry> eligible;
        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            const Candidate& c = candidates[i];
            if (!c.casts || !(c.radius > 0.f) || !std::isfinite(c.position.x) || !std::isfinite(c.position.y)
                || !std::isfinite(c.position.z))
                continue;
            const double distance = Detail::distanceBetween(camera, c.position);
            if (distance > reach(c.radius) + params.maxDistance)
                continue;
            eligible.push_back({ i, importance(c.radius, distance) });
        }
        // The canonical order: score descending, quantised position, radius, input index.
        std::sort(eligible.begin(), eligible.end(), [&](const Entry& a, const Entry& b) {
            if (a.score != b.score)
                return a.score > b.score;
            const Candidate& ca = candidates[a.index];
            const Candidate& cb = candidates[b.index];
            const auto ka = std::make_tuple(Detail::quantise(ca.position.x), Detail::quantise(ca.position.y),
                Detail::quantise(ca.position.z), ca.radius, a.index);
            const auto kb = std::make_tuple(Detail::quantise(cb.position.x), Detail::quantise(cb.position.y),
                Detail::quantise(cb.position.z), cb.radius, b.index);
            return ka < kb;
        });

        std::vector<int> candidateOfSlot(slotCount, -1);
        std::vector<bool> isHolder(slotCount, false);
        std::vector<bool> taken(eligible.size(), false);

        // Holders keep their slot.
        for (std::size_t e = 0; e < eligible.size(); ++e)
        {
            const Candidate& c = candidates[eligible[e].index];
            for (std::size_t s = 0; s < slotCount && s < previous.size(); ++s)
            {
                const Slot& old = previous[s];
                if (!old.used || old.id != c.id || candidateOfSlot[s] >= 0)
                    continue;
                Slot& slot = result.slots[s];
                slot.used = true;
                slot.id = c.id;
                slot.position = c.position;
                slot.radius = c.radius;
                slot.heldFrames
                    = old.heldFrames == std::numeric_limits<std::uint32_t>::max() ? old.heldFrames : old.heldFrames + 1;
                slot.fade = params.fadeFrames == 0
                    ? 1.f
                    : std::min(1.f, old.fade + 1.f / static_cast<float>(params.fadeFrames));
                candidateOfSlot[s] = static_cast<int>(eligible[e].index);
                isHolder[s] = true;
                taken[e] = true;
                break;
            }
        }

        // The others take a free slot or displace the weakest long-held holder.
        for (std::size_t e = 0; e < eligible.size(); ++e)
        {
            if (taken[e])
                continue;
            const Candidate& c = candidates[eligible[e].index];
            int target = -1;
            for (std::size_t s = 0; s < slotCount; ++s)
            {
                if (candidateOfSlot[s] < 0)
                {
                    target = static_cast<int>(s);
                    break;
                }
            }
            if (target < 0)
            {
                double weakest = 0.0;
                for (std::size_t s = 0; s < slotCount; ++s)
                {
                    if (!isHolder[s] || result.slots[s].heldFrames < params.minHoldFrames)
                        continue;
                    const double score
                        = importance(result.slots[s].radius, Detail::distanceBetween(camera, result.slots[s].position));
                    if (target < 0 || score < weakest)
                    {
                        target = static_cast<int>(s);
                        weakest = score;
                    }
                }
                if (target < 0 || !(eligible[e].score > params.holdFactor * weakest))
                    continue;
            }
            const std::size_t t = static_cast<std::size_t>(target);
            Slot& slot = result.slots[t];
            slot = Slot{};
            slot.used = true;
            slot.id = c.id;
            slot.position = c.position;
            slot.radius = c.radius;
            candidateOfSlot[t] = static_cast<int>(eligible[e].index);
            isHolder[t] = false;
        }

        for (std::size_t s = 0; s < slotCount; ++s)
        {
            if (candidateOfSlot[s] >= 0)
                result.slotOfCandidate[static_cast<std::size_t>(candidateOfSlot[s])] = static_cast<int>(s);
        }
        return result;
    }
}

#endif
