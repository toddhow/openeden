// Tests for the snow and ash cover maths, Render::Cover (task 0039, stage a). Written from the task spec before the
// implementation: the build and melt curves (bounded, monotonic, ash fades slower than snow melts), how upward a
// surface is (walls get nothing, flat tops everything), the ragged edge, the kinds' colours and roughness, the packing
// of the cover into the scene block's spare float, and that the shader twin (files/shaders/lib/material/cover.glsl)
// holds the same constants. Pure CPU maths: no device, window or game data.

#include <components/render/covermath.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <regex>
#include <sstream>
#include <string>

namespace
{
    using namespace Render::Cover;

    TEST(RenderCover, weatherKindFollowsTheParticleKind)
    {
        EXPECT_EQ(kindOf(false, false), Kind::None);
        EXPECT_EQ(kindOf(true, false), Kind::Snow);
        EXPECT_EQ(kindOf(false, true), Kind::Ash);
    }

    TEST(RenderCover, weatherKindPrefersSnowIfBothAreSet)
    {
        EXPECT_EQ(kindOf(true, true), Kind::Snow);
    }

    TEST(RenderCover, buildRisesAtOneOverBuildSeconds)
    {
        State state;
        state = step(state, Kind::Snow, 1.f, 300.f, 1200.f, 7200.f);
        EXPECT_NEAR(state.snow, 0.25f, 1e-5f);
        EXPECT_FLOAT_EQ(state.ash, 0.f);
    }

    TEST(RenderCover, buildStopsAtTheTargetAndAtOne)
    {
        State state;
        state = step(state, Kind::Snow, 0.4f, 100000.f, 1200.f, 7200.f);
        EXPECT_FLOAT_EQ(state.snow, 0.4f);
        state = step(state, Kind::Snow, 5.f, 100000.f, 1200.f, 7200.f);
        EXPECT_FLOAT_EQ(state.snow, 1.f);
    }

    TEST(RenderCover, snowMeltsAtOneOverMeltSeconds)
    {
        State state;
        state.snow = 1.f;
        state = step(state, Kind::None, 1.f, 3600.f, 1200.f, 7200.f);
        EXPECT_NEAR(state.snow, 0.5f, 1e-5f);
    }

    TEST(RenderCover, ashFadesSlowerThanSnowMelts)
    {
        State snowy;
        snowy.snow = 1.f;
        State ashy;
        ashy.ash = 1.f;
        snowy = step(snowy, Kind::None, 0.f, 1800.f, 1200.f, 7200.f);
        ashy = step(ashy, Kind::None, 0.f, 1800.f, 1200.f, 7200.f);
        EXPECT_LT(snowy.snow, ashy.ash);
        EXPECT_NEAR(ashy.ash, 1.f - 1800.f / (7200.f * ashFadeFactor), 1e-5f);
    }

    TEST(RenderCover, meltStopsAtZero)
    {
        State state;
        state.snow = 0.2f;
        state.ash = 0.1f;
        state = step(state, Kind::None, 0.f, 1.0e7f, 1200.f, 7200.f);
        EXPECT_FLOAT_EQ(state.snow, 0.f);
        EXPECT_FLOAT_EQ(state.ash, 0.f);
    }

    TEST(RenderCover, snowingDoesNotBuildAshAndTheOtherWayRound)
    {
        State state;
        state = step(state, Kind::Snow, 1.f, 600.f, 1200.f, 7200.f);
        EXPECT_GT(state.snow, 0.f);
        EXPECT_FLOAT_EQ(state.ash, 0.f);
        State other;
        other = step(other, Kind::Ash, 1.f, 600.f, 1200.f, 7200.f);
        EXPECT_GT(other.ash, 0.f);
        EXPECT_FLOAT_EQ(other.snow, 0.f);
    }

    TEST(RenderCover, snowMeltsWhileAshBuilds)
    {
        State state;
        state.snow = 0.5f;
        const State next = step(state, Kind::Ash, 1.f, 600.f, 1200.f, 7200.f);
        EXPECT_LT(next.snow, 0.5f);
        EXPECT_GT(next.ash, 0.f);
    }

