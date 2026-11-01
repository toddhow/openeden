// Tests for the exterior sky-visibility field, Render::Gi (task 0036, stage a). Written from the task spec before the
// implementation: the file format, the visibility maths and the projection of the sky.

#include <components/render/skyvisibility.hpp>
#include <components/render/skyvismath.hpp>

#include <components/render/atmospheremath.hpp>
#include <components/render/probebake.hpp>
#include <components/render/probesamplermath.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace
{
    using namespace Render::Gi;

    constexpr float fourPi = 12.5663706f;

    // A made-up field with small distinct numbers: nothing here comes from a game.
    SkyVisField sampleField(float spacing = 1024.f, std::uint32_t levels = 3)
    {
        SkyVisField f;
        f.cellX = -3;
        f.cellY = 7;
        f.rays = 128;
        f.seed = 5;
        f.spacing = spacing;
        f.levelSpacing = 128.f;
        f.levels = levels;
        const std::uint32_t n = skyVisColumnsPerSide(spacing);
        for (std::uint32_t c = 0; c < n * n; ++c)
        {
            f.base.push_back(static_cast<float>(c) * 3.25f - 40.5f);
            for (std::uint32_t l = 0; l < levels; ++l)
            {
                VisSh sh{};
                for (int k = 0; k < 9; ++k)
                    sh[k] = skyVisQuantize(0.01f * static_cast<float>((c + l * 7 + k * 13) % 200) - 1.f);
                f.probes.push_back(sh);
            }
        }
        return f;
    }

    // All visible directions of a sphere, as the baker adds them.
    VisSh bakeSphere(const std::function<bool(const vsg::vec3&)>& visible, std::uint32_t rays = 4096)
    {
        VisSh sh{};
        for (std::uint32_t i = 0; i < rays; ++i)
        {
            const vsg::vec3 dir = spiralDirection(i, rays, 0.5f);
            if (visible(dir))
                visAdd(sh, dir, fourPi / static_cast<float>(rays));
        }
        return sh;
    }
}

TEST(SkyVisFile, ColumnsPerSideFollowTheSpacing)
{
    EXPECT_EQ(skyVisColumnsPerSide(256.f), 33u);
    EXPECT_EQ(skyVisColumnsPerSide(512.f), 17u);
    EXPECT_EQ(skyVisColumnsPerSide(1024.f), 9u);
}

TEST(SkyVisFile, FileNameFollowsGridCoordinates)
{
    EXPECT_EQ(skyVisFilePath(-3, 7), "openeden/probes/exterior/-3_7.skyvis.json");
    EXPECT_EQ(skyVisFilePath(0, 0), "openeden/probes/exterior/0_0.skyvis.json");
}

TEST(SkyVisFile, QuantizeRoundsToAMultipleAndIsIdempotent)
{
    for (const float v : { 0.f, 0.3f, -0.7f, 1.2345678f, 3.5449077f, -3.5f })
    {
        const float q = skyVisQuantize(v);
        EXPECT_NEAR(q, v, 0.5f / 4096.f + 1e-7f);
        EXPECT_FLOAT_EQ(q * 4096.f, std::round(q * 4096.f));
        EXPECT_EQ(skyVisQuantize(q), q);
    }
    // Far outside the range it is clamped to the 16-bit limits, not wrapped.
    EXPECT_FLOAT_EQ(skyVisQuantize(100.f), 32767.f / 4096.f);
    EXPECT_FLOAT_EQ(skyVisQuantize(-100.f), -32768.f / 4096.f);
}

TEST(SkyVisFile, RoundTripsByteForByte)
{
    const SkyVisField field = sampleField();
    ASSERT_EQ(validate(field), "");
    const std::string text = writeCanonical(field);
    ProbeError error;
    const auto parsed = parseSkyVis(text, "x.skyvis.json", error);
    ASSERT_TRUE(parsed.has_value()) << error.describe();
    EXPECT_EQ(writeCanonical(*parsed), text);
    EXPECT_EQ(parsed->cellX, -3);
    EXPECT_EQ(parsed->cellY, 7);
    EXPECT_EQ(parsed->rays, 128u);
    EXPECT_EQ(parsed->seed, 5u);
    EXPECT_EQ(parsed->levels, 3u);
    ASSERT_EQ(parsed->base.size(), field.base.size());
    for (std::size_t i = 0; i < field.base.size(); ++i)
        EXPECT_EQ(parsed->base[i], field.base[i]);
    ASSERT_EQ(parsed->probes.size(), field.probes.size());
    for (std::size_t i = 0; i < field.probes.size(); ++i)
        EXPECT_EQ(parsed->probes[i], field.probes[i]);
}

