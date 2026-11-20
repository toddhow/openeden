// Task 0359 stage b: the analytic particles of Skyrim static models (components/nifscene/particleanalytic.*). Every
// expected value is worked out by hand from the model in the header; no game asset is read.

#include <gtest/gtest.h>

#include <cmath>
#include <set>

#include <components/nifscene/particleanalytic.hpp>

namespace
{
    using namespace NifScene;
    using namespace NifScene::ParticleAnalytic;

    // A system that draws something: a point emitter, 10 a second, one second of life, speed 0.
    Flat::ParticleSystem system()
    {
        Flat::ParticleSystem f;
        ParticleSystem& s = f.mSystem;
        s.mName = "test";
        s.mMaxParticles = 64;
        s.mBirthRate = 10.f;
        s.mEmitter = ParticleSystem::Emitter::Point;
        s.mLife = 1.f;
        s.mLifeVariation = 0.f;
        s.mRadius = 2.f;
        s.mSpeed = 0.f;
        f.mCentre = { 1.f, 2.f, 3.f };
        return f;
    }

    const std::array<float, 4> white{ 1.f, 1.f, 1.f, 1.f };

    // The time at which `slot` has the given age: the shader's age is fract((time + phase) / period) * period.
    double timeForAge(const Pack& p, std::uint32_t slot, double age, int cycle = 0)
    {
        const double phase = p.mEntries[kHeaderEntries + slot].mPositionSize[3];
        return age - phase + cycle * p.mPeriod + 10.0 * p.mPeriod;
    }
}

TEST(NifSceneParticleAnalytic, the_hash_is_bob_jenkins_one_at_a_time_and_the_float_is_below_one)
{
    EXPECT_EQ(hash(0u), 0u);
    EXPECT_EQ(hash(1u), 307143837u); // worked out by hand: 1 -> 1025 -> 1041 -> 9369 -> 9373 -> 307143837
    EXPECT_FLOAT_EQ(unitFloat(0u), 0.f);
    EXPECT_LT(unitFloat(0xFFFFFFFFu), 1.f);
    EXPECT_GE(unitFloat(0x400000u), 0.49f);
    EXPECT_LE(unitFloat(0x400000u), 0.51f);
}

TEST(NifSceneParticleAnalytic, a_system_with_no_birth_rate_or_no_life_packs_to_nothing)
{
    Flat::ParticleSystem f = system();
    f.mSystem.mBirthRate = 0.f;
    EXPECT_TRUE(pack(f, 1u, white).mEntries.empty());
    EXPECT_FALSE(evaluate(pack(f, 1u, white), 1.0, 0).mAlive);
}

TEST(NifSceneParticleAnalytic, the_slot_count_follows_the_birth_rate_times_the_life_and_the_pool_size)
{
    Flat::ParticleSystem f = system(); // 10 a second, 1 s: 10 slots, period 1
    Pack p = pack(f, 1u, white);
    EXPECT_EQ(p.mSlots, 10u);
    EXPECT_FLOAT_EQ(p.mPeriod, 1.f);
    EXPECT_EQ(p.mEntries.size(), kHeaderEntries + 10u);

    f.mSystem.mMaxParticles = 4; // a pool of 4 caps it; the period stays a full life so no particle is cut short
    p = pack(f, 1u, white);
    EXPECT_EQ(p.mSlots, 4u);
    EXPECT_FLOAT_EQ(p.mPeriod, 1.f);

    f.mSystem.mMaxParticles = 1000;
    f.mSystem.mBirthRate = 1000.f;
    EXPECT_EQ(pack(f, 1u, white).mSlots, kMaxSlots);
}

