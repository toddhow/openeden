// Tests for the point light shadow map cache, Render::PointShadow::FaceCache (task 0077, stage e: docs/tasks/0077).
// Written from the design in the spec ("Caching the maps") before the implementation. Pure CPU logic: no device or
// window.

#include <components/render/pointshadowcache.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

namespace
{
    using namespace Render::PointShadow;
    using V3 = vsg::dvec3;

    constexpr int faces = facesPerLight;

    CacheLight light(std::uint64_t id, double x, double y, double z, float radius = 100.f)
    {
        CacheLight l;
        l.used = true;
        l.id = id;
        l.position = V3(x, y, z);
        l.radius = radius;
        return l;
    }

    Disturbance dist(double x, double y, double z, float radius)
    {
        Disturbance d;
        d.center = V3(x, y, z);
        d.radius = radius;
        return d;
    }

    int count(const std::vector<std::uint8_t>& draw, int slot)
    {
        int n = 0;
        for (int f = 0; f < faces; ++f)
            n += draw[static_cast<std::size_t>(slot * faces + f)] ? 1 : 0;
        return n;
    }

    int total(const std::vector<std::uint8_t>& draw)
    {
        return static_cast<int>(std::count_if(draw.begin(), draw.end(), [](std::uint8_t v) { return v != 0; }));
    }

    struct Rig
    {
        FaceCache cache;
        CacheParams params;
        std::vector<std::uint8_t> draw;
        std::uint32_t epoch = 0;

        void step(const std::vector<CacheLight>& lights, const std::vector<Disturbance>& disturbances = {})
        {
            cache.step(lights, disturbances, epoch, params, draw);
        }
    };

    int ceilDiv(int a, int b)
    {
        return (a + b - 1) / b;
    }
}

TEST(PointShadowCache, a_new_light_draws_all_six_faces_at_once)
{
    Rig rig;
    rig.step({ light(1, 0, 0, 0), CacheLight{} });
    ASSERT_EQ(rig.draw.size(), static_cast<std::size_t>(2 * faces));
    EXPECT_EQ(count(rig.draw, 0), faces);
    EXPECT_EQ(count(rig.draw, 1), 0);
}

TEST(PointShadowCache, a_still_light_draws_nothing_until_the_refresh_period)
{
    Rig rig;
    rig.params.refreshFrames = 24;
    rig.step({ light(1, 0, 0, 0) });
    for (int frame = 1; frame < 24; ++frame)
    {
        rig.step({ light(1, 0, 0, 0) });
        EXPECT_EQ(total(rig.draw), 0) << "frame " << frame;
    }
    rig.step({ light(1, 0, 0, 0) });
    EXPECT_GT(total(rig.draw), 0);
}

TEST(PointShadowCache, a_radius_change_redraws_every_face_at_once)
{
    Rig rig;
    rig.step({ light(1, 0, 0, 0, 100.f) });
    rig.step({ light(1, 0, 0, 0, 100.f) });
    rig.step({ light(1, 0, 0, 0, 120.f) });
    EXPECT_EQ(count(rig.draw, 0), faces);
}

TEST(PointShadowCache, a_different_light_in_the_slot_redraws_every_face_at_once)
{
    Rig rig;
    rig.step({ light(1, 0, 0, 0) });
    rig.step({ light(1, 0, 0, 0) });
    rig.step({ light(2, 0, 0, 0) });
    EXPECT_EQ(count(rig.draw, 0), faces);
}

TEST(PointShadowCache, a_slot_that_was_free_and_is_taken_again_redraws_even_for_the_same_light)
{
    Rig rig;
    rig.step({ light(1, 0, 0, 0) });
    rig.step({ CacheLight{} });
    EXPECT_EQ(total(rig.draw), 0);
    rig.step({ light(1, 0, 0, 0) });
    EXPECT_EQ(count(rig.draw, 0), faces);
}

TEST(PointShadowCache, a_move_inside_the_tolerance_is_ignored_and_drift_adds_up)
{
    Rig rig;
    rig.params.moveTolerance = 1.0;
    rig.step({ light(1, 0, 0, 0) });
    rig.step({ light(1, 0.6, 0, 0) });
    EXPECT_EQ(total(rig.draw), 0);
    // 1.2 from where the maps were drawn, though only 0.6 from the last frame.
    rig.step({ light(1, 1.2, 0, 0) });
    EXPECT_EQ(count(rig.draw, 0), faces);
    // The new position is the reference now.
    rig.step({ light(1, 1.7, 0, 0) });
    EXPECT_EQ(total(rig.draw), 0);
}