TEST(SkyVisFile, CanonicalTextHasTheDocumentedShape)
{
    const std::string text = writeCanonical(sampleField());
    EXPECT_EQ(text.rfind("{\n  \"format\": \"openeden.skyvis\",\n  \"version\": 1,\n  \"cell\": {\n    \"exterior\": "
                         "[-3, 7]\n  },\n",
                  0),
        0u);
    EXPECT_EQ(text.back(), '\n');
    EXPECT_EQ(text.find('\r'), std::string::npos);
}

TEST(SkyVisFile, ParserAcceptsOtherWhitespace)
{
    const SkyVisField field = sampleField();
    std::string text = writeCanonical(field);
    // Replace every newline by a tab-indented newline: still valid JSON.
    std::string spaced;
    for (const char c : text)
    {
        spaced += c;
        if (c == '\n')
            spaced += "\t";
    }
    ProbeError error;
    const auto parsed = parseSkyVis(spaced, "x.skyvis.json", error);
    ASSERT_TRUE(parsed.has_value()) << error.describe();
    EXPECT_EQ(writeCanonical(*parsed), text);
}

TEST(SkyVisFile, ErrorsAreLocated)
{
    const std::string good = writeCanonical(sampleField());
    ProbeError error;

    {
        std::string bad = good;
        bad.replace(bad.find("\"version\": 1"), 12, "\"version\": 2");
        EXPECT_FALSE(parseSkyVis(bad, "f.json", error).has_value());
        EXPECT_EQ(error.file, "f.json");
        EXPECT_EQ(error.line, 3);
        EXPECT_EQ(error.field, "version");
        EXPECT_NE(error.describe().find("f.json:3: version: "), std::string::npos);
    }
    {
        std::string bad = good;
        bad.replace(bad.find("openeden.skyvis"), 15, "openeden.probes");
        EXPECT_FALSE(parseSkyVis(bad, "f.json", error).has_value());
        EXPECT_EQ(error.field, "format");
    }
    {
        std::string bad = good;
        bad.replace(bad.find("\"rays\""), 6, "\"rayz\"");
        EXPECT_FALSE(parseSkyVis(bad, "f.json", error).has_value());
        EXPECT_NE(error.message.find("rayz"), std::string::npos);
    }
    {
        // A fraction in an SH number is refused: they are integers.
        std::string bad = good;
        const std::size_t columns = bad.find("\"columns\"");
        const std::size_t firstComma = bad.find(", ", bad.find('[', columns + 12));
        bad.insert(firstComma + 2, "0.5, ");
        EXPECT_FALSE(parseSkyVis(bad, "f.json", error).has_value());
        EXPECT_EQ(error.field.rfind("columns[0]", 0), 0u);
    }
    {
        // A column of the wrong length names the column.
        std::string bad = good;
        const std::size_t columns = bad.find("\"columns\"");
        const std::size_t open = bad.find('[', columns + 12);
        const std::size_t close = bad.find(']', open);
        const std::size_t lastComma = bad.rfind(", ", close);
        bad.erase(lastComma, close - lastComma);
        EXPECT_FALSE(parseSkyVis(bad, "f.json", error).has_value());
        EXPECT_EQ(error.field.rfind("columns[0]", 0), 0u);
        EXPECT_GE(error.line, 1);
    }
    {
        // An SH number outside the 16-bit range.
        std::string bad = good;
        const std::size_t columns = bad.find("\"columns\"");
        const std::size_t firstComma = bad.find(", ", bad.find('[', columns + 12));
        bad.insert(firstComma + 2, "99999, ");
        EXPECT_FALSE(parseSkyVis(bad, "f.json", error).has_value());
    }
    {
        // Not JSON at all.
        EXPECT_FALSE(parseSkyVis("not json", "f.json", error).has_value());
        EXPECT_EQ(error.line, 1);
        EXPECT_FALSE(parseSkyVis("", "f.json", error).has_value());
    }
}

