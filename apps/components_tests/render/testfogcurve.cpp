// Task 0355 (follow-up of stage d): the fog curve of a game pack interior and how it rides in scene.fogColor.w
// (components/render/fogcurve.hpp, twin of lib/view/fog.glsl and fog.slang). Every expected number is worked out by hand.
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>

#include <components/render/fogcurve.hpp>

namespace
{
    using namespace Render::FogCurve;

    TEST(RenderFogCurve, theLinearCurveLeavesTheMarkerExactlyAsItWas)
    {
        // Morrowind and every view but a pack interior: the marker is bit for bit 1 or 2.
        EXPECT_EQ(encodeMarker(1.f, Curve{}), 1.f);
        EXPECT_EQ(encodeMarker(2.f, Curve{}), 2.f);
        EXPECT_EQ(encodeMarker(2.f, Curve{ 1.f, 1.f }), 2.f);
        EXPECT_EQ(encodeMarker(2.f, Curve{ std::nanf(""), 0.5f }), 2.f);
        EXPECT_EQ(encodeMarker(2.f, Curve{ 0.f, 0.5f }), 2.f);
    }

    TEST(RenderFogCurve, aCurveIsCodedByHandAndReadBack)
    {
        // power 0.7 -> round(44.8) = 45, max 0.8 -> round(204) = 204: code 45 * 256 + 204 = 11724.
        const float w = encodeMarker(2.f, Curve{ 0.7f, 0.8f });
        EXPECT_EQ(w, 2.f + 11724.f / 131072.f);
        const Curve c = decode(w);
        EXPECT_FLOAT_EQ(c.power, 45.f / 64.f);
        EXPECT_FLOAT_EQ(c.max, 204.f / 255.f);
        // the same with the non-HDR marker
        const Curve d = decode(encodeMarker(1.f, Curve{ 0.7f, 0.8f }));
        EXPECT_EQ(d, c);
    }

    TEST(RenderFogCurve, aCodedMarkerStaysOnItsSideOf1Point5)
    {
        // Every reader of the marker tests > 1.5 (lib/view/linear.glsl and the sky shaders): the code never moves it.
        for (float power : { 1.f / 64.f, 0.4f, 0.7f, 2.5f, 3.98f })
            for (float max : { 1.f / 255.f, 0.5f, 1.f })
            {
                EXPECT_GT(encodeMarker(2.f, Curve{ power, max }), 1.5f);
                EXPECT_LT(encodeMarker(1.f, Curve{ power, max }), 1.5f);
                EXPECT_GE(encodeMarker(1.f, Curve{ power, max }), 1.f);
            }
        // the largest code, 255 * 256 + 255, is still under a half
        EXPECT_LT(65535.f / 131072.f, 0.5f);
    }

    TEST(RenderFogCurve, everyCodeRoundTripsExactlyAtBothMarkers)
    {
        for (int p = 1; p <= 255; p += 7)
            for (int m = 1; m <= 255; m += 5)
                for (float marker : { 1.f, 2.f })
                {
                    const float w = marker + static_cast<float>(p * 256 + m) / kCodeScale;
                    const Curve c = decode(w);
                    EXPECT_FLOAT_EQ(c.power, static_cast<float>(p) / kPowerScale) << p << " " << m;
                    EXPECT_FLOAT_EQ(c.max, static_cast<float>(m) / kMaxScale) << p << " " << m;
                }
    }

    TEST(RenderFogCurve, aMaxOfOneAndAPowerOfOneAreExactAfterTheRoundTrip)
    {
        // max 1 is code 255 and power 1 is code 64: the half that is plain linear must stay exactly 1
        const Curve onlyPower = decode(encodeMarker(2.f, Curve{ 0.7f, 1.f }));
        EXPECT_EQ(onlyPower.max, 1.f);
        const Curve onlyMax = decode(encodeMarker(2.f, Curve{ 1.f, 0.8f }));
        EXPECT_EQ(onlyMax.power, 1.f);
    }

    TEST(RenderFogCurve, otherViewsAndPlainMarkersDecodeAsLinear)
    {
        for (float w : { 0.f, 0.3f, 0.99f, 1.f, 2.f })
            EXPECT_EQ(decode(w), Curve{}) << w;
    }

    TEST(RenderFogCurve, theQuantisationClampsToOneCodeAtLeast)
    {
        // a tiny power or max would round to code 0 ("none"), so they are held at code 1
        const Curve c = decode(encodeMarker(2.f, Curve{ 0.001f, 0.0001f }));
        EXPECT_FLOAT_EQ(c.power, 1.f / 64.f);
        EXPECT_FLOAT_EQ(c.max, 1.f / 255.f);
        // above the range: held at code 255 (power 3.984375)
        EXPECT_FLOAT_EQ(decode(encodeMarker(2.f, Curve{ 50.f, 1.f })).power, 255.f / 64.f);
    }

    TEST(RenderFogCurve, theShareIsPowerThenMax)
    {
        // pow(0.25, 0.5) = 0.5, times 0.8 = 0.4
        EXPECT_FLOAT_EQ(apply(0.25f, Curve{ 0.5f, 0.8f }), 0.4f);
        EXPECT_FLOAT_EQ(apply(0.f, Curve{ 0.7f, 0.8f }), 0.f);
        EXPECT_FLOAT_EQ(apply(1.f, Curve{ 0.7f, 0.8f }), 0.8f);
        EXPECT_FLOAT_EQ(apply(0.5f, Curve{ 1.f, 1.f }), 0.5f);
        EXPECT_FLOAT_EQ(apply(-1.f, Curve{ 0.7f, 1.f }), 0.f);
    }

    // The shader twins: their constants are read back from the text of both files.
    TEST(RenderFogCurve, theShaderTwinsHoldTheSameConstantsAndTheSameBranches)
    {
        for (const char* name : { "fog.glsl", "fog.slang" })
        {
            const std::filesystem::path source = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".."
                / "files" / "shaders" / "lib" / "view" / name;
            std::ifstream in(source);
            if (!in)
                GTEST_SKIP() << "the shader source is not beside the test sources";
            std::stringstream text;
            text << in.rdbuf();
            const std::string all = text.str();
            std::map<std::string, float> c;
            const std::regex line(R"((?:static\s+)?const\s+float\s+(\w+)\s*=\s*([-+0-9.eE]+)\s*;)");
            for (auto it = std::sregex_iterator(all.begin(), all.end(), line); it != std::sregex_iterator(); ++it)
                c[(*it)[1]] = std::stof((*it)[2]);
            ASSERT_TRUE(c.count("fogCurveCodeScale")) << name;
            ASSERT_TRUE(c.count("fogCurvePowerScale")) << name;
            ASSERT_TRUE(c.count("fogCurveMaxScale")) << name;
            EXPECT_FLOAT_EQ(c.at("fogCurveCodeScale"), kCodeScale) << name;
            EXPECT_FLOAT_EQ(c.at("fogCurvePowerScale"), kPowerScale) << name;
            EXPECT_FLOAT_EQ(c.at("fogCurveMaxScale"), kMaxScale) << name;
            // the linear branch of applyFog goes through the curve, and only that branch
            EXPECT_NE(all.find("fogValue = fogCurve(clamp((distance - scene.fogStart) * scene.fogScale, 0.0, 1.0));"),
                std::string::npos)
                << name;
            std::size_t uses = 0;
            for (std::size_t at = all.find("fogCurve("); at != std::string::npos; at = all.find("fogCurve(", at + 1))
                ++uses;
            EXPECT_EQ(uses, 2u) << name << ": fogCurve is defined once and used once";
        }
    }
}