TEST(PointShadowCache, a_world_change_redraws_every_face_of_every_used_slot_within_the_budget)
{
    Rig rig;
    rig.params.facesPerFrame = 4;
    rig.params.refreshFrames = 1000;
    const std::vector<CacheLight> lights = { light(1, 0, 0, 0), light(2, 500, 0, 0) };
    rig.step(lights);
    rig.step(lights);
    EXPECT_EQ(total(rig.draw), 0);
    rig.epoch = 7;
    std::vector<int> seen(static_cast<std::size_t>(2 * faces), 0);
    int frames = 0;
    for (; frames < 10; ++frames)
    {
        rig.step(lights);
        EXPECT_LE(total(rig.draw), 4);
        for (std::size_t i = 0; i < seen.size(); ++i)
            seen[i] += rig.draw[i] ? 1 : 0;
        if (std::all_of(seen.begin(), seen.end(), [](int n) { return n > 0; }))
            break;
    }
    EXPECT_EQ(frames + 1, ceilDiv(2 * faces, 4));
    for (int n : seen)
        EXPECT_EQ(n, 1);
}

TEST(PointShadowCache, an_epoch_change_in_the_frame_of_a_new_light_does_not_draw_it_twice)
{
    Rig rig;
    rig.params.refreshFrames = 1000;
    rig.step({ light(1, 0, 0, 0) });
    rig.epoch = 1;
    rig.step({ light(1, 0, 0, 0), light(2, 400, 0, 0) });
    EXPECT_EQ(count(rig.draw, 1), faces);
    rig.step({ light(1, 0, 0, 0), light(2, 400, 0, 0) });
    EXPECT_EQ(count(rig.draw, 1), 0);
}

TEST(PointShadowCache, a_disturbance_touches_only_the_faces_it_is_in)
{
    const CacheLight l = light(1, 0, 0, 0, 100.f);
    // Straight along +X, small: face 0 only.
    for (int f = 0; f < faces; ++f)
    {
        EXPECT_EQ(disturbanceTouchesFace(l, dist(50, 0, 0, 10.f), f), f == 0) << f;
    }
    for (int f = 0; f < faces; ++f)
    {
        EXPECT_EQ(disturbanceTouchesFace(l, dist(0, 0, -50, 10.f), f), f == 5) << f;
    }
}

TEST(PointShadowCache, a_disturbance_across_a_cube_edge_touches_both_faces)
{
    const CacheLight l = light(1, 0, 0, 0, 100.f);
    EXPECT_TRUE(disturbanceTouchesFace(l, dist(50, 50, 0, 10.f), 0));
    EXPECT_TRUE(disturbanceTouchesFace(l, dist(50, 50, 0, 10.f), 2));
    EXPECT_FALSE(disturbanceTouchesFace(l, dist(50, 50, 0, 10.f), 1));
}

TEST(PointShadowCache, a_disturbance_round_the_light_touches_every_face)
{
    const CacheLight l = light(1, 0, 0, 0, 100.f);
    for (int f = 0; f < faces; ++f)
    {
        EXPECT_TRUE(disturbanceTouchesFace(l, dist(1, 1, 1, 10.f), f)) << f;
    }
}

TEST(PointShadowCache, a_disturbance_beyond_the_far_plane_touches_nothing)
{
    const CacheLight l = light(1, 0, 0, 0, 100.f);
    const double far = farMargin * reach(100.f);
    for (int f = 0; f < faces; ++f)
    {
        EXPECT_FALSE(disturbanceTouchesFace(l, dist(far + 50.0, 0, 0, 10.f), f)) << f;
    }
    EXPECT_TRUE(disturbanceTouchesFace(l, dist(far + 5.0, 0, 0, 10.f), 0));
}