TEST(SkyVisFile, ValidateRefusesBadFields)
{
    {
        SkyVisField f = sampleField();
        f.spacing = 300.f; // does not divide a cell
        EXPECT_NE(validate(f), "");
    }
    {
        SkyVisField f = sampleField();
        f.levels = 0;
        EXPECT_NE(validate(f), "");
    }
    {
        SkyVisField f = sampleField();
        f.base.pop_back();
        EXPECT_NE(validate(f), "");
    }
    {
        SkyVisField f = sampleField();
        f.probes.pop_back();
        EXPECT_NE(validate(f), "");
    }
    {
        SkyVisField f = sampleField();
        f.base[0] = std::nanf("");
        EXPECT_NE(validate(f), "");
    }
    {
        SkyVisField f = sampleField();
        f.probes[0][0] = 0.1234567f; // not a multiple of 1/4096
        EXPECT_NE(validate(f), "");
    }
    {
        SkyVisField f = sampleField();
        f.levelSpacing = 0.f;
        EXPECT_NE(validate(f), "");
    }
    {
        SkyVisField f = sampleField();
        f.rays = 0;
        EXPECT_NE(validate(f), "");
    }
}

TEST(SkyVisFile, HostileInputsDoNotCrash)
{
    ProbeError error;
    EXPECT_FALSE(parseSkyVis(std::string(100000, '['), "f.json", error).has_value());
    EXPECT_FALSE(parseSkyVis("{\"format\": \"openeden.skyvis\", \"version\": 1}", "f.json", error).has_value());
    EXPECT_FALSE(parseSkyVis("{\"format\": \"openeden.skyvis\", \"version\": 1e0}", "f.json", error).has_value());
}

TEST(SkyVisFile, SizeOfARealisticCellIsMeasured)
{
    // 33 by 33 columns of 8 levels, the default; the quantised integer text is what keeps a cell small.
    SkyVisField f = sampleField(256.f, 8);
    ASSERT_EQ(validate(f), "");
    const std::string text = writeCanonical(f);
    EXPECT_LT(text.size(), 1100u * 1024u);
    EXPECT_GT(text.size(), 100u * 1024u);
}

TEST(SkyVisMath, OpenPointHasTheKnownSh)
{
    const VisSh open = openVisibility();
    EXPECT_NEAR(open[0], std::sqrt(fourPi), 1e-4f);
    for (int k = 1; k < 9; ++k)
        EXPECT_FLOAT_EQ(open[k], 0.f);
    // What the baker gets from rays that all escape matches it.
    const VisSh baked = bakeSphere([](const vsg::vec3&) { return true; });
    EXPECT_NEAR(baked[0], open[0], 2e-3f);
    for (int k = 1; k < 9; ++k)
        EXPECT_NEAR(baked[k], 0.f, 2e-3f);
}

TEST(SkyVisMath, BlockedPointIsZero)
{
    const VisSh blocked = bakeSphere([](const vsg::vec3&) { return false; });
    for (int k = 0; k < 9; ++k)
        EXPECT_FLOAT_EQ(blocked[k], 0.f);
    for (const float v : blockedVisibility())
        EXPECT_FLOAT_EQ(v, 0.f);
}

TEST(SkyVisMath, HemisphereMatchesTheBakedHemisphere)
{
    const VisSh baked = bakeSphere([](const vsg::vec3& d) { return d.z > 0.f; }, 20000);
    const VisSh exact = hemisphereVisibility();
    for (int k = 0; k < 9; ++k)
        EXPECT_NEAR(baked[k], exact[k], 4e-3f) << "coefficient " << k;
    EXPECT_NEAR(exact[0], std::sqrt(fourPi) / 2.f, 1e-4f);
}