    TEST(RenderCover, stepIsMonotonicTowardsTheTargetAndBounded)
    {
        State state;
        float last = 0.f;
        for (int i = 0; i < 200; ++i)
        {
            state = step(state, Kind::Snow, 1.f, 30.f, 1200.f, 7200.f);
            EXPECT_GE(state.snow, last);
            EXPECT_LE(state.snow, 1.f);
            last = state.snow;
        }
        EXPECT_FLOAT_EQ(last, 1.f);
        for (int i = 0; i < 2000; ++i)
        {
            state = step(state, Kind::None, 0.f, 30.f, 1200.f, 7200.f);
            EXPECT_LE(state.snow, last);
            EXPECT_GE(state.snow, 0.f);
            last = state.snow;
        }
        EXPECT_FLOAT_EQ(last, 0.f);
    }

    TEST(RenderCover, stepKeepsTheStateWhenTimeDoesNotAdvance)
    {
        State state;
        state.snow = 0.3f;
        state.ash = 0.2f;
        for (float dt : { 0.f, -5.f, std::numeric_limits<float>::quiet_NaN() })
        {
            const State next = step(state, Kind::Snow, 1.f, dt, 1200.f, 7200.f);
            EXPECT_FLOAT_EQ(next.snow, 0.3f);
            EXPECT_FLOAT_EQ(next.ash, 0.2f);
        }
    }

    TEST(RenderCover, stepSurvivesBadTimesAndBadState)
    {
        State state;
        state.snow = std::numeric_limits<float>::quiet_NaN();
        state.ash = 7.f;
        const State next = step(state, Kind::None, 0.f, 10.f, 0.f, 0.f);
        EXPECT_GE(next.snow, 0.f);
        EXPECT_LE(next.snow, 1.f);
        EXPECT_GE(next.ash, 0.f);
        EXPECT_LE(next.ash, 1.f);
    }

    TEST(RenderCover, precipitationAlphaCapsTheTarget)
    {
        State state;
        state = step(state, Kind::Snow, 0.25f, 100000.f, 1200.f, 7200.f);
        EXPECT_FLOAT_EQ(state.snow, 0.25f);
    }

    TEST(RenderCover, dominantPicksTheLargerAndSnowOnATie)
    {
        State state;
        EXPECT_EQ(dominant(state).kind, Kind::None);
        EXPECT_FLOAT_EQ(dominant(state).amount, 0.f);
        state.snow = 0.4f;
        EXPECT_EQ(dominant(state).kind, Kind::Snow);
        EXPECT_FLOAT_EQ(dominant(state).amount, 0.4f);
        state.ash = 0.7f;
        EXPECT_EQ(dominant(state).kind, Kind::Ash);
        EXPECT_FLOAT_EQ(dominant(state).amount, 0.7f);
        state.ash = 0.4f;
        EXPECT_EQ(dominant(state).kind, Kind::Snow);
    }

    TEST(RenderCover, upwardnessIsZeroOnWallsAndOnCeilings)
    {
        EXPECT_FLOAT_EQ(upwardness(0.f), 0.f);
        EXPECT_FLOAT_EQ(upwardness(-1.f), 0.f);
        EXPECT_FLOAT_EQ(upwardness(0.3f), 0.f);
    }

    TEST(RenderCover, upwardnessIsOneOnFlatTops)
    {
        EXPECT_FLOAT_EQ(upwardness(1.f), 1.f);
        EXPECT_FLOAT_EQ(upwardness(0.95f), 1.f);
    }

    TEST(RenderCover, upwardnessMostlyCoversA45DegreeSlope)
    {
        // The spec's D3: slopes up to about 45 degrees gather cover.
        const float u = upwardness(std::cos(0.7853981634f));
        EXPECT_GT(u, 0.5f);
        EXPECT_LT(u, 1.f);
    }

    TEST(RenderCover, upwardnessGrowsWithNormalZ)
    {
        float last = 0.f;
        for (int i = 0; i <= 100; ++i)
        {
            const float u = upwardness(-0.2f + 1.4f * static_cast<float>(i) / 100.f);
            EXPECT_GE(u, last);
            EXPECT_GE(u, 0.f);
            EXPECT_LE(u, 1.f);
            last = u;
        }
    }

    TEST(RenderCover, noiseIsInRangeAndRepeatable)
    {
        for (int i = 0; i < 400; ++i)
        {
            const float x = static_cast<float>(i) * 13.7f - 2000.f;
            const float y = static_cast<float>(i) * -7.3f + 500.f;
            const float n = coverNoise(x, y);
            EXPECT_GE(n, 0.f);
            EXPECT_LT(n, 1.f);
            EXPECT_FLOAT_EQ(n, coverNoise(x, y));
        }
    }

