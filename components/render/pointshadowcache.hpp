#ifndef OPENEDEN_RENDER_POINTSHADOWCACHE_H
#define OPENEDEN_RENDER_POINTSHADOWCACHE_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <vsg/maths/vec3.h>

#include "pointshadowmath.hpp"
#include "pointshadowselect.hpp"

namespace Render::PointShadow
{
    /*
     * Which faces of the point light shadow maps are drawn this frame (task 0077, stage e; the design is in the spec,
     * "Caching the maps"). A pure state machine: no clock, no pointer order, no unordered container, and it allocates
     * nothing once its vectors have grown.
     *
     * A face keeps its depth image until it is drawn again. Two kinds of reasons to draw:
     *  - HARD, for a whole slot at once and not counted against the budget (the stale image would be another light's
     *    shadow or a shadow from another place): the slot is new, holds another light, or the light's radius changed
     *    or it moved by more than `moveTolerance` from where the maps were drawn.
     *  - SOFT, per face, counted against `facesPerFrame` and served oldest first (then slot, then face): the world
     *    changed (the epoch), a disturbance (a moving caster) touches the face's wedge, or the face is `refreshFrames`
     *    old (the net for everything not detected: a door, an animation, a loaded terrain tile).
     * A soft face that is pending is drawn within ceil(6 * slots / facesPerFrame) steps, whatever else happens: a face
     * that is drawn restarts at age 0, so no newer request can pass an older one.
     * The light's colour is not an input: a flickering light never invalidates anything.
     */

    struct CacheParams
    {
        /// Soft faces drawn per frame at most (1 or more).
        int facesPerFrame = 6;
        /// A face this many steps old is drawn again.
        std::uint32_t refreshFrames = 24;
        /// Game units a light may move from where its maps were drawn before they are all drawn again.
        double moveTolerance = 1.0;
    };

    struct CacheLight
    {
        bool used = false;
        std::uint64_t id = 0;
        vsg::dvec3 position;
        float radius = 0.f;
    };

    /// A moving caster: a sphere that has to be redrawn (where it is now, and where it was).
    struct Disturbance
    {
        vsg::dvec3 center;
        float radius = 0.f;
    };

    /// The faces' wedges are 90 degrees plus the guard band; this is a little wider than the widest of them.
    constexpr double cacheTanHalf = 1.25;

    /// Whether a sphere may be drawn in the face's map: inside the light's far plane and not wholly outside the face's
    /// pyramid (conservative: never says no for a sphere that is inside).
    inline bool disturbanceTouchesFace(const CacheLight& light, const Disturbance& disturbance, int face)
    {
        const double x = disturbance.center.x - light.position.x;
        const double y = disturbance.center.y - light.position.y;
        const double z = disturbance.center.z - light.position.z;
        const double r = disturbance.radius;
        const double far = farMargin * reach(light.radius);
        const double distance = std::sqrt(x * x + y * y + z * z);
        if (distance - r > far)
            return false;
        if (distance <= r)
            return true;
        const FaceAxes axes = faceAxes(face);
        const double forward = x * axes.forward.x + y * axes.forward.y + z * axes.forward.z;
        const double right = x * axes.right.x + y * axes.right.y + z * axes.right.z;
        const double up = x * axes.up.x + y * axes.up.y + z * axes.up.z;
        const double norm = std::sqrt(1.0 + cacheTanHalf * cacheTanHalf);
        const double slack = r * norm;
        const double t = cacheTanHalf * forward;
        return right - t <= slack && -right - t <= slack && up - t <= slack && -up - t <= slack;
    }

    class FaceCache
    {
    public:
        /// `lights`: one entry per slot. `draw` gets slots * 6 entries, 1 where face (slot * 6 + face) is drawn.
        void step(const std::vector<CacheLight>& lights, const std::vector<Disturbance>& disturbances,
            std::uint32_t epoch, const CacheParams& params, std::vector<std::uint8_t>& draw)
        {
            const std::size_t slotCount = lights.size();
            draw.assign(slotCount * facesPerLight, 0);
            if (mSlots.size() != slotCount)
                mSlots.assign(slotCount, SlotState{});
            const bool epochChanged = mHaveEpoch && epoch != mEpoch;
            mEpoch = epoch;
            mHaveEpoch = true;

            mQueue.clear();
            mLastHardSlots = 0;
            for (std::size_t s = 0; s < slotCount; ++s)
            {
                const CacheLight& now = lights[s];
                SlotState& state = mSlots[s];
                if (!now.used)
                {
                    state = SlotState{};
                    continue;
                }
                if (hardChange(state, now, params))
                {
                    state = SlotState{};
                    state.valid = true;
                    state.drawn = now;
                    ++mLastHardSlots;
                    for (std::size_t f = 0; f < facesPerLight; ++f)
                        draw[s * facesPerLight + f] = 1;
                    continue;
                }
                for (std::size_t f = 0; f < facesPerLight; ++f)
                {
                    if (state.age[f] != std::numeric_limits<std::uint32_t>::max())
                        ++state.age[f];
                    bool pending = state.pending[f] != 0 || epochChanged || state.age[f] >= params.refreshFrames;
                    for (std::size_t d = 0; !pending && d < disturbances.size(); ++d)
                        pending = disturbanceTouchesFace(state.drawn, disturbances[d], static_cast<int>(f));
                    state.pending[f] = pending ? 1 : 0;
                    if (pending)
                        mQueue.push_back({ state.age[f], s, f });
                }
            }

            mLastPending = mQueue.size();
            std::sort(mQueue.begin(), mQueue.end(), [](const Entry& a, const Entry& b) {
                if (a.age != b.age)
                    return a.age > b.age;
                if (a.slot != b.slot)
                    return a.slot < b.slot;
                return a.face < b.face;
            });
            const std::size_t budget = static_cast<std::size_t>(std::max(params.facesPerFrame, 1));
            for (std::size_t q = 0; q < mQueue.size() && q < budget; ++q)
            {
                const Entry& e = mQueue[q];
                draw[e.slot * facesPerLight + e.face] = 1;
                mSlots[e.slot].age[e.face] = 0;
                mSlots[e.slot].pending[e.face] = 0;
            }
        }