TEST(NifSceneParticleAnalytic, slot_entries_carry_the_flag_the_shader_tells_them_by)
{
    const Pack p = pack(system(), 5u, white);
    for (std::uint32_t i = 0; i < p.mSlots; ++i)
    {
        const Entry& e = p.mEntries[kHeaderEntries + i];
        EXPECT_FLOAT_EQ(e.mVelocityAge[3], -1.f);
        EXPECT_FLOAT_EQ(e.mPositionSize[0], static_cast<float>(i));
        EXPECT_FLOAT_EQ(e.mPositionSize[1], static_cast<float>(p.mSlots));
        EXPECT_FLOAT_EQ(e.mPositionSize[2], p.mPeriod);
        EXPECT_GE(e.mPositionSize[3], 0.f);
        EXPECT_LT(e.mPositionSize[3], p.mPeriod * 1.0001f);
    }
    EXPECT_GE(p.mEntries[0].mVelocityAge[3], 0.f); // the header is not a slot
}

TEST(NifSceneParticleAnalytic, gravity_accelerates_a_particle_from_rest_at_the_emitter_centre)
{
    Flat::ParticleSystem f = system();
    f.mGravity = { 0.f, 0.f, 100.f };
    const Pack p = pack(f, 7u, white);
    const Particle a = evaluate(p, timeForAge(p, 0, 0.5), 0);
    ASSERT_TRUE(a.mAlive);
    EXPECT_NEAR(a.mAge, 0.5f, 1e-4f);
    EXPECT_NEAR(a.mPosition[0], 1.f, 1e-3f);
    EXPECT_NEAR(a.mPosition[1], 2.f, 1e-3f);
    EXPECT_NEAR(a.mPosition[2], 3.f + 0.5f * 100.f * 0.25f, 1e-2f); // 0.5 a t^2
}

TEST(NifSceneParticleAnalytic, drag_limits_the_travel_as_an_exponential)
{
    Flat::ParticleSystem f = system();
    f.mSystem.mSpeed = 100.f; // straight up the emitter Z axis (declination 0)
    f.mSystem.mDrag = 0.1f; // 0.1 * kDragPerSecond = 3 a second
    const Pack p = pack(f, 7u, white);
    const Particle a = evaluate(p, timeForAge(p, 0, 0.5), 0);
    const float k = 0.1f * kDragPerSecond;
    EXPECT_NEAR(a.mPosition[2], 3.f + 100.f * (1.f - std::exp(-k * 0.5f)) / k, 1e-2f);
}

TEST(NifSceneParticleAnalytic, the_emitter_rotation_and_scale_carry_the_launch_direction_and_the_speed)
{
    Flat::ParticleSystem f = system();
    f.mSystem.mSpeed = 10.f;
    f.mSystemScale = 2.f; // speeds are in model units, scaled by the system
    // rotation taking local Z to the model X axis: rows are the model axes' components of local (x, y, z)
    f.mRotation = { 0.f, 0.f, 1.f, 0.f, 1.f, 0.f, -1.f, 0.f, 0.f };
    const Pack p = pack(f, 7u, white);
    const Particle a = evaluate(p, timeForAge(p, 0, 0.5), 0);
    EXPECT_NEAR(a.mPosition[0], 1.f + 10.f * 2.f * 0.5f, 1e-3f);
    EXPECT_NEAR(a.mPosition[1], 2.f, 1e-3f);
    EXPECT_NEAR(a.mPosition[2], 3.f, 1e-3f);
}

TEST(NifSceneParticleAnalytic, a_declination_of_a_quarter_turn_launches_sideways)
{
    Flat::ParticleSystem f = system();
    f.mSystem.mSpeed = 10.f;
    f.mSystem.mDeclination = 1.5707964f;
    f.mSystem.mPlanarAngle = 0.f;
    const Pack p = pack(f, 7u, white);
    const Particle a = evaluate(p, timeForAge(p, 0, 1.0 - 1e-3), 0);
    EXPECT_NEAR(a.mPosition[0], 1.f + 10.f * 0.999f, 1e-2f); // along the emitter X at planar angle 0
    EXPECT_NEAR(a.mPosition[2], 3.f, 1e-2f);
}