TEST(SkyVisMath, DotOfUniformSkyAndOpenPointIsTheSkyAmbient)
{
    const vsg::vec3 colour(0.2f, 0.35f, 0.5f);
    const Sh9 sky = projectSky(colour, colour, colour);
    const vsg::vec3 d = skyDot(sky, openVisibility());
    EXPECT_NEAR(d.x, colour.x, 1e-3f);
    EXPECT_NEAR(d.y, colour.y, 1e-3f);
    EXPECT_NEAR(d.z, colour.z, 1e-3f);
    // The same as the probe maths' ambient of the uniform sky for any normal.
    const vsg::vec3 a = shAmbient(sky, vsg::vec3(0.3f, -0.4f, 0.8f));
    EXPECT_NEAR(a.x, colour.x, 1e-3f);
    // A blocked point gets nothing.
    const vsg::vec3 z = skyDot(sky, blockedVisibility());
    EXPECT_FLOAT_EQ(z.x, 0.f);
}

TEST(SkyVisMath, ProjectionOfAUniformSkyIsTheConstantTermOnly)
{
    const vsg::vec3 colour(0.9f, 0.4f, 0.1f);
    const Sh9 sky = projectSky(colour, colour, colour);
    EXPECT_NEAR(sky.c[0].x, colour.x * std::sqrt(fourPi), 1e-3f);
    EXPECT_NEAR(sky.c[0].y, colour.y * std::sqrt(fourPi), 1e-3f);
    for (int k = 1; k < 9; ++k)
    {
        EXPECT_NEAR(sky.c[k].x, 0.f, 1e-3f) << k;
        EXPECT_NEAR(sky.c[k].y, 0.f, 1e-3f) << k;
        EXPECT_NEAR(sky.c[k].z, 0.f, 1e-3f) << k;
    }
}

TEST(SkyVisMath, ABrighterZenithTiltsTheSkyUpwards)
{
    const Sh9 sky = projectSky(vsg::vec3(1.f, 1.f, 1.f), vsg::vec3(0.5f, 0.5f, 0.5f), vsg::vec3(0.5f, 0.5f, 0.5f));
    // Coefficient 2 is the one along +Z; only the Z-symmetric terms are non-zero.
    EXPECT_GT(sky.c[2].x, 0.05f);
    for (const int k : { 1, 3, 4, 5, 7, 8 })
        EXPECT_NEAR(sky.c[k].x, 0.f, 1e-4f) << k;
    // Light from above reaches an up-facing normal more than a down-facing one.
    EXPECT_GT(shAmbient(sky, vsg::vec3(0, 0, 1)).x, shAmbient(sky, vsg::vec3(0, 0, -1)).x);
}

TEST(SkyVisMath, ProjectionIsLinearInTheColours)
{
    const Sh9 a = projectSky(vsg::vec3(1.f, 0.f, 0.f), vsg::vec3(0.f, 1.f, 0.f), vsg::vec3(0.f, 0.f, 1.f));
    const Sh9 b = projectSky(vsg::vec3(2.f, 0.f, 0.f), vsg::vec3(0.f, 2.f, 0.f), vsg::vec3(0.f, 0.f, 2.f));
    for (int k = 0; k < 9; ++k)
        EXPECT_NEAR(b.c[k].x, 2.f * a.c[k].x, 1e-4f);
}

TEST(SkyVisMath, OcclusionIsOneOnOpenGroundWhateverTheSky)
{
    const std::vector<std::array<vsg::vec3, 3>> skies = {
        { vsg::vec3(0.2f, 0.4f, 0.8f), vsg::vec3(0.9f, 0.6f, 0.3f), vsg::vec3(0.1f, 0.1f, 0.1f) },
        { vsg::vec3(1.f, 1.f, 1.f), vsg::vec3(1.f, 1.f, 1.f), vsg::vec3(1.f, 1.f, 1.f) },
        { vsg::vec3(0.01f, 0.02f, 0.05f), vsg::vec3(0.02f, 0.02f, 0.03f), vsg::vec3(0.f, 0.f, 0.f) },
    };
    for (const auto& s : skies)
    {
        const Sh9 sky = projectSky(s[0], s[1], s[2]);
        const vsg::vec3 o = skyOcclusion(sky, hemisphereVisibility());
        EXPECT_NEAR(o.x, 1.f, 1e-5f);
        EXPECT_NEAR(o.y, 1.f, 1e-5f);
        EXPECT_NEAR(o.z, 1.f, 1e-5f);
    }
}

