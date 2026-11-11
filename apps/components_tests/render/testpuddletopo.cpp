// OpenEden (task 0095, stage W-e): the topographic fallback of the puddle map. Where the map covers the ground and found
// no pit, small shallow puddles come from the fine shape of the ground (the dip of the stored heights), the same supply
// as the pits and a finer noise. The C++ twin is components/render/puddlemapgpu.hpp; this file also checks the packer's
// words and that the GLSL and Slang twins carry the same constants. Generated cells only.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <components/puddle/puddlegpu.hpp>
#include <components/render/puddlemapgpu.hpp>

namespace
{
    namespace PM = Render::PuddleMap;

    constexpr std::uint32_t side = 8;
    constexpr std::uint32_t spacing = 64;
    constexpr std::uint32_t cellSize = side * spacing;
    constexpr float base = 100.f;

    std::shared_ptr<Puddle::CellMap> makeCell(std::int32_t cx, std::int32_t cy)
    {
        auto cell = std::make_shared<Puddle::CellMap>();
        cell->mCellX = cx;
        cell->mCellY = cy;
        cell->mSide = side;
        cell->mSpacing = static_cast<float>(spacing);
        cell->mHeightBase = base;
        const std::size_t n = side * side;
        cell->mTexelFlags.assign(n, 0);
        cell->mTop.assign(n, 800); // 100 units over the base: level ground at 200
        cell->mCapacity.assign(n, 0);
        cell->mBasinMax.assign(n, 0);
        cell->mCatchment.assign(n, 0);
        cell->mBasinId.assign(n, Puddle::noBasin);
        return cell;
    }

    struct Fixture
    {
        std::map<std::pair<std::int32_t, std::int32_t>, std::shared_ptr<const Puddle::CellMap>> cells;
        std::vector<std::uint32_t> buffer
            = std::vector<std::uint32_t>(PM::bufferWordsWithTops(PM::windowSide, side), 0u);
        bool topographic = true;
        float strength = 1.f;
        bool withTops = true;

        void pack()
        {
            Puddle::GpuWindowParams p;
            p.mTexelsPerCell = side;
            p.mSpacingUnits = spacing;
            p.mCellSizeUnits = cellSize;
            p.mSupply = 0.5f;
            p.mAbsorption = 0.1f;
            p.mTopographic = topographic;
            p.mTopographicStrength = strength;
            const std::size_t words = withTops ? buffer.size() : PM::bufferWords(PM::windowSide, side);
            Puddle::packWindow(buffer.data(), words, p, [this](std::int32_t x, std::int32_t y) {
                const auto it = cells.find({ x, y });
                return it == cells.end() ? std::shared_ptr<const Puddle::CellMap>() : it->second;
            });
        }

        PM::Shade shade(float x, float y, float wet, float normalZ = 1.f)
        {
            return PM::shadeAt(buffer.data(), x, y, 200.f, wet, normalZ, PM::ShadeMode::Terrain);
        }
    };

    float atTexel(int t)
    {
        return static_cast<float>(t * static_cast<int>(spacing));
    }

    // A level plain with a hollow of `depth` units (a multiple of 1/8) in texel (4, 4) of cell (0, 0).
    void dig(Puddle::CellMap& c, float depth)
    {
        c.mTop[4 * side + 4] = static_cast<std::uint16_t>(800 - depth * 8.f);
    }

    TEST(PuddleTopo, the_constants_and_words_are_what_the_shaders_read)
    {
        EXPECT_EQ(PM::optionTopographic, 2u);
        EXPECT_EQ(PM::wordTopoStrength, 12u);
        EXPECT_LT(PM::wordTopoStrength, PM::tableBase);
        EXPECT_EQ(PM::optionKeepStaticNoise, 1u);
    }