    TEST(RenderCover, noiseVaries)
    {
        float lo = 1.f;
        float hi = 0.f;
        for (int i = 0; i < 400; ++i)
        {
            const float n = coverNoise(static_cast<float>(i) * 11.f, static_cast<float>(i) * 5.f);
            lo = std::min(lo, n);
            hi = std::max(hi, n);
        }
        EXPECT_LT(lo, 0.25f);
        EXPECT_GT(hi, 0.75f);
    }

    TEST(RenderCover, maskIsZeroWithoutCoverOnAWallOrUnderAnEave)
    {
        for (float n : { 0.f, 0.3f, 0.6f, 0.999f })
        {
            EXPECT_FLOAT_EQ(coverMask(0.f, 1.f, 1.f, n), 0.f);
            EXPECT_FLOAT_EQ(coverMask(1.f, 0.f, 1.f, n), 0.f);
            EXPECT_FLOAT_EQ(coverMask(1.f, 1.f, 0.f, n), 0.f);
        }
    }

    TEST(RenderCover, maskIsFullOnFlatOpenTopsWithFullCover)
    {
        for (float n : { 0.f, 0.3f, 0.6f, 0.999999f })
            EXPECT_FLOAT_EQ(coverMask(1.f, 1.f, 1.f, n), 1.f);
    }

    TEST(RenderCover, maskGrowsWithTheCoverAmount)
    {
        for (float n : { 0.1f, 0.5f, 0.9f })
        {
            float last = 0.f;
            for (int i = 0; i <= 100; ++i)
            {
                const float m = coverMask(static_cast<float>(i) / 100.f, 1.f, 1.f, n);
                EXPECT_GE(m, last - 1e-6f);
                EXPECT_GE(m, 0.f);
                EXPECT_LE(m, 1.f);
                last = m;
            }
        }
    }

    TEST(RenderCover, maskEdgeIsRaggedAtHalfCover)
    {
        // At half cover some places (low noise) are covered and others (high noise) are bare.
        EXPECT_GT(coverMask(0.5f, 1.f, 1.f, 0.05f), 0.9f);
        EXPECT_LT(coverMask(0.5f, 1.f, 1.f, 0.95f), 0.1f);
    }