TEST(PointShadowCache, a_disturbance_redraws_the_face_it_is_in_at_the_next_step)
{
    Rig rig;
    rig.params.refreshFrames = 1000;
    const std::vector<CacheLight> lights = { light(1, 0, 0, 0) };
    rig.step(lights);
    rig.step(lights);
    rig.step(lights, { dist(50, 0, 0, 10.f) });
    EXPECT_EQ(total(rig.draw), 1);
    EXPECT_EQ(rig.draw[0], 1);
}

TEST(PointShadowCache, a_disturbance_of_an_unused_slot_does_nothing)
{
    Rig rig;
    rig.step({ CacheLight{}, light(2, 1000, 0, 0) });
    rig.step({ CacheLight{}, light(2, 1000, 0, 0) }, { dist(50, 0, 0, 10.f) });
    EXPECT_EQ(total(rig.draw), 0);
}

TEST(PointShadowCache, moving_things_cannot_exceed_the_budget_in_a_frame)
{
    Rig rig;
    rig.params.facesPerFrame = 2;
    rig.params.refreshFrames = 1000;
    const std::vector<CacheLight> lights = { light(1, 0, 0, 0), light(2, 150, 0, 0), light(3, 300, 0, 0) };
    rig.step(lights);
    for (int frame = 0; frame < 50; ++frame)
    {
        rig.step(lights, { dist(1, 1, 1, 400.f) });
        EXPECT_LE(total(rig.draw), 2) << frame;
    }
}

TEST(PointShadowCache, new_lights_do_not_take_the_budget)
{
    Rig rig;
    rig.params.facesPerFrame = 1;
    rig.step({ light(1, 0, 0, 0), light(2, 200, 0, 0) });
    EXPECT_EQ(total(rig.draw), 2 * faces);
}

TEST(PointShadowCache, a_budget_of_zero_still_draws_one_face_a_frame)
{
    Rig rig;
    rig.params.facesPerFrame = 0;
    rig.params.refreshFrames = 1000;
    const std::vector<CacheLight> lights = { light(1, 0, 0, 0) };
    rig.step(lights);
    rig.epoch = 3;
    rig.step(lights);
    EXPECT_EQ(total(rig.draw), 1);
}

TEST(PointShadowCache, a_disturbance_that_never_stops_starves_no_face)
{
    // Everything is disturbed every frame: the oldest face goes first, so each face is drawn once in
    // faces / budget frames.
    Rig rig;
    rig.params.facesPerFrame = 2;
    rig.params.refreshFrames = 1000;
    const std::vector<CacheLight> lights = { light(1, 0, 0, 0) };
    rig.step(lights);
    std::vector<int> last(faces, -1);
    for (int frame = 0; frame < 60; ++frame)
    {
        rig.step(lights, { dist(1, 1, 1, 400.f) });
        for (int f = 0; f < faces; ++f)
        {
            if (rig.draw[static_cast<std::size_t>(f)])
            {
                if (last[static_cast<std::size_t>(f)] >= 0)
                {
                    EXPECT_EQ(frame - last[static_cast<std::size_t>(f)], faces / 2);
                }
                last[static_cast<std::size_t>(f)] = frame;
            }
        }
    }
    for (int l : last)
    {
        EXPECT_GE(l, 0);
    }
}

TEST(PointShadowCache, a_still_scene_refreshes_every_face_within_the_period_plus_the_queue)
{
    Rig rig;
    rig.params.facesPerFrame = 3;
    rig.params.refreshFrames = 20;
    const std::vector<CacheLight> lights = { light(1, 0, 0, 0), light(2, 300, 0, 0), light(3, 600, 0, 0) };
    rig.step(lights);
    std::vector<int> last(lights.size() * faces, 0);
    const int bound = 20 + ceilDiv(static_cast<int>(lights.size()) * faces, 3);
    for (int frame = 1; frame < 300; ++frame)
    {
        rig.step(lights);
        for (std::size_t i = 0; i < last.size(); ++i)
        {
            if (rig.draw[i])
                last[i] = frame;
            EXPECT_LE(frame - last[i], bound) << "face " << i << " frame " << frame;
        }
    }
}

