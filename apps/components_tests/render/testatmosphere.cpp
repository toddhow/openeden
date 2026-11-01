// Tests for the atmosphere maths, Render::Atmosphere (task 0034). Written from the task spec before the header
// existed.

#include <components/render/atmospheremath.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace Render::Atmosphere
{
    namespace
    {
        constexpr float deg = 3.14159265358979f / 180.f;

        float luma(const vsg::vec3& c)
        {
            return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
        }

        bool finite(const vsg::vec3& c)
        {
            return std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z);
        }

        float maxDifference(const vsg::vec3& a, const vsg::vec3& b)
        {
            return std::max({ std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z) });
        }

        // Some arbitrary vanilla colours, as the weather manager would hand them over.
        Colours vanilla()
        {
            Colours c;
            c.sky = vsg::vec3(0.35f, 0.5f, 0.8f);
            c.ambient = vsg::vec3(0.4f, 0.4f, 0.45f);
            c.sun = vsg::vec3(1.f, 0.95f, 0.8f);
            c.horizon = vsg::vec3(0.6f, 0.65f, 0.7f);
            c.cloudTint = vsg::vec3(1.f, 1.f, 1.f);
            return c;
        }

        void expectSame(const vsg::vec3& a, const vsg::vec3& b)
        {
            EXPECT_EQ(a.x, b.x);
            EXPECT_EQ(a.y, b.y);
            EXPECT_EQ(a.z, b.z);
        }

        void expectSame(const Colours& a, const Colours& b)
        {
            expectSame(a.sky, b.sky);
            expectSame(a.ambient, b.ambient);
            expectSame(a.sun, b.sun);
            expectSame(a.horizon, b.horizon);
            expectSame(a.cloudTint, b.cloudTint);
        }
    }

    TEST(RenderAtmosphereTest, needs_the_setting_and_hdr)
    {
        EXPECT_TRUE(useAtmosphere(true, true));
        EXPECT_FALSE(useAtmosphere(false, true));
        EXPECT_FALSE(useAtmosphere(true, false));
        EXPECT_FALSE(useAtmosphere(false, false));
    }

    TEST(RenderAtmosphereTest, look_names_parse_and_unknown_is_vanilla)
    {
        EXPECT_EQ(parseLook("vanilla"), Look::Vanilla);
        EXPECT_EQ(parseLook("physical"), Look::Physical);
        EXPECT_EQ(parseLook("nonsense"), Look::Vanilla);
        EXPECT_EQ(parseLook(""), Look::Vanilla);
    }

    TEST(RenderAtmosphereTest, sun_elevation_is_the_angle_above_the_horizon)
    {
        EXPECT_NEAR(sunElevation(vsg::vec3(0.f, 0.f, 1.f), false), 90.f * deg, 1e-5f);
        EXPECT_NEAR(sunElevation(vsg::vec3(1.f, 0.f, 0.f), false), 0.f, 1e-5f);
        EXPECT_NEAR(sunElevation(vsg::vec3(1.f, 0.f, 1.f), false), 45.f * deg, 1e-4f);
        // the length does not matter
        EXPECT_NEAR(sunElevation(vsg::vec3(0.f, 0.f, 7.f), false), 90.f * deg, 1e-5f);
    }

    TEST(RenderAtmosphereTest, the_mirrored_night_sun_is_below_the_horizon)
    {
        EXPECT_NEAR(sunElevation(vsg::vec3(1.f, 0.f, 1.f), true), -45.f * deg, 1e-4f);
        EXPECT_LT(sunElevation(vsg::vec3(0.3f, -0.268f, 0.5f), true), 0.f);
    }

    TEST(RenderAtmosphereTest, air_mass_is_one_at_the_zenith_and_about_38_at_the_horizon)
    {
        EXPECT_NEAR(airMass(90.f * deg), 1.f, 0.01f);
        EXPECT_NEAR(airMass(0.f), 38.f, 1.f);
        // below the horizon it is clamped, not extrapolated
        EXPECT_FLOAT_EQ(airMass(-10.f * deg), airMass(0.f));
    }

    TEST(RenderAtmosphereTest, air_mass_grows_as_the_sun_sinks)
    {
        float last = airMass(90.f * deg);
        for (int e = 85; e >= 0; e -= 5)
        {
            const float m = airMass(static_cast<float>(e) * deg);
            EXPECT_GT(m, last) << e;
            last = m;
        }
    }

    TEST(RenderAtmosphereTest, blue_light_is_lost_first)
    {
        for (int e = 0; e <= 90; e += 10)
        {
            const auto t = sunTransmittance(static_cast<float>(e) * deg, 2.f);
            EXPECT_GE(t.x, t.y) << e;
            EXPECT_GE(t.y, t.z) << e;
            EXPECT_GT(t.z, 0.f) << e;
            EXPECT_LE(t.x, 1.f) << e;
        }
    }

    TEST(RenderAtmosphereTest, a_low_sun_is_redder_than_a_high_one)
    {
        const auto low = sunTransmittance(3.f * deg, 2.f);
        const auto high = sunTransmittance(60.f * deg, 2.f);
        EXPECT_LT(low.z / low.x, high.z / high.x);
        EXPECT_LT(low.z / low.x, 0.5f);
        EXPECT_GT(high.z / high.x, 0.7f);
    }

    TEST(RenderAtmosphereTest, hazier_air_lets_less_sun_through)
    {
        EXPECT_GT(luma(sunTransmittance(30.f * deg, 1.f)), luma(sunTransmittance(30.f * deg, 6.f)));
    }

    TEST(RenderAtmosphereTest, the_day_weight_runs_from_night_to_day)
    {
        EXPECT_EQ(dayWeight(-20.f * deg), 0.f);
        EXPECT_EQ(dayWeight(-4.f * deg), 0.f);
        EXPECT_EQ(dayWeight(6.f * deg), 1.f);
        EXPECT_EQ(dayWeight(80.f * deg), 1.f);
        float last = 0.f;
        for (int e = -4; e <= 6; ++e)
        {
            const float w = dayWeight(static_cast<float>(e) * deg);
            EXPECT_GE(w, last) << e;
            EXPECT_LE(w, 1.f) << e;
            last = w;
        }
        EXPECT_NEAR(dayWeight(1.f * deg), 0.5f, 0.01f);
    }

    TEST(RenderAtmosphereTest, a_clear_noon_sky_is_blue)
    {
        const auto m = model(70.f * deg, 1.f);
        EXPECT_GT(m.sky.z, m.sky.x);
        EXPECT_GT(m.sky.z, m.sky.y);
    }

    TEST(RenderAtmosphereTest, the_horizon_at_sunset_is_warmer_than_the_zenith)
    {
        const auto m = model(2.f * deg, 1.f);
        EXPECT_GT(m.horizon.x / m.horizon.z, m.sky.x / m.sky.z);
        EXPECT_GT(m.horizon.x, m.horizon.z);
    }

    TEST(RenderAtmosphereTest, the_sun_light_is_dimmer_and_redder_when_low)
    {
        const auto low = model(4.f * deg, 1.f);
        const auto high = model(60.f * deg, 1.f);
        EXPECT_LT(luma(low.sun), luma(high.sun));
        EXPECT_GT(low.sun.x / low.sun.z, high.sun.x / high.sun.z);
    }

    TEST(RenderAtmosphereTest, the_sky_brightens_with_the_sun)
    {
        float last = 0.f;
        for (int e = 0; e <= 60; e += 10)
        {
            const float l = luma(model(static_cast<float>(e) * deg, 1.f).sky);
            EXPECT_GE(l, last) << e;
            last = l;
        }
    }

    TEST(RenderAtmosphereTest, haze_takes_the_colour_out_of_the_sky)
    {
        const auto clear = model(50.f * deg, 1.f);
        const auto hazy = model(50.f * deg, 0.1f);
        EXPECT_LT(hazy.sky.z - hazy.sky.x, clear.sky.z - clear.sky.x);
    }

    TEST(RenderAtmosphereTest, the_model_is_finite_and_bounded_everywhere)
    {
        for (int e = -90; e <= 90; e += 3)
            for (int c = 0; c <= 10; ++c)
            {
                const auto m = model(static_cast<float>(e) * deg, static_cast<float>(c) / 10.f);
                for (const auto& v : { m.sky, m.horizon, m.ambient, m.sun, m.cloudTint })
                {
                    ASSERT_TRUE(finite(v)) << e << " " << c;
                    EXPECT_GE(std::min({ v.x, v.y, v.z }), 0.f) << e << " " << c;
                    EXPECT_LE(std::max({ v.x, v.y, v.z }), 2.f) << e << " " << c;
                }
            }
    }

    TEST(RenderAtmosphereTest, night_gives_back_the_vanilla_colours_exactly)
    {
        for (const Look look : { Look::Vanilla, Look::Physical })
        {
            const Colours v = vanilla();
            expectSame(applyLook(v, -20.f * deg, 1.f, look), v);
        }
    }

    TEST(RenderAtmosphereTest, no_clearness_gives_back_the_vanilla_colours_exactly)
    {
        for (const Look look : { Look::Vanilla, Look::Physical })
        {
            const Colours v = vanilla();
            expectSame(applyLook(v, 40.f * deg, 0.f, look), v);
        }
    }

    TEST(RenderAtmosphereTest, the_vanilla_look_keeps_each_colours_brightness)
    {
        const Colours v = vanilla();
        for (int e = 8; e <= 80; e += 12)
        {
            const Colours out = applyLook(v, static_cast<float>(e) * deg, 1.f, Look::Vanilla);
            EXPECT_NEAR(luma(out.sky), luma(v.sky), 2e-3f) << e;
            EXPECT_NEAR(luma(out.ambient), luma(v.ambient), 2e-3f) << e;
            EXPECT_NEAR(luma(out.sun), luma(v.sun), 2e-3f) << e;
        }
    }

    TEST(RenderAtmosphereTest, the_vanilla_look_keeps_the_brightness_with_a_low_sun_too)
    {
        // A low sun is red; scaling it up to a white sun's brightness must not clip a channel and lose brightness.
        const Colours v = vanilla();
        for (int tenth = 5; tenth <= 80; tenth += 5)
        {
            const Colours out = applyLook(v, static_cast<float>(tenth) * 0.1f * deg, 1.f, Look::Vanilla);
            EXPECT_NEAR(luma(out.sun), luma(v.sun), 2e-3f) << tenth;
            EXPECT_LE(std::max({ out.sun.x, out.sun.y, out.sun.z }), 1.0001f) << tenth;
        }
    }

    TEST(RenderAtmosphereTest, the_vanilla_look_stays_nearer_to_the_weather_than_the_physical_one)
    {
        const Colours v = vanilla();
        for (int e = 10; e <= 70; e += 20)
        {
            const Colours soft = applyLook(v, static_cast<float>(e) * deg, 1.f, Look::Vanilla);
            const Colours hard = applyLook(v, static_cast<float>(e) * deg, 1.f, Look::Physical);
            EXPECT_LE(maxDifference(soft.sky, v.sky), maxDifference(hard.sky, v.sky)) << e;
            EXPECT_LE(maxDifference(soft.sun, v.sun), maxDifference(hard.sun, v.sun)) << e;
        }
    }

    TEST(RenderAtmosphereTest, the_physical_look_reddens_a_clear_sunset)
    {
        const Colours v = vanilla();
        const Colours out = applyLook(v, 3.f * deg, 1.f, Look::Physical);
        EXPECT_GT(out.sun.x / out.sun.z, v.sun.x / v.sun.z);
    }

    TEST(RenderAtmosphereTest, the_result_changes_smoothly_with_the_hour)
    {
        const Colours v = vanilla();
        for (const Look look : { Look::Vanilla, Look::Physical })
        {
            Colours last = applyLook(v, -10.f * deg, 1.f, look);
            for (int tenth = -99; tenth <= 900; ++tenth)
            {
                const Colours now = applyLook(v, static_cast<float>(tenth) * 0.1f * deg, 1.f, look);
                ASSERT_LT(maxDifference(now.sky, last.sky), 0.03f) << tenth;
                ASSERT_LT(maxDifference(now.sun, last.sun), 0.03f) << tenth;
                ASSERT_LT(maxDifference(now.ambient, last.ambient), 0.03f) << tenth;
                last = now;
            }
        }
    }
}