TEST(NifSceneParticleAnalytic, the_size_follows_the_scale_curve_over_the_life_and_the_system_scale)
{
    Flat::ParticleSystem f = system();
    f.mSystem.mScales = { 1.f, 3.f }; // 2 at the middle of a life
    f.mSystemScale = 1.5f;
    const Pack p = pack(f, 7u, white);
    EXPECT_NEAR(evaluate(p, timeForAge(p, 0, 0.0), 0).mSize, 2.f * 1.f * 1.5f, 1e-3f);
    EXPECT_NEAR(evaluate(p, timeForAge(p, 0, 0.5), 0).mSize, 2.f * 2.f * 1.5f, 1e-2f);
    EXPECT_NEAR(evaluate(p, timeForAge(p, 0, 0.999), 0).mSize, 2.f * 3.f * 1.5f, 2e-2f);
    f.mSystem.mScales.clear(); // no modifier: factor 1
    const Pack q = pack(f, 7u, white);
    EXPECT_NEAR(evaluate(q, timeForAge(q, 0, 0.5), 0).mSize, 2.f * 1.5f, 1e-4f);
}

TEST(NifSceneParticleAnalytic, the_colour_stops_blend_between_the_three_colours_and_the_tint_multiplies)
{
    Flat::ParticleSystem f = system();
    ParticleSystem& s = f.mSystem;
    s.mHasColor = true;
    s.mColors = { Vec4{ 1.f, 0.f, 0.f, 0.f }, Vec4{ 1.f, 1.f, 1.f, 1.f }, Vec4{ 0.f, 0.f, 1.f, 0.f } };
    s.mColorStops = { 0.1f, 0.5f, 0.6f, 1.f };
    const Pack p = pack(f, 7u, { 0.5f, 1.f, 1.f, 0.5f });
    auto at = [&](double u) { return evaluate(p, timeForAge(p, 0, u), 0).mColor; };
    EXPECT_NEAR(at(0.05)[0], 0.5f, 1e-3f); // colour 1 times the tint
    EXPECT_NEAR(at(0.05)[3], 0.f, 1e-3f);
    EXPECT_NEAR(at(0.3)[1], 0.5f, 1e-2f); // halfway from colour 1 to 2: green 0.5
    EXPECT_NEAR(at(0.3)[3], 0.25f, 1e-2f); // alpha 0.5 times the tint's 0.5
    EXPECT_NEAR(at(0.55)[0], 0.5f, 1e-3f); // held at colour 2 (red 1 times the tint 0.5)
    EXPECT_NEAR(at(0.55)[3], 0.5f, 1e-3f);
    EXPECT_NEAR(at(0.999)[2], 1.f, 2e-2f); // colour 3
}

TEST(NifSceneParticleAnalytic, a_colour_modifier_with_every_stop_at_zero_blends_evenly_over_the_life)
{
    Flat::ParticleSystem f = system();
    ParticleSystem& s = f.mSystem;
    s.mHasColor = true;
    s.mColors = { Vec4{ 0.f, 0.f, 0.f, 0.f }, Vec4{ 1.f, 1.f, 1.f, 1.f }, Vec4{ 0.f, 0.f, 0.f, 0.f } };
    s.mColorStops = { 0.f, 0.f, 0.f, 0.f };
    const Pack p = pack(f, 7u, white);
    EXPECT_NEAR(evaluate(p, timeForAge(p, 0, 0.25), 0).mColor[3], 0.5f, 1e-2f);
    EXPECT_NEAR(evaluate(p, timeForAge(p, 0, 0.5), 0).mColor[3], 1.f, 1e-2f);
    EXPECT_NEAR(evaluate(p, timeForAge(p, 0, 0.75), 0).mColor[3], 0.5f, 1e-2f);
}