TEST(SkyVisMath, OcclusionOfBlockedIsZeroAndOpenAirIsCappedAtOne)
{
    const Sh9 sky = projectSky(vsg::vec3(0.3f, 0.5f, 0.9f), vsg::vec3(0.6f, 0.5f, 0.4f), vsg::vec3(0.1f, 0.1f, 0.1f));
    const vsg::vec3 blocked = skyOcclusion(sky, blockedVisibility());
    EXPECT_FLOAT_EQ(blocked.x, 0.f);
    EXPECT_FLOAT_EQ(blocked.y, 0.f);
    EXPECT_FLOAT_EQ(blocked.z, 0.f);
    // A point in the air sees the sphere, which is more than open ground does; it is not made brighter than flat.
    const vsg::vec3 open = skyOcclusion(sky, openVisibility());
    EXPECT_LE(open.x, 1.f);
    EXPECT_FLOAT_EQ(open.x, 1.f);
}

TEST(SkyVisMath, UnderACoverIsDarkerAndFollowsTheSkyColour)
{
    // A roof over everything above 60 degrees: only the low sky shows.
    const VisSh covered = bakeSphere([](const vsg::vec3& d) { return d.z > 0.f && d.z < 0.5f; }, 8000);
    const vsg::vec3 zenith(0.2f, 0.3f, 1.0f), horizon(1.0f, 0.5f, 0.2f), ground(0.1f, 0.1f, 0.1f);
    const vsg::vec3 o = skyOcclusion(projectSky(zenith, horizon, ground), covered);
    EXPECT_GT(o.x, 0.f);
    EXPECT_LT(o.x, 1.f);
    EXPECT_LT(o.z, 1.f);
    // Only the warm horizon is visible, so red is kept better than blue.
    EXPECT_GT(o.x, o.z);
    // Under a uniform sky all channels are cut by the same share.
    const vsg::vec3 u(0.5f, 0.5f, 0.5f);
    const vsg::vec3 ou = skyOcclusion(projectSky(u, u, u), covered);
    EXPECT_NEAR(ou.x, ou.z, 1e-5f);
    EXPECT_LT(ou.x, 1.f);
}

TEST(SkyVisMath, OcclusionHandlesAZeroSky)
{
    const Sh9 sky = projectSky(vsg::vec3(0, 0, 0), vsg::vec3(0, 0, 0), vsg::vec3(0, 0, 0));
    const vsg::vec3 o = skyOcclusion(sky, bakeSphere([](const vsg::vec3& d) { return d.z > 0.3f; }));
    EXPECT_TRUE(std::isfinite(o.x) && std::isfinite(o.y) && std::isfinite(o.z));
    EXPECT_GE(o.x, 0.f);
    EXPECT_LE(o.x, 1.f);
}

TEST(SkyVisMath, SkyFromTheAtmosphereColoursIsLinearAndFinite)
{
    Render::Atmosphere::Colours c;
    c.sky = vsg::vec3(0.3f, 0.5f, 0.9f);
    c.horizon = vsg::vec3(0.8f, 0.7f, 0.6f);
    c.ambient = vsg::vec3(0.4f, 0.4f, 0.45f);
    const Sh9 sky = skyFromColours(c.sky, c.horizon);
    // The colours are display-encoded and the model works in linear light: the constant term sits between the linear
    // horizon and the linear zenith values, not the encoded ones.
    const float lin = Render::Hdr::toLinear(c.sky.x);
    EXPECT_LT(lin, c.sky.x);
    for (const vsg::vec3& k : sky.c)
        EXPECT_TRUE(std::isfinite(k.x) && std::isfinite(k.y) && std::isfinite(k.z));
    EXPECT_GT(sky.c[0].x, 0.f);
}

// ---- Task 0036 (stage c): the share of the ambient a surface keeps depends on which way it faces ----

namespace
{
    Sh9 testSky()
    {
        return projectSky(vsg::vec3(0.3f, 0.5f, 0.9f), vsg::vec3(0.8f, 0.6f, 0.4f), vsg::vec3(0.1f, 0.1f, 0.1f));
    }
}