    TEST(RenderCover, maskSurvivesBadInput)
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FLOAT_EQ(coverMask(nan, 1.f, 1.f, 0.5f), 0.f);
        EXPECT_FLOAT_EQ(coverMask(5.f, 5.f, 5.f, 0.5f), 1.f);
        EXPECT_FLOAT_EQ(coverMask(-1.f, 1.f, 1.f, 0.5f), 0.f);
    }

    TEST(RenderCover, kindsDifferInColourAndRoughness)
    {
        const Rgb snow = colour(Kind::Snow);
        const Rgb ash = colour(Kind::Ash);
        EXPECT_GT(snow.r + snow.g + snow.b, 2.f * (ash.r + ash.g + ash.b));
        EXPECT_GT(snow.b, 0.f);
        EXPECT_LT(ash.r, 0.3f);
        EXPECT_GT(roughness(Kind::Snow), 0.f);
        EXPECT_LE(roughness(Kind::Snow), 1.f);
        EXPECT_GT(roughness(Kind::Ash), roughness(Kind::Snow));
        EXPECT_LE(roughness(Kind::Ash), 1.f);
    }

    TEST(RenderCover, noKindHasNoColourEffect)
    {
        const Rgb none = colour(Kind::None);
        EXPECT_FLOAT_EQ(none.r, 0.f);
        EXPECT_FLOAT_EQ(none.g, 0.f);
        EXPECT_FLOAT_EQ(none.b, 0.f);
    }

    TEST(RenderCover, packingWithoutCoverIsExactlyTheOldWetnessEncoding)
    {
        for (float wet : { 0.f, 0.25f, 0.5f, 1.f })
            EXPECT_EQ(pack(wet, Kind::None, 0.f), 1.f + wet);
        // A kind with nothing on the ground packs as no cover.
        EXPECT_EQ(pack(0.5f, Kind::Snow, 0.f), 1.5f);
    }

    TEST(RenderCover, packingRoundTripsWetnessAndCover)
    {
        for (Kind kind : { Kind::Snow, Kind::Ash })
        {
            for (int a = 1; a <= 100; ++a)
            {
                const float amount = static_cast<float>(a) / 100.f;
                for (float wet : { 0.f, 0.01f, 0.5f, 0.99f, 1.f })
                {
                    const Unpacked u = unpack(pack(wet, kind, amount));
                    EXPECT_EQ(u.kind, kind);
                    EXPECT_NEAR(u.amount, amount, 0.5f / coverSteps + 1e-6f);
                    EXPECT_NEAR(u.wetness, wet, 1e-3f);
                }
            }
        }
    }

    TEST(RenderCover, packingFullAmountDecodesToOne)
    {
        EXPECT_FLOAT_EQ(unpack(pack(0.f, Kind::Snow, 1.f)).amount, 1.f);
        EXPECT_FLOAT_EQ(unpack(pack(0.f, Kind::Ash, 1.f)).amount, 1.f);
    }

    TEST(RenderCover, unpackingTheOldEncodingGivesWetnessAndNoCover)
    {
        for (float w : { 1.f, 1.3f, 2.f })
        {
            const Unpacked u = unpack(w);
            EXPECT_EQ(u.kind, Kind::None);
            EXPECT_FLOAT_EQ(u.amount, 0.f);
            EXPECT_FLOAT_EQ(u.wetness, w - 1.f);
        }
    }

    TEST(RenderCover, otherViewsAlphaMeansNoWetnessAndNoCover)
    {
        // The map, preview and editor views leave the alpha at 1 or 0 (lib/view/data.glsl).
        for (float w : { 0.f, 1.f })
        {
            const Unpacked u = unpack(w);
            EXPECT_EQ(u.kind, Kind::None);
            EXPECT_FLOAT_EQ(u.amount, 0.f);
            EXPECT_FLOAT_EQ(u.wetness, 0.f);
        }
    }

    TEST(RenderCover, unpackingNaNIsDryAndBare)
    {
        const Unpacked u = unpack(std::numeric_limits<float>::quiet_NaN());
        EXPECT_EQ(u.kind, Kind::None);
        EXPECT_FLOAT_EQ(u.amount, 0.f);
        EXPECT_FLOAT_EQ(u.wetness, 0.f);
    }

    // The shader twin: every constant of Render::Cover that the shader repeats is read back from the GLSL text.
    std::map<std::string, float> readShaderConstants()
    {
        std::map<std::string, float> out;
        const std::filesystem::path source = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".."
            / "files" / "shaders" / "lib" / "material" / "cover.glsl";
        std::ifstream in(source);
        if (!in)
            return out;
        std::stringstream text;
        text << in.rdbuf();
        const std::string all = text.str();
        const std::regex line(R"(const\s+(?:float|int)\s+(\w+)\s*=\s*([-+0-9.eE]+)\s*;)");
        for (auto it = std::sregex_iterator(all.begin(), all.end(), line); it != std::sregex_iterator(); ++it)
            out[(*it)[1]] = std::stof((*it)[2]);
        return out;
    }

    TEST(RenderCover, shaderTwinHoldsTheSameConstants)
    {
        const auto c = readShaderConstants();
        if (c.empty())
            GTEST_SKIP() << "the shader source is not beside the test sources";
        const auto expect = [&c](const char* name, float value) {
            ASSERT_TRUE(c.count(name)) << name;
            EXPECT_FLOAT_EQ(c.at(name), value) << name;
        };
        expect("coverUpFrom", upFrom);
        expect("coverUpTo", upTo);
        expect("coverEdge", edge);
        expect("coverNoiseCell", noiseCell);
        expect("coverSteps", coverSteps);
        expect("coverSnowR", colour(Kind::Snow).r);
        expect("coverSnowG", colour(Kind::Snow).g);
        expect("coverSnowB", colour(Kind::Snow).b);
        expect("coverAshR", colour(Kind::Ash).r);
        expect("coverAshG", colour(Kind::Ash).g);
        expect("coverAshB", colour(Kind::Ash).b);
        expect("coverSnowRoughness", roughness(Kind::Snow));
        expect("coverAshRoughness", roughness(Kind::Ash));
    }
}