    TEST(PuddleTopo, the_dip_of_level_ground_is_zero_and_of_a_hollow_positive)
    {
        Fixture f;
        auto c = makeCell(0, 0);
        f.cells[{ 0, 0 }] = c;
        f.pack();
        EXPECT_FLOAT_EQ(PM::groundDip(f.buffer.data(), 4.f, 4.f), 0.f);
        EXPECT_FLOAT_EQ(PM::groundDip(f.buffer.data(), 3.3f, 5.7f), 0.f);

        dig(*c, 4.f);
        f.pack();
        // At the hollow's texel the dip is the mean of the four level neighbours minus the floor: 4 units.
        EXPECT_NEAR(PM::groundDip(f.buffer.data(), 4.f, 4.f), 4.f, 1e-4f);
        // Next to it the hollow is a neighbour of the texel, which sees a quarter of its depth as a rise... as a drop of
        // a quarter: the neighbour's own height is level and one of its four neighbours is 4 units lower, so the mean is
        // 1 unit below it: a mound of 1 unit.
        EXPECT_NEAR(PM::groundDip(f.buffer.data(), 5.f, 4.f), -1.f, 1e-4f);
        // Between the two texels the blend is by distance.
        EXPECT_NEAR(PM::groundDip(f.buffer.data(), 4.5f, 4.f), 0.5f * 4.f + 0.5f * -1.f, 1e-4f);
    }

    TEST(PuddleTopo, a_mound_has_a_negative_dip_and_no_hollowness)
    {
        Fixture f;
        auto c = makeCell(0, 0);
        c->mTop[4 * side + 4] = static_cast<std::uint16_t>(800 + 32);
        f.cells[{ 0, 0 }] = c;
        f.pack();
        const float dip = PM::groundDip(f.buffer.data(), 4.f, 4.f);
        EXPECT_NEAR(dip, -4.f, 1e-4f);
        EXPECT_FLOAT_EQ(PM::dipFactorOf(dip), 0.f);
    }

    TEST(PuddleTopo, the_dip_does_not_read_across_a_missing_cell_as_a_hollow)
    {
        // The texel at the edge of the only loaded cell: its neighbour in the missing cell counts as level with it.
        Fixture f;
        f.cells[{ 0, 0 }] = makeCell(0, 0);
        f.pack();
        EXPECT_FLOAT_EQ(PM::groundDip(f.buffer.data(), 0.f, 0.f), 0.f);
        EXPECT_FLOAT_EQ(PM::groundDip(f.buffer.data(), 7.f, 3.f), 0.f);
    }

    TEST(PuddleTopo, a_buffer_without_tops_has_no_dip)
    {
        Fixture f;
        f.withTops = false;
        f.cells[{ 0, 0 }] = makeCell(0, 0);
        f.pack();
        EXPECT_EQ(f.buffer[PM::wordTopsBase], 0u);
        EXPECT_FLOAT_EQ(PM::groundDip(f.buffer.data(), 4.f, 4.f), 0.f);
    }

    TEST(PuddleTopo, the_hollowness_rises_from_flat_to_full_over_the_documented_dips)
    {
        EXPECT_FLOAT_EQ(PM::dipFactorOf(0.f), 0.f);
        EXPECT_FLOAT_EQ(PM::dipFactorOf(PM::topoDipFrom), 0.f);
        EXPECT_FLOAT_EQ(PM::dipFactorOf(PM::topoDipTo), 1.f);
        EXPECT_FLOAT_EQ(PM::dipFactorOf(100.f), 1.f);
        float last = 0.f;
        for (float d = -1.f; d < 6.f; d += 0.05f)
        {
            const float v = PM::dipFactorOf(d);
            EXPECT_GE(v, last - 1e-6f);
            last = v;
        }
        EXPECT_NEAR(PM::dipFactorOf(0.5f * (PM::topoDipFrom + PM::topoDipTo)), 0.5f, 1e-5f);
    }

    TEST(PuddleTopo, the_ramp_starts_where_the_pits_start_and_ends_at_full_wetness)
    {
        // supply 0.5, absorption 0.1: the ground holds water from wetness 0.2.
        EXPECT_FLOAT_EQ(PM::topoRamp(0.f, 0.5f, 0.1f), 0.f);
        EXPECT_FLOAT_EQ(PM::topoRamp(0.2f, 0.5f, 0.1f), 0.f);
        EXPECT_NEAR(PM::topoRamp(0.6f, 0.5f, 0.1f), 0.5f, 1e-5f);
        EXPECT_FLOAT_EQ(PM::topoRamp(1.f, 0.5f, 0.1f), 1.f);
        EXPECT_FLOAT_EQ(PM::topoRamp(5.f, 0.5f, 0.1f), 1.f);
        EXPECT_FLOAT_EQ(PM::topoRamp(-1.f, 0.5f, 0.1f), 0.f);
        // Nothing to take up: never any water.
        EXPECT_FLOAT_EQ(PM::topoRamp(1.f, 0.1f, 0.1f), 0.f);
        EXPECT_FLOAT_EQ(PM::topoRamp(1.f, 0.f, 0.f), 0.f);
    }