        /// Slots that were drawn whole in the last step, and soft faces that were waiting (for logs and counters).
        std::size_t lastHardSlots() const { return mLastHardSlots; }
        std::size_t lastPendingFaces() const { return mLastPending; }

    private:
        struct SlotState
        {
            bool valid = false;
            CacheLight drawn; // the light as the maps were last drawn for it
            std::array<std::uint32_t, facesPerLight> age{};
            std::array<std::uint8_t, facesPerLight> pending{};
        };

        struct Entry
        {
            std::uint32_t age;
            std::size_t slot;
            std::size_t face;
        };

        static bool hardChange(const SlotState& state, const CacheLight& now, const CacheParams& params)
        {
            if (!state.valid || state.drawn.id != now.id || state.drawn.radius != now.radius)
                return true;
            const double x = now.position.x - state.drawn.position.x;
            const double y = now.position.y - state.drawn.position.y;
            const double z = now.position.z - state.drawn.position.z;
            return x * x + y * y + z * z > params.moveTolerance * params.moveTolerance;
        }

        std::vector<SlotState> mSlots;
        std::vector<Entry> mQueue;
        std::uint32_t mEpoch = 0;
        bool mHaveEpoch = false;
        std::size_t mLastHardSlots = 0;
        std::size_t mLastPending = 0;
    };
    /*
     * Finds the casters that moved between two frames (task 0077, stage e). The caller reports each tracked caster once
     * a frame with a key (only used to recognise it again; never ordered by meaning), its pose (position and the nine
     * entries of its rotation) and the sphere that holds it. A caster whose pose changed, appeared or vanished gives a
     * disturbance at its sphere now and at the sphere it had. The pose is compared with the one at the last report,
     * not with the last frame's, so a slow drift adds up until it passes the epsilon. The result does not depend on
     * the order of `add` calls.
     */
    class MotionTracker
    {
    public:
        using Pose = std::array<float, 12>;

        /// Game units a position may change by before it counts as moving (float wobble of a still actor).
        static constexpr float positionEpsilon = 0.5f;
        /// Rotation matrix entries (0.01 is about 0.6 degrees).
        static constexpr float rotationEpsilon = 0.01f;

        void begin() { mCurrent.clear(); }

        void add(std::uint64_t key, const Pose& pose, const vsg::dvec3& center, float radius)
        {
            mCurrent.push_back({ key, pose, center, radius });
        }

        void end(std::vector<Disturbance>& out)
        {
            out.clear();
            std::sort(mCurrent.begin(), mCurrent.end(), [](const Item& a, const Item& b) { return a.key < b.key; });
            std::size_t p = 0;
            for (const Item& now : mCurrent)
            {
                while (p < mPrevious.size() && mPrevious[p].key < now.key)
                {
                    out.push_back({ mPrevious[p].center, mPrevious[p].radius });
                    ++p;
                }
                if (p < mPrevious.size() && mPrevious[p].key == now.key)
                {
                    if (moved(mPrevious[p].pose, now.pose))
                    {
                        out.push_back({ mPrevious[p].center, mPrevious[p].radius });
                        out.push_back({ now.center, now.radius });
                    }
                    else
                    {
                        // Not moved far enough yet: the anchor stays where it was last reported.
                        mCurrent[static_cast<std::size_t>(&now - mCurrent.data())] = mPrevious[p];
                    }
                    ++p;
                }
                else
                {
                    out.push_back({ now.center, now.radius });
                }
            }
            for (; p < mPrevious.size(); ++p)
                out.push_back({ mPrevious[p].center, mPrevious[p].radius });
            mPrevious.swap(mCurrent);
        }

    private:
        struct Item
        {
            std::uint64_t key;
            Pose pose;
            vsg::dvec3 center;
            float radius;
        };

        static bool moved(const Pose& a, const Pose& b)
        {
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                const float limit = i < 3 ? positionEpsilon : rotationEpsilon;
                if (!(std::abs(a[i] - b[i]) <= limit))
                    return true;
            }
            return false;
        }

        std::vector<Item> mCurrent;
        std::vector<Item> mPrevious;
    };
}

#endif