TEST(PointShadowCache, every_change_is_drawn_within_the_bound_property)
{
    std::mt19937 rng(1234);
    for (int run = 0; run < 40; ++run)
    {
        const int slotCount = 1 + static_cast<int>(rng() % 8);
        Rig rig;
        rig.params.facesPerFrame = 1 + static_cast<int>(rng() % 8);
        rig.params.refreshFrames = 5 + static_cast<std::uint32_t>(rng() % 60);
        std::vector<CacheLight> lights;
        for (int s = 0; s < slotCount; ++s)
            lights.push_back(light(static_cast<std::uint64_t>(s + 1), 200.0 * s, 0, 0, 100.f));
        rig.step(lights);
        const int bound = ceilDiv(slotCount * faces, rig.params.facesPerFrame);
        // pendingSince[i] is the first frame since which face i has a change not yet drawn, or -1.
        std::vector<int> pendingSince(static_cast<std::size_t>(slotCount * faces), -1);
        for (int frame = 1; frame < 400; ++frame)
        {
            std::vector<Disturbance> ds;
            const int n = static_cast<int>(rng() % 3);
            for (int i = 0; i < n; ++i)
            {
                const int s = static_cast<int>(rng() % static_cast<unsigned>(slotCount));
                const double dx = static_cast<double>(static_cast<int>(rng() % 161) - 80);
                const double dy = static_cast<double>(static_cast<int>(rng() % 161) - 80);
                const double dz = static_cast<double>(static_cast<int>(rng() % 161) - 80);
                ds.push_back(dist(200.0 * s + dx, dy, dz, static_cast<float>(5 + rng() % 30)));
            }
            const bool epochBump = rng() % 50 == 0;
            if (epochBump)
                ++rig.epoch;
            for (int s = 0; s < slotCount; ++s)
            {
                for (int f = 0; f < faces; ++f)
                {
                    bool touched = epochBump;
                    for (const Disturbance& d : ds)
                        touched = touched || disturbanceTouchesFace(lights[static_cast<std::size_t>(s)], d, f);
                    int& since = pendingSince[static_cast<std::size_t>(s * faces + f)];
                    if (touched && since < 0)
                        since = frame;
                }
            }
            rig.step(lights, ds);
            for (std::size_t i = 0; i < pendingSince.size(); ++i)
            {
                if (rig.draw[i])
                    pendingSince[i] = -1;
                else if (pendingSince[i] >= 0)
                {
                    ASSERT_LE(frame - pendingSince[i], bound) << "run " << run << " face " << i;
                }
            }
        }
    }
}

TEST(PointShadowCache, the_decisions_repeat_exactly)
{
    auto run = [] {
        std::mt19937 rng(99);
        Rig rig;
        rig.params.facesPerFrame = 3;
        std::vector<CacheLight> lights = { light(1, 0, 0, 0), light(2, 300, 0, 0), light(3, 0, 300, 0) };
        std::vector<std::vector<std::uint8_t>> out;
        for (int frame = 0; frame < 200; ++frame)
        {
            std::vector<Disturbance> ds;
            if (rng() % 2)
                ds.push_back(dist(static_cast<double>(rng() % 400), static_cast<double>(rng() % 400), 0, 30.f));
            if (frame % 40 == 39)
                ++rig.epoch;
            rig.step(lights, ds);
            out.push_back(rig.draw);
        }
        return out;
    };
    EXPECT_EQ(run(), run());
}

TEST(PointShadowCache, an_unused_slot_forgets_its_pending_faces)
{
    Rig rig;
    rig.params.facesPerFrame = 1;
    rig.params.refreshFrames = 1000;
    rig.step({ light(1, 0, 0, 0) });
    rig.epoch = 1;
    rig.step({ light(1, 0, 0, 0) });
    rig.step({ CacheLight{} });
    EXPECT_EQ(total(rig.draw), 0);
    rig.step({ light(1, 0, 0, 0) });
    EXPECT_EQ(count(rig.draw, 0), faces);
    rig.step({ light(1, 0, 0, 0) });
    EXPECT_EQ(total(rig.draw), 0);
}

namespace
{
    MotionTracker::Pose pose(float x, float y, float z, float rot = 1.f)
    {
        MotionTracker::Pose p{};
        p[0] = x;
        p[1] = y;
        p[2] = z;
        p[3] = rot; // the rotation matrix entries; only the first is varied
        return p;
    }