    TEST(PuddleTopo, no_water_no_slope_no_strength_no_puddle)
    {
        EXPECT_FLOAT_EQ(PM::topoCoverage(0.f, 1.f, 0.99f, 1.f, 1.f), 0.f);
        EXPECT_FLOAT_EQ(PM::topoCoverage(1.f, 1.f, 0.99f, 1.f, 0.f), 0.f);
        // A steep surface: the flatness of the old puddles takes it away.
        EXPECT_FLOAT_EQ(PM::topoCoverage(1.f, 1.f, 0.99f, 0.5f, 1.f), 0.f);
        EXPECT_FLOAT_EQ(PM::topoCoverage(1.f, 1.f, 0.99f, 1.f, 1.f), 1.f);
    }

    TEST(PuddleTopo, a_hollow_puddles_where_flat_ground_stays_dry)
    {
        // Full ramp, strength 1: the threshold is 1 - 0.3 = 0.7 with an edge of 0.06 each side. Noise 0.45: flat ground
        // needs 0.64 at least (dry), a full hollow adds 0.35 to reach 0.8 (wet).
        EXPECT_FLOAT_EQ(PM::topoCoverage(1.f, 0.f, 0.45f, 1.f, 1.f), 0.f);
        EXPECT_FLOAT_EQ(PM::topoCoverage(1.f, 1.f, 0.45f, 1.f, 1.f), 1.f);
        // Half a hollow is in between.
        const float half = PM::topoCoverage(1.f, 0.5f, 0.55f, 1.f, 1.f);
        EXPECT_GT(half, 0.f);
        EXPECT_LT(half, 1.f);
    }

    TEST(PuddleTopo, coverage_grows_with_the_water_and_shrinks_as_it_dries_for_every_spot)
    {
        for (float noiseValue = 0.f; noiseValue < 1.f; noiseValue += 0.07f)
        {
            for (float dipFactor : { 0.f, 0.5f, 1.f })
            {
                float last = 0.f;
                for (float ramp = 0.f; ramp <= 1.f; ramp += 0.02f)
                {
                    const float v = PM::topoCoverage(ramp, dipFactor, noiseValue, 1.f, 1.f);
                    EXPECT_GE(v, last - 1e-6f) << noiseValue << " " << dipFactor << " " << ramp;
                    EXPECT_GE(v, 0.f);
                    EXPECT_LE(v, 1.f);
                    last = v;
                }
            }
        }
    }

    TEST(PuddleTopo, a_hollower_spot_is_never_drier_than_a_flatter_one_with_the_same_noise)
    {
        for (float noiseValue = 0.f; noiseValue < 1.f; noiseValue += 0.05f)
        {
            for (float ramp = 0.f; ramp <= 1.f; ramp += 0.1f)
            {
                float last = 0.f;
                for (float dipFactor = 0.f; dipFactor <= 1.f; dipFactor += 0.1f)
                {
                    const float v = PM::topoCoverage(ramp, dipFactor, noiseValue, 1.f, 1.f);
                    EXPECT_GE(v, last - 1e-6f);
                    last = v;
                }
            }
        }
    }

    TEST(PuddleTopo, strength_scales_how_much_ground_can_be_puddle)
    {
        // Count the share of a noise sweep that is wet on flat ground at full ramp: grows with the strength.
        auto share = [](float strength) {
            int wet = 0;
            const int steps = 1000;
            for (int i = 0; i < steps; ++i)
                if (PM::topoCoverage(1.f, 0.f, (static_cast<float>(i) + 0.5f) / steps, 1.f, strength) > 0.5f)
                    ++wet;
            return static_cast<float>(wet) / steps;
        };
        EXPECT_FLOAT_EQ(share(0.f), 0.f);
        EXPECT_GT(share(1.f), share(0.5f));
        EXPECT_GT(share(2.f), share(1.f));
        EXPECT_NEAR(share(1.f), PM::topoMaxCoverage, 0.02f);
        // Beyond the range the setting is cut.
        EXPECT_FLOAT_EQ(share(9.f), share(2.f));
    }