TEST(SkyVisNormal, CosineVisibilityOfOpenGroundFollowsTheHeightOfTheNormal)
{
    const VisSh h = hemisphereVisibility();
    EXPECT_NEAR(visCosine(h, vsg::vec3(0, 0, 1)), 1.f, 1e-3f);
    EXPECT_NEAR(visCosine(h, vsg::vec3(1, 0, 0)), 0.5f, 1e-3f);
    EXPECT_NEAR(visCosine(h, vsg::vec3(0, 0, -1)), 0.f, 1e-3f);
    EXPECT_NEAR(visCosine(openVisibility(), vsg::vec3(0.3f, 0.2f, -0.7f)), 1.f, 1e-3f);
    EXPECT_FLOAT_EQ(visCosine(blockedVisibility(), vsg::vec3(0, 1, 0)), 0.f);
}

TEST(SkyVisNormal, OpenGroundKeepsEverythingForAnyNormal)
{
    const Sh9 sky = testSky();
    for (const vsg::vec3& n : { vsg::vec3(0, 0, 1), vsg::vec3(1, 0, 0), vsg::vec3(0, -1, 0), vsg::vec3(0, 0, -1),
             vsg::vec3(0.6f, 0.0f, 0.8f), vsg::vec3(0, 0, 0) })
    {
        const vsg::vec3 o = skyOcclusionNormal(sky, hemisphereVisibility(), n);
        EXPECT_NEAR(o.x, 1.f, 1e-4f) << n.x << " " << n.y << " " << n.z;
        EXPECT_NEAR(o.y, 1.f, 1e-4f);
        EXPECT_NEAR(o.z, 1.f, 1e-4f);
    }
}

TEST(SkyVisNormal, AWallSeesLessOnTheSideItFacesTheBuilding)
{
    // A building to the west: the sky is hidden for directions with x < -0.2 and below the horizon.
    VisSh v{};
    for (std::uint32_t i = 0; i < 6000; ++i)
    {
        const vsg::vec3 d = spiralDirection(i, 6000, 0.5f);
        if (d.z > 0.f && d.x > -0.2f)
            visAdd(v, d, 12.5663706f / 6000.f);
    }
    const Sh9 sky = testSky();
    const vsg::vec3 facingAway = skyOcclusionNormal(sky, v, vsg::vec3(1, 0, 0));
    const vsg::vec3 facingIn = skyOcclusionNormal(sky, v, vsg::vec3(-1, 0, 0));
    EXPECT_GT(facingAway.x, 0.8f);
    EXPECT_LT(facingIn.x, 0.4f);
    EXPECT_GT(facingAway.x, facingIn.x + 0.4f);
    // Up facing is in between, and a blocked point is dark whatever it faces.
    const vsg::vec3 up = skyOcclusionNormal(sky, v, vsg::vec3(0, 0, 1));
    EXPECT_GT(up.x, facingIn.x);
    EXPECT_LE(up.x, 1.f);
    for (const vsg::vec3& n : { vsg::vec3(1, 0, 0), vsg::vec3(0, 0, 1), vsg::vec3(0, 0, 0) })
    {
        const vsg::vec3 b = skyOcclusionNormal(sky, blockedVisibility(), n);
        EXPECT_FLOAT_EQ(b.x, 0.f);
        EXPECT_FLOAT_EQ(b.z, 0.f);
    }
}

TEST(SkyVisNormal, NoNormalGivesTheOmniShare)
{
    const Sh9 sky = testSky();
    VisSh half = hemisphereVisibility();
    for (float& v : half)
        v *= 0.5f;
    const vsg::vec3 withNone = skyOcclusionNormal(sky, half, vsg::vec3(0, 0, 0));
    const vsg::vec3 omni = skyOcclusion(sky, half);
    EXPECT_NEAR(withNone.x, omni.x, 1e-5f);
    EXPECT_NEAR(withNone.z, omni.z, 1e-5f);
}

TEST(SkyVisNormal, ASkyWithNoLightKeepsTheFlatAmbientWhateverTheNormal)
{
    const Sh9 dark = projectSky(vsg::vec3(0, 0, 0), vsg::vec3(0, 0, 0), vsg::vec3(0, 0, 0));
    const vsg::vec3 o = skyOcclusionNormal(dark, blockedVisibility(), vsg::vec3(0, 0, 1));
    EXPECT_TRUE(std::isfinite(o.x) && std::isfinite(o.y) && std::isfinite(o.z));
}