TEST(NifSceneParticleAnalytic, fade_in_and_fade_out_scale_the_alpha_at_the_ends_of_a_life)
{
    Flat::ParticleSystem f = system();
    f.mSystem.mFadeIn = 0.2f;
    f.mSystem.mFadeOut = 0.4f;
    const Pack p = pack(f, 7u, white);
    auto alpha = [&](double u) { return evaluate(p, timeForAge(p, 0, u), 0).mColor[3]; };
    EXPECT_NEAR(alpha(0.1), 0.5f, 1e-2f);
    EXPECT_NEAR(alpha(0.5), 1.f, 1e-3f);
    EXPECT_NEAR(alpha(0.8), 0.5f, 1e-2f);
}

TEST(NifSceneParticleAnalytic, a_flipbook_steps_through_its_grid_over_the_life)
{
    Flat::ParticleSystem f = system();
    ParticleSystem& s = f.mSystem;
    s.mHasSubTexture = true;
    for (int row = 0; row < 2; ++row) // 4 columns, 2 rows, the data's order: left, width, top, height
        for (int col = 0; col < 4; ++col)
            s.mSubTextures.push_back({ col * 0.25f, 0.25f, row * 0.5f, 0.5f });
    s.mSubStart = 0.f;
    s.mSubEnd = 7.f;
    s.mSubCount = 8.f;
    const Pack p = pack(f, 7u, white);
    const Particle first = evaluate(p, timeForAge(p, 0, 0.01), 0);
    EXPECT_EQ(first.mFrame, 0u);
    EXPECT_NEAR(first.mRect[0], 0.f, 1e-5f);
    EXPECT_NEAR(first.mRect[2], 0.f, 1e-5f);
    const Particle mid = evaluate(p, timeForAge(p, 0, 0.55), 0); // frame 4: column 0, row 1
    EXPECT_EQ(mid.mFrame, 4u);
    EXPECT_NEAR(mid.mRect[0], 0.f, 1e-5f);
    EXPECT_NEAR(mid.mRect[1], 0.25f, 1e-5f);
    EXPECT_NEAR(mid.mRect[2], 0.5f, 1e-5f);
    EXPECT_NEAR(mid.mRect[3], 0.5f, 1e-5f);
    EXPECT_EQ(evaluate(p, timeForAge(p, 0, 0.99), 0).mFrame, 7u);
}

TEST(NifSceneParticleAnalytic, without_a_flipbook_the_whole_texture_is_drawn)
{
    const Pack p = pack(system(), 7u, white);
    const Particle a = evaluate(p, timeForAge(p, 0, 0.3), 0);
    EXPECT_EQ(a.mFrame, 0u);
    EXPECT_FLOAT_EQ(a.mRect[0], 0.f);
    EXPECT_FLOAT_EQ(a.mRect[1], 1.f);
    EXPECT_FLOAT_EQ(a.mRect[2], 0.f);
    EXPECT_FLOAT_EQ(a.mRect[3], 1.f);
}

TEST(NifSceneParticleAnalytic, an_irregular_atlas_is_not_cut_up)
{
    Flat::ParticleSystem f = system();
    f.mSystem.mHasSubTexture = true;
    f.mSystem.mSubTextures = { { 0.f, 0.3f, 0.f, 1.f }, { 0.3f, 0.7f, 0.f, 1.f } }; // not a regular grid
    f.mSystem.mSubEnd = 1.f;
    f.mSystem.mSubCount = 2.f;
    const Pack p = pack(f, 7u, white);
    const Particle a = evaluate(p, timeForAge(p, 0, 0.9), 0);
    EXPECT_FLOAT_EQ(a.mRect[1], 1.f);
}

TEST(NifSceneParticleAnalytic, rotation_grows_with_the_age_at_the_modifiers_speed)
{
    Flat::ParticleSystem f = system();
    f.mSystem.mRotationAngle = 0.5f;
    f.mSystem.mRotationSpeed = 2.f;
    const Pack p = pack(f, 7u, white);
    EXPECT_NEAR(evaluate(p, timeForAge(p, 0, 0.25), 0).mAngle, 0.5f + 2.f * 0.25f, 1e-3f);
}