    TEST(PuddleTopo, the_noise_is_finer_than_the_old_puddles)
    {
        EXPECT_LT(PM::topoNoiseCell, Render::Wetness::puddleNoiseCell);
        EXPECT_LT(PM::topoMaxCoverage, Render::Wetness::puddleMaxCoverage);
    }

    TEST(PuddleTopo, off_the_covered_ground_with_no_pit_stays_dry_as_before)
    {
        Fixture f;
        f.topographic = false;
        auto c = makeCell(0, 0);
        dig(*c, 6.f);
        f.cells[{ 0, 0 }] = c;
        f.pack();
        EXPECT_EQ(f.buffer[PM::wordOptions] & PM::optionTopographic, 0u);
        for (int t = 1; t < 7; ++t)
        {
            const PM::Shade s = f.shade(atTexel(t), atTexel(4), 1.f);
            EXPECT_TRUE(s.covered);
            EXPECT_FALSE(s.topographic);
            EXPECT_FLOAT_EQ(s.coverage, 0.f);
            EXPECT_FLOAT_EQ(s.depth, 0.f);
        }
    }

    TEST(PuddleTopo, on_a_hollow_gets_a_shallow_puddle_in_the_rain_and_none_when_dry)
    {
        Fixture f;
        auto c = makeCell(0, 0);
        dig(*c, 6.f);
        f.cells[{ 0, 0 }] = c;
        f.pack();
        const float x = atTexel(4);
        const float y = atTexel(4);
        // Find a noise value high enough at this place for the hollow: the shade is deterministic, so try a few spots
        // round the hollow and require that the soaked hollow has some puddle and the dry one has none.
        int wetSpots = 0;
        int dryWetSpots = 0;
        for (int i = 0; i < 40; ++i)
        {
            const float px = x + static_cast<float>(i) * 1.7f - 30.f;
            const PM::Shade soaked = f.shade(px, y, 1.f);
            EXPECT_TRUE(soaked.covered);
            EXPECT_TRUE(soaked.topographic);
            EXPECT_GE(soaked.coverage, 0.f);
            EXPECT_LE(soaked.depth, PM::topoMaxDepth + 1e-6f);
            EXPECT_NEAR(soaked.depth, PM::topoMaxDepth * soaked.coverage, 1e-6f);
            if (soaked.coverage > 0.5f)
                ++wetSpots;
            const PM::Shade dry = f.shade(px, y, 0.f);
            if (dry.coverage > 0.f)
                ++dryWetSpots;
        }
        EXPECT_GT(wetSpots, 0);
        EXPECT_EQ(dryWetSpots, 0);
    }

    TEST(PuddleTopo, the_hollow_is_wetter_than_the_same_noise_on_level_ground)
    {
        // Same positions over a plain and over a hollow: the hollow never has less puddle at a point near it and has
        // more somewhere.
        Fixture plain;
        plain.cells[{ 0, 0 }] = makeCell(0, 0);
        plain.pack();
        Fixture hollow;
        auto c = makeCell(0, 0);
        dig(*c, 6.f);
        hollow.cells[{ 0, 0 }] = c;
        hollow.pack();
        float plainSum = 0.f;
        float hollowSum = 0.f;
        for (int i = 0; i < 64; ++i)
        {
            const float x = atTexel(4) - 40.f + static_cast<float>(i % 8) * 10.f;
            const float y = atTexel(4) - 40.f + static_cast<float>(i / 8) * 10.f;
            const PM::Shade a = plain.shade(x, y, 1.f);
            const PM::Shade b = hollow.shade(x, y, 1.f);
            EXPECT_EQ(a.dipFactor, 0.f);
            plainSum += a.coverage;
            hollowSum += b.coverage;
        }
        EXPECT_GT(hollowSum, plainSum);
    }