    std::vector<Disturbance> frame(
        MotionTracker& tracker, const std::vector<std::pair<std::uint64_t, MotionTracker::Pose>>& things)
    {
        tracker.begin();
        for (const auto& [key, p] : things)
            tracker.add(key, p, V3(p[0], p[1], p[2] + 64.0), 150.f);
        std::vector<Disturbance> out;
        tracker.end(out);
        return out;
    }
}

TEST(PointShadowMotionTracker, a_thing_that_stays_still_is_no_disturbance)
{
    MotionTracker tracker;
    frame(tracker, { { 1, pose(10, 20, 30) } });
    EXPECT_TRUE(frame(tracker, { { 1, pose(10, 20, 30) } }).empty());
    EXPECT_TRUE(frame(tracker, { { 1, pose(10, 20, 30) } }).empty());
}

TEST(PointShadowMotionTracker, a_move_reports_where_it_was_and_where_it_is)
{
    MotionTracker tracker;
    frame(tracker, { { 1, pose(0, 0, 0) } });
    const std::vector<Disturbance> out = frame(tracker, { { 1, pose(5, 0, 0) } });
    ASSERT_EQ(out.size(), 2u);
    const bool oldAndNew
        = (out[0].center.x == 0.0 && out[1].center.x == 5.0) || (out[0].center.x == 5.0 && out[1].center.x == 0.0);
    EXPECT_TRUE(oldAndNew);
    EXPECT_EQ(out[0].radius, 150.f);
}

TEST(PointShadowMotionTracker, a_turn_is_a_move)
{
    MotionTracker tracker;
    frame(tracker, { { 1, pose(0, 0, 0, 1.f) } });
    EXPECT_EQ(frame(tracker, { { 1, pose(0, 0, 0, 0.9f) } }).size(), 2u);
}

TEST(PointShadowMotionTracker, a_new_and_a_vanished_thing_are_reported_once_each)
{
    MotionTracker tracker;
    frame(tracker, { { 1, pose(0, 0, 0) } });
    EXPECT_EQ(frame(tracker, { { 1, pose(0, 0, 0) }, { 2, pose(100, 0, 0) } }).size(), 1u);
    const std::vector<Disturbance> gone = frame(tracker, { { 2, pose(100, 0, 0) } });
    ASSERT_EQ(gone.size(), 1u);
    EXPECT_EQ(gone[0].center.x, 0.0);
    EXPECT_TRUE(frame(tracker, { { 2, pose(100, 0, 0) } }).empty());
}

TEST(PointShadowMotionTracker, a_tiny_float_wobble_is_ignored)
{
    MotionTracker tracker;
    frame(tracker, { { 1, pose(1000.f, 0, 0) } });
    EXPECT_TRUE(frame(tracker, { { 1, pose(1000.01f, 0, 0) } }).empty());
}

TEST(PointShadowMotionTracker, the_order_things_are_added_in_does_not_matter)
{
    MotionTracker a;
    MotionTracker b;
    frame(a, { { 1, pose(0, 0, 0) }, { 2, pose(50, 0, 0) } });
    frame(b, { { 2, pose(50, 0, 0) }, { 1, pose(0, 0, 0) } });
    const auto outA = frame(a, { { 1, pose(3, 0, 0) }, { 2, pose(50, 0, 0) } });
    const auto outB = frame(b, { { 2, pose(50, 0, 0) }, { 1, pose(3, 0, 0) } });
    EXPECT_EQ(outA.size(), outB.size());
    EXPECT_EQ(outA.size(), 2u);
}

TEST(PointShadowMotionTracker, a_slow_drift_adds_up_until_it_is_reported)
{
    MotionTracker tracker;
    frame(tracker, { { 1, pose(0, 0, 0) } });
    int reported = -1;
    for (int i = 1; i <= 100 && reported < 0; ++i)
    {
        if (!frame(tracker, { { 1, pose(0.03f * static_cast<float>(i), 0, 0) } }).empty())
            reported = i;
    }
    // 0.03 a frame passes 0.5 units at frame 17, and the report is then 0.51 away from where it was.
    EXPECT_EQ(reported, 17);
    // The anchor moved to the reported pose.
    EXPECT_TRUE(frame(tracker, { { 1, pose(0.03f * 17.f, 0, 0) } }).empty());
}