TEST(NifSceneParticleAnalytic, volume_emitters_start_particles_inside_their_volume)
{
    Flat::ParticleSystem box = system();
    box.mSystem.mEmitter = ParticleSystem::Emitter::Box;
    box.mSystem.mEmitterSize = { 10.f, 4.f, 2.f };
    box.mCentre = { 0.f, 0.f, 0.f };
    Flat::ParticleSystem cylinder = system();
    cylinder.mSystem.mEmitter = ParticleSystem::Emitter::Cylinder;
    cylinder.mSystem.mEmitterSize = { 5.f, 8.f, 0.f };
    cylinder.mCentre = { 0.f, 0.f, 0.f };
    Flat::ParticleSystem sphere = system();
    sphere.mSystem.mEmitter = ParticleSystem::Emitter::Sphere;
    sphere.mSystem.mEmitterSize = { 3.f, 0.f, 0.f };
    sphere.mCentre = { 0.f, 0.f, 0.f };
    const Pack pb = pack(box, 3u, white), pc = pack(cylinder, 3u, white), ps = pack(sphere, 3u, white);
    std::set<float> xs;
    for (int cycle = 0; cycle < 50; ++cycle)
        for (std::uint32_t slot = 0; slot < pb.mSlots; ++slot)
        {
            const auto b = evaluate(pb, timeForAge(pb, slot, 0.0, cycle), slot).mPosition;
            EXPECT_LE(std::fabs(b[0]), 5.001f);
            EXPECT_LE(std::fabs(b[1]), 2.001f);
            EXPECT_LE(std::fabs(b[2]), 1.001f);
            xs.insert(b[0]);
            const auto c = evaluate(pc, timeForAge(pc, slot, 0.0, cycle), slot).mPosition;
            EXPECT_LE(std::hypot(c[0], c[1]), 5.001f);
            EXPECT_LE(std::fabs(c[2]), 4.001f);
            const auto s = evaluate(ps, timeForAge(ps, slot, 0.0, cycle), slot).mPosition;
            EXPECT_LE(std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]), 3.001f);
        }
    EXPECT_GT(xs.size(), 200u); // the starts are spread, not one point
}

TEST(NifSceneParticleAnalytic, a_life_with_variation_leaves_some_of_each_period_dead)
{
    Flat::ParticleSystem f = system();
    f.mSystem.mLife = 0.6f;
    f.mSystem.mLifeVariation = 0.4f; // 0.2 to 1.0 s; the period is the longest life
    const Pack p = pack(f, 11u, white);
    EXPECT_FLOAT_EQ(p.mPeriod, 1.f);
    int alive = 0, total = 0;
    for (int step = 0; step < 100; ++step)
        for (std::uint32_t slot = 0; slot < p.mSlots; ++slot)
        {
            alive += evaluate(p, 3.0 + step * 0.173, slot).mAlive ? 1 : 0;
            ++total;
        }
    const double share = static_cast<double>(alive) / total;
    EXPECT_GT(share, 0.5);
    EXPECT_LT(share, 0.7);
}

TEST(NifSceneParticleAnalytic, the_result_is_a_pure_function_of_the_arguments)
{
    Flat::ParticleSystem f = system();
    f.mSystem.mEmitter = ParticleSystem::Emitter::Box;
    f.mSystem.mEmitterSize = { 10.f, 10.f, 10.f };
    f.mSystem.mSpeed = 20.f;
    f.mSystem.mSpeedVariation = 5.f;
    f.mSystem.mDeclinationVariation = 1.f;
    const Pack a = pack(f, 99u, white), b = pack(f, 99u, white), other = pack(f, 100u, white);
    bool anyDiffers = false;
    for (std::uint32_t slot = 0; slot < a.mSlots; ++slot)
    {
        const Particle x = evaluate(a, 12.34, slot), y = evaluate(b, 12.34, slot), z = evaluate(other, 12.34, slot);
        EXPECT_EQ(x.mPosition, y.mPosition);
        EXPECT_EQ(x.mAlive, y.mAlive);
        anyDiffers = anyDiffers || x.mPosition != z.mPosition;
    }
    EXPECT_TRUE(anyDiffers); // another system seed gives another flame
    // the same particle one period later is the next cycle: the draws differ
    EXPECT_NE(evaluate(a, 12.34, 0).mPosition, evaluate(a, 12.34 + a.mPeriod, 0).mPosition);
}