    TEST(PuddleTopo, a_pit_pixel_is_the_maps_and_not_the_fallbacks)
    {
        Fixture f;
        auto c = makeCell(0, 0);
        for (std::uint32_t y = 2; y < 6; ++y)
        {
            for (std::uint32_t x = 2; x < 6; ++x)
            {
                const std::size_t i = y * side + x;
                c->mCapacity[i] = 64;
                c->mBasinMax[i] = 64;
                c->mCatchment[i] = 60;
            }
        }
        f.cells[{ 0, 0 }] = c;
        f.pack();
        const PM::Shade on = f.shade(atTexel(3), atTexel(3), 1.f);
        f.topographic = false;
        f.pack();
        const PM::Shade off = f.shade(atTexel(3), atTexel(3), 1.f);
        EXPECT_FALSE(on.topographic);
        EXPECT_GT(on.capacity, 0.f);
        EXPECT_EQ(on.coverage, off.coverage);
        EXPECT_EQ(on.depth, off.depth);
        EXPECT_EQ(on.level, off.level);
        EXPECT_EQ(on.rippleGate, off.rippleGate);
    }

    TEST(PuddleTopo, ground_the_map_does_not_cover_keeps_the_old_noise_puddles)
    {
        Fixture f;
        f.cells[{ 0, 0 }] = makeCell(0, 0);
        f.pack();
        // Outside the window: not loaded, so not covered, and the fallback does not run (the shader draws the old noise).
        const PM::Shade far = f.shade(atTexel(8) * 40.f, 0.f, 1.f);
        EXPECT_FALSE(far.loaded);
        EXPECT_FALSE(far.covered);
        EXPECT_FALSE(far.topographic);
        // With the map off altogether nothing is covered.
        std::vector<std::uint32_t> none(PM::bufferWordsWithTops(PM::windowSide, side), 0u);
        const PM::Shade s = PM::shadeAt(none.data(), 10.f, 10.f, 0.f, 1.f, 1.f, PM::ShadeMode::Terrain);
        EXPECT_FALSE(s.covered);
        EXPECT_FALSE(s.topographic);
    }

    TEST(PuddleTopo, a_steep_pixel_gets_no_puddle_from_the_fallback)
    {
        Fixture f;
        auto c = makeCell(0, 0);
        dig(*c, 6.f);
        f.cells[{ 0, 0 }] = c;
        f.pack();
        for (int i = 0; i < 40; ++i)
            EXPECT_FLOAT_EQ(f.shade(atTexel(4) + static_cast<float>(i) * 1.3f, atTexel(4), 1.f, 0.6f).coverage, 0.f);
    }

    TEST(PuddleTopo, the_ripples_start_half_grown_in_the_deepest_part_of_a_fallback_puddle)
    {
        Fixture f;
        auto c = makeCell(0, 0);
        dig(*c, 6.f);
        f.cells[{ 0, 0 }] = c;
        f.pack();
        float best = 0.f;
        for (int i = 0; i < 200; ++i)
            best = std::max(best, f.shade(atTexel(4) - 50.f + static_cast<float>(i) * 0.5f, atTexel(4), 1.f).rippleGate);
        EXPECT_GT(best, 0.f);
        EXPECT_NEAR(best, PM::smoothstep(PM::rippleDepthFrom, PM::rippleDepthTo, PM::topoMaxDepth), 1e-5f);
        EXPECT_LT(best, 1.f);
    }

    TEST(PuddleTopo, the_packer_writes_the_option_and_the_strength)
    {
        Fixture f;
        f.strength = 1.5f;
        f.cells[{ 0, 0 }] = makeCell(0, 0);
        f.pack();
        EXPECT_NE(f.buffer[PM::wordOptions] & PM::optionTopographic, 0u);
        EXPECT_FLOAT_EQ(PM::bitsFloat(f.buffer[PM::wordTopoStrength]), 1.5f);
        Fixture g;
        g.topographic = false;
        g.cells[{ 0, 0 }] = makeCell(0, 0);
        g.pack();
        EXPECT_EQ(g.buffer[PM::wordOptions], 0u);
    }

    TEST(PuddleTopo, the_relief_debug_view_has_a_name_and_colours_the_hollowness)
    {
        std::uint32_t mode = 0;
        EXPECT_TRUE(PM::parseDebugMode("relief", mode));
        EXPECT_EQ(mode, static_cast<std::uint32_t>(PM::DebugRelief));
        PM::Shade s;
        s.covered = true;
        s.dipFactor = 0.75f;
        s.coverage = 0.5f;
        const PM::Rgb c = PM::debugColour(PM::DebugRelief, s);
        EXPECT_FLOAT_EQ(c.g, 0.75f);
        EXPECT_FLOAT_EQ(c.r, 0.f);
        EXPECT_NEAR(c.b, 0.55f, 1e-6f);
        s.covered = false;
        EXPECT_FLOAT_EQ(PM::debugColour(PM::DebugRelief, s).r, 0.25f);
    }