TEST(NifSceneParticleAnalytic, a_slot_beyond_the_pool_is_dead)
{
    const Pack p = pack(system(), 1u, white);
    EXPECT_FALSE(evaluate(p, 1.0, p.mSlots).mAlive);
    EXPECT_FALSE(evaluate(p, 1.0, 100000u).mAlive);
}

TEST(NifSceneParticleAnalytic, the_reach_holds_every_particle)
{
    Flat::ParticleSystem f = system();
    f.mSystem.mEmitter = ParticleSystem::Emitter::Cylinder;
    f.mSystem.mEmitterSize = { 4.f, 2.f, 0.f };
    f.mSystem.mSpeed = 30.f;
    f.mSystem.mSpeedVariation = 5.f;
    f.mSystem.mDeclination = 3.14159f;
    f.mSystem.mDeclinationVariation = 3.14159f;
    f.mSystem.mPlanarAngleVariation = 6.3f;
    f.mGravity = { 0.f, 0.f, 50.f };
    const Pack p = pack(f, 5u, white);
    for (int step = 0; step < 200; ++step)
        for (std::uint32_t slot = 0; slot < p.mSlots; ++slot)
        {
            const Particle a = evaluate(p, step * 0.31, slot);
            if (!a.mAlive)
                continue;
            const float dx = a.mPosition[0] - p.mCentre[0], dy = a.mPosition[1] - p.mCentre[1],
                        dz = a.mPosition[2] - p.mCentre[2];
            EXPECT_LE(std::sqrt(dx * dx + dy * dy + dz * dz) + a.mSize, p.mReach + 1e-3f);
        }
}

TEST(NifSceneParticleAnalytic, the_header_layout_is_the_one_the_shader_reads)
{
    Flat::ParticleSystem f = system();
    f.mSystem.mEmitter = ParticleSystem::Emitter::Cylinder;
    f.mSystem.mEmitterSize = { 4.f, 2.f, 0.f };
    f.mEmitterScale = 2.f;
    f.mSystem.mSpeed = 7.f;
    f.mSystem.mDeclination = 0.25f;
    f.mSystem.mLife = 3.f;
    f.mSystem.mRadius = 5.f;
    f.mSystemScale = 1.5f;
    f.mSystem.mAspectRatio = 0.5f;
    f.mGravity = { 1.f, 2.f, 3.f };
    const Pack p = pack(f, 5u, { 1.f, 1.f, 1.f, 1.f });
    const Entry& e0 = p.mEntries[0];
    EXPECT_EQ(e0.mPositionSize, (std::array<float, 4>{ 1.f, 2.f, 3.f, 2.f })); // kind 2: cylinder
    EXPECT_EQ(e0.mColor, (std::array<float, 4>{ 8.f, 4.f, 0.f, 0.5f })); // size times the emitter scale, aspect
    EXPECT_FLOAT_EQ(e0.mVelocityAge[0], 7.f * 1.5f); // speeds carry the system scale
    EXPECT_FLOAT_EQ(e0.mVelocityAge[2], 0.25f);
    EXPECT_FLOAT_EQ(e0.mMaxAge[2], 3.f);
    const Entry& e1 = p.mEntries[1];
    EXPECT_FLOAT_EQ(e1.mPositionSize[0], 1.f); // rotation column 0 is the model X axis for the identity
    EXPECT_FLOAT_EQ(e1.mColor[1], 1.f);
    EXPECT_FLOAT_EQ(e1.mVelocityAge[2], 1.f);
    EXPECT_FLOAT_EQ(e1.mPositionSize[3], 5.f * 1.5f);
    EXPECT_EQ(p.mEntries[2].mPositionSize[2], 3.f); // gravity z
}