    // The shader twins carry the same constants and the same structure.
    std::string readShader(const std::string& relative)
    {
        const std::filesystem::path source = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".."
            / "files" / "shaders" / relative;
        std::ifstream in(source, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    std::optional<double> constantOf(const std::string& text, const std::string& name)
    {
        const std::regex re("\\b" + name + "\\s*=\\s*(-?[0-9]*\\.?[0-9]+)u?\\s*;");
        std::smatch m;
        if (!std::regex_search(text, m, re))
            return std::nullopt;
        return std::stod(m[1].str());
    }

    class PuddleTopoShader : public testing::TestWithParam<const char*>
    {
    };

    TEST_P(PuddleTopoShader, the_constants_equal_the_cpp_twins)
    {
        const std::string text = readShader(GetParam());
        ASSERT_FALSE(text.empty()) << GetParam();
        const std::vector<std::pair<const char*, double>> expected = {
            { "puddleTopoNoiseCell", PM::topoNoiseCell },
            { "puddleTopoMaxCoverage", PM::topoMaxCoverage },
            { "puddleTopoDipFrom", PM::topoDipFrom },
            { "puddleTopoDipTo", PM::topoDipTo },
            { "puddleTopoDipBias", PM::topoDipBias },
            { "puddleTopoEdge", PM::topoEdge },
            { "puddleTopoMaxDepth", PM::topoMaxDepth },
            { "PUDDLE_OPTION_TOPOGRAPHIC", PM::optionTopographic },
            { "PUDDLE_TOPO_STRENGTH", PM::wordTopoStrength },
        };
        for (const auto& [name, value] : expected)
        {
            const std::optional<double> found = constantOf(text, name);
            ASSERT_TRUE(found.has_value()) << name << " in " << GetParam();
            EXPECT_NEAR(*found, value, 1e-6) << name << " in " << GetParam();
        }
    }

    TEST_P(PuddleTopoShader, the_fallback_runs_only_for_a_covered_pixel_with_no_capacity_and_the_option_set)
    {
        const std::string text = readShader(GetParam());
        ASSERT_FALSE(text.empty());
        EXPECT_NE(text.find("PUDDLE_OPTION_TOPOGRAPHIC) != 0u"), std::string::npos);
        const std::size_t none = text.find("!(capacity > 0.0)");
        const std::size_t option = text.find("PUDDLE_OPTION_TOPOGRAPHIC) != 0u", none);
        ASSERT_NE(none, std::string::npos);
        EXPECT_NE(option, std::string::npos);
        // The same ingredients as the C++ twin: the dip of the heights, the ramp of the supply, the finer noise.
        EXPECT_NE(text.find("puddleGroundDip(p)"), std::string::npos);
        EXPECT_NE(text.find("puddleTopoRamp(wet)"), std::string::npos);
        EXPECT_NE(text.find("wetNoise(pos / puddleTopoNoiseCell)"), std::string::npos);
        EXPECT_NE(text.find("puddleTopoMaxDepth * s.coverage"), std::string::npos);
        EXPECT_NE(text.find("mode == 5u"), std::string::npos);
        // The dip is the mean of four neighbours minus the texel, blended like the capacity.
        EXPECT_NE(text.find("0.25 * (puddleHeightOrElse(gx - 1, gy"), std::string::npos);
        EXPECT_NE(text.find("dip += weights[k] * (mean - centre.y)"), std::string::npos);
        EXPECT_NE(text.find("wetFlatness(normalZ)"), std::string::npos);
    }

    INSTANTIATE_TEST_SUITE_P(
        Twins, PuddleTopoShader, testing::Values("lib/material/puddlemap.glsl", "lib/material/puddlemap.slang"));

    TEST(PuddleTopoShaderUse, the_terrain_hands_the_fallback_its_real_normal)
    {
        for (const char* file : { "terrain/main.frag", "terrain/terrain.frag.slang" })
        {
            const std::string text = readShader(file);
            ASSERT_FALSE(text.empty()) << file;
            EXPECT_NE(text.find(".z, false)"), std::string::npos) << file;
            EXPECT_EQ(text.find("wetS.wet, 1.0, false"), std::string::npos) << file;
        }
    }
}
