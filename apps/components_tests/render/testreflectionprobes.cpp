// Tests for reflection probes and the `gi debug` view, Render::Gi (task 0028, stage d). Written from the task spec
// before the implementation: the math, the file format, the GPU layout, the debug markers and the baker.

#include <components/render/probebake.hpp>
#include <components/render/probedebug.hpp>
#include <components/render/reflectiongpu.hpp>
#include <components/render/reflectionmath.hpp>
#include <components/render/reflectionprobes.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    using namespace Render::Gi;

    float lengthOf(const vsg::vec3& v)
    {
        return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }

    float dotOf(const vsg::vec3& a, const vsg::vec3& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    std::vector<vsg::vec3> someDirections()
    {
        std::vector<vsg::vec3> out;
        for (std::uint32_t i = 0; i < 200; ++i)
            out.push_back(spiralDirection(i, 200, 0.37f));
        out.push_back(vsg::vec3(1, 0, 0));
        out.push_back(vsg::vec3(-1, 0, 0));
        out.push_back(vsg::vec3(0, 1, 0));
        out.push_back(vsg::vec3(0, -1, 0));
        out.push_back(vsg::vec3(0, 0, 1));
        out.push_back(vsg::vec3(0, 0, -1));
        return out;
    }

    // A map whose texel (x, y) is a distinct colour.
    std::vector<vsg::vec3> gradientMap(std::uint32_t resolution)
    {
        std::vector<vsg::vec3> map;
        for (std::uint32_t y = 0; y < resolution; ++y)
            for (std::uint32_t x = 0; x < resolution; ++x)
                map.emplace_back(static_cast<float>(x), static_cast<float>(y), static_cast<float>(x + y) * 0.5f);
        return map;
    }

    ReflectionSet sampleSet()
    {
        ReflectionSet set;
        set.cell = CellId{ true, "Example Hall", 0, 0 };
        set.samples = 4;
        set.bounces = 2;
        set.seed = 7;
        set.ambient = vsg::vec3(0.2f, 0.25f, 0.3f);
        set.resolution = 8;
        for (int p = 0; p < 2; ++p)
        {
            ReflectionProbe probe;
            probe.position = vsg::vec3(10.f * static_cast<float>(p), -5.5f, 100.f);
            for (std::uint32_t i = 0; i < 64; ++i)
                probe.texels.emplace_back(0.01f * static_cast<float>(i + p), 1e-5f, -0.f);
            set.probes.push_back(probe);
        }
        return set;
    }

    ProbeError mustFail(const std::string& text)
    {
        ProbeError error;
        EXPECT_FALSE(parseReflections(text, "x.reflections.json", error).has_value());
        return error;
    }

    std::string replaceOnce(std::string text, const std::string& from, const std::string& to)
    {
        const std::size_t at = text.find(from);
        EXPECT_NE(at, std::string::npos) << from;
        if (at != std::string::npos)
            text.replace(at, from.size(), to);
        return text;
    }

    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    // ---- fake worlds ----

    class BoxRoom : public RayWorld
    {
    public:
        // A closed room from `low` to `high`; a ray from inside hits the wall it reaches first.
        BoxRoom(vsg::vec3 low, vsg::vec3 high)
            : mLow(low)
            , mHigh(high)
        {
        }

        bool cast(const vsg::vec3& from, const vsg::vec3& to, vsg::vec3& hitPos, vsg::vec3& hitNormal) const override
        {
            const vsg::vec3 d = to - from;
            float best = 2.f;
            vsg::vec3 normal(0, 0, 1);
            for (int axis = 0; axis < 3; ++axis)
            {
                const float o = axis == 0 ? from.x : (axis == 1 ? from.y : from.z);
                const float dd = axis == 0 ? d.x : (axis == 1 ? d.y : d.z);
                const float lo = axis == 0 ? mLow.x : (axis == 1 ? mLow.y : mLow.z);
                const float hi = axis == 0 ? mHigh.x : (axis == 1 ? mHigh.y : mHigh.z);
                if (dd == 0.f)
                    continue;
                for (const float wall : { lo, hi })
                {
                    const float t = (wall - o) / dd;
                    if (t > 0.f && t <= 1.f && t < best && insideBox(from + d * t))
                    {
                        best = t;
                        normal = vsg::vec3(0, 0, 0);
                        (axis == 0 ? normal.x : (axis == 1 ? normal.y : normal.z)) = (wall == lo) ? 1.f : -1.f;
                    }
                }
            }
            if (best > 1.f)
                return false;
            hitPos = from + d * best;
            hitNormal = normal;
            return true;
        }

    private:
        // The walls are the faces of the box only, not planes without end.
        bool insideBox(const vsg::vec3& p) const
        {
            constexpr float slack = 1e-3f;
            return p.x >= mLow.x - slack && p.x <= mHigh.x + slack && p.y >= mLow.y - slack && p.y <= mHigh.y + slack
                && p.z >= mLow.z - slack && p.z <= mHigh.z + slack;
        }

        vsg::vec3 mLow, mHigh;
    };

    class OpenWorld : public RayWorld
    {
    public:
        bool cast(const vsg::vec3&, const vsg::vec3&, vsg::vec3&, vsg::vec3&) const override { return false; }
    };

    class RecordingSink : public SceneUtil::DebugPrimitiveSink
    {
    public:
        void addLine(const vsg::vec3&, const vsg::vec3&, const vsg::vec4&) override { ++lines; }
        void addTriangle(const vsg::vec3& a, const vsg::vec3& b, const vsg::vec3& c, const vsg::vec4& color) override
        {
            ++triangles;
            vertices.push_back(a);
            vertices.push_back(b);
            vertices.push_back(c);
            lastColor = color;
        }
        int lines = 0;
        int triangles = 0;
        std::vector<vsg::vec3> vertices;
        vsg::vec4 lastColor;
    };

    BakeSettings roomSettings()
    {
        BakeSettings s;
        s.rays = 32;
        s.bounces = 2;
        s.seed = 3;
        s.ambient = vsg::vec3(0.3f, 0.3f, 0.3f);
        return s;
    }

    Volume latticeFor(const vsg::vec3& low, const vsg::vec3& high, float spacing)
    {
        return layoutVolume(low, high, spacing, maxVolumeProbes);
    }
}

namespace Render::Gi
{
    // ---------------------------------------------------------------------------------------------
    // Octahedral maps and chains
    // ---------------------------------------------------------------------------------------------

    TEST(RenderReflectionMathTest, the_axes_land_on_known_points_of_the_map)
    {
        const vsg::vec2 up = octEncode(vsg::vec3(0, 0, 1));
        EXPECT_FLOAT_EQ(up.x, 0.5f);
        EXPECT_FLOAT_EQ(up.y, 0.5f);
        const vsg::vec2 px = octEncode(vsg::vec3(1, 0, 0));
        EXPECT_FLOAT_EQ(px.x, 1.f);
        EXPECT_FLOAT_EQ(px.y, 0.5f);
        const vsg::vec2 py = octEncode(vsg::vec3(0, -1, 0));
        EXPECT_FLOAT_EQ(py.x, 0.5f);
        EXPECT_FLOAT_EQ(py.y, 0.f);
        const vsg::vec2 down = octEncode(vsg::vec3(0, 0, -1));
        // straight down is a corner of the square
        EXPECT_TRUE((down.x == 0.f || down.x == 1.f) && (down.y == 0.f || down.y == 1.f));
    }

    TEST(RenderReflectionMathTest, decoding_an_encoded_direction_gives_it_back)
    {
        for (const vsg::vec3& dir : someDirections())
        {
            const vsg::vec3 back = octDecode(octEncode(dir));
            EXPECT_NEAR(lengthOf(back), 1.f, 1e-5f);
            EXPECT_GT(dotOf(back, dir), 0.9999f) << dir.x << " " << dir.y << " " << dir.z;
        }
    }

    TEST(RenderReflectionMathTest, the_encoding_ignores_the_length_and_survives_a_zero_vector)
    {
        const vsg::vec2 a = octEncode(vsg::vec3(0.2f, -0.4f, 0.1f));
        const vsg::vec2 b = octEncode(vsg::vec3(2.f, -4.f, 1.f));
        EXPECT_NEAR(a.x, b.x, 1e-6f);
        EXPECT_NEAR(a.y, b.y, 1e-6f);
        const vsg::vec2 zero = octEncode(vsg::vec3(0, 0, 0));
        EXPECT_TRUE(std::isfinite(zero.x) && std::isfinite(zero.y));
    }

    TEST(RenderReflectionMathTest, every_texel_centre_decodes_to_a_unit_direction_that_encodes_back_into_its_texel)
    {
        constexpr std::uint32_t res = 16;
        for (std::uint32_t y = 0; y < res; ++y)
            for (std::uint32_t x = 0; x < res; ++x)
            {
                const vsg::vec3 dir = reflectionTexelDirection(res, x, y);
                EXPECT_NEAR(lengthOf(dir), 1.f, 1e-5f);
                const vsg::vec2 uv = octEncode(dir);
                EXPECT_EQ(static_cast<std::uint32_t>(std::floor(uv.x * res)), x);
                EXPECT_EQ(static_cast<std::uint32_t>(std::floor(uv.y * res)), y);
            }
    }

    TEST(RenderReflectionMathTest, a_chain_has_halved_copies_down_to_four_by_four)
    {
        EXPECT_EQ(reflectionLevels(64), 5u);
        EXPECT_EQ(reflectionLevels(32), 4u);
        EXPECT_EQ(reflectionLevels(16), 3u);
        EXPECT_EQ(reflectionLevels(8), 2u);
        EXPECT_EQ(reflectionLevelOffset(32, 0), 0u);
        EXPECT_EQ(reflectionLevelOffset(32, 1), 1024u);
        EXPECT_EQ(reflectionLevelOffset(32, 2), 1024u + 256u);
        EXPECT_EQ(reflectionChainTexels(32), 1024u + 256u + 64u + 16u);
        EXPECT_EQ(buildReflectionChain(gradientMap(32), 32).size(), reflectionChainTexels(32));
    }

    TEST(RenderReflectionMathTest, the_first_copy_of_a_chain_is_the_map_and_the_next_averages_two_by_two)
    {
        const auto base = gradientMap(8);
        const auto chain = buildReflectionChain(base, 8);
        for (std::size_t i = 0; i < base.size(); ++i)
            EXPECT_EQ(chain[i], base[i]);
        // copy 1 texel (0,0) is the mean of base texels (0,0), (1,0), (0,1), (1,1)
        const vsg::vec3 expected = (base[0] + base[1] + base[8] + base[9]) * 0.25f;
        const vsg::vec3 got = chain[reflectionLevelOffset(8, 1)];
        EXPECT_NEAR(got.x, expected.x, 1e-6f);
        EXPECT_NEAR(got.y, expected.y, 1e-6f);
        EXPECT_NEAR(got.z, expected.z, 1e-6f);
    }

    TEST(RenderReflectionMathTest, a_constant_map_reads_as_that_constant_for_every_direction_and_roughness)
    {
        const vsg::vec3 value(0.25f, 0.5f, 0.75f);
        const auto chain = buildReflectionChain(std::vector<vsg::vec3>(32 * 32, value), 32);
        for (const vsg::vec3& dir : someDirections())
            for (const float roughness : { 0.f, 0.3f, 0.77f, 1.f })
            {
                const vsg::vec3 got = sampleReflectionChain(chain.data(), 32, dir, roughness);
                EXPECT_NEAR(got.x, value.x, 1e-5f);
                EXPECT_NEAR(got.y, value.y, 1e-5f);
                EXPECT_NEAR(got.z, value.z, 1e-5f);
            }
    }

    TEST(RenderReflectionMathTest, a_sharp_read_in_a_texel_centre_direction_gives_that_texel)
    {
        const auto base = gradientMap(16);
        const auto chain = buildReflectionChain(base, 16);
        for (const auto& [x, y] : { std::pair<std::uint32_t, std::uint32_t>{ 3, 5 }, { 8, 8 }, { 0, 15 } })
        {
            const vsg::vec3 got = sampleReflectionChain(chain.data(), 16, reflectionTexelDirection(16, x, y), 0.f);
            const vsg::vec3 expected = base[y * 16 + x];
            EXPECT_NEAR(got.x, expected.x, 1e-3f);
            EXPECT_NEAR(got.y, expected.y, 1e-3f);
        }
    }

    TEST(RenderReflectionMathTest, the_roughest_read_is_the_smallest_copy_and_the_lod_is_clamped)
    {
        EXPECT_FLOAT_EQ(reflectionLod(0.f, 4), 0.f);
        EXPECT_FLOAT_EQ(reflectionLod(1.f, 4), 3.f);
        EXPECT_FLOAT_EQ(reflectionLod(-2.f, 4), 0.f);
        EXPECT_FLOAT_EQ(reflectionLod(7.f, 4), 3.f);
        EXPECT_FLOAT_EQ(reflectionLod(0.5f, 4), 1.5f);
    }

    TEST(RenderReflectionMathTest, the_environment_brdf_is_finite_and_not_negative_and_never_reflects_more_than_all)
    {
        for (float r = 0.f; r <= 1.0001f; r += 0.1f)
            for (float v = 0.01f; v <= 1.0001f; v += 0.1f)
            {
                const vsg::vec2 ab = envBrdfScaleBias(r, v);
                EXPECT_TRUE(std::isfinite(ab.x) && std::isfinite(ab.y));
                EXPECT_GE(ab.x, 0.f);
                EXPECT_GE(ab.y, 0.f);
                EXPECT_LE(ab.x + ab.y, 1.05f) << "r " << r << " v " << v;
            }
    }

    TEST(RenderReflectionMathTest, a_smooth_surface_seen_head_on_reflects_by_its_f0)
    {
        const vsg::vec2 ab = envBrdfScaleBias(0.05f, 1.f);
        EXPECT_NEAR(ab.x + ab.y, 1.f, 0.05f);
        EXPECT_LT(ab.y, 0.1f);
    }

    TEST(RenderReflectionMathTest, no_probes_gives_an_empty_blend_and_one_probe_gives_it_all)
    {
        const vsg::vec3 p[1] = { vsg::vec3(5, 5, 5) };
        EXPECT_EQ(reflectionBlend(p, 0, vsg::vec3(0, 0, 0)).count, 0u);
        const auto one = reflectionBlend(p, 1, vsg::vec3(100, 0, 0));
        EXPECT_EQ(one.count, 1u);
        EXPECT_EQ(one.index[0], 0u);
        EXPECT_FLOAT_EQ(one.weight[0], 1.f);
    }

    TEST(RenderReflectionMathTest, two_probes_weigh_by_the_other_ones_distance)
    {
        const vsg::vec3 p[2] = { vsg::vec3(0, 0, 0), vsg::vec3(100, 0, 0) };
        const auto blend = reflectionBlend(p, 2, vsg::vec3(25, 0, 0));
        ASSERT_EQ(blend.count, 2u);
        EXPECT_EQ(blend.index[0], 0u);
        EXPECT_EQ(blend.index[1], 1u);
        EXPECT_NEAR(blend.weight[0], 0.75f, 1e-5f);
        EXPECT_NEAR(blend.weight[1], 0.25f, 1e-5f);
    }

    TEST(RenderReflectionMathTest, many_probes_use_the_two_nearest_and_the_weights_add_to_one)
    {
        const vsg::vec3 p[4]
            = { vsg::vec3(0, 0, 0), vsg::vec3(100, 0, 0), vsg::vec3(0, 300, 0), vsg::vec3(900, 900, 0) };
        const auto blend = reflectionBlend(p, 4, vsg::vec3(30, 0, 0));
        ASSERT_EQ(blend.count, 2u);
        EXPECT_EQ(blend.index[0], 0u);
        EXPECT_EQ(blend.index[1], 1u);
        EXPECT_NEAR(blend.weight[0] + blend.weight[1], 1.f, 1e-5f);
        EXPECT_GT(blend.weight[0], blend.weight[1]);
    }

    TEST(RenderReflectionMathTest, the_blend_does_not_jump_when_the_second_nearest_probe_changes)
    {
        // a point moves in small steps; the weights change only a little with each step.
        const vsg::vec3 p[3] = { vsg::vec3(0, 0, 0), vsg::vec3(100, 0, 0), vsg::vec3(0, 160, 0) };
        const vsg::vec3 before(100, 60, 0);
        const vsg::vec3 after(100, 61, 0);
        const auto a = reflectionBlend(p, 3, before);
        const auto b = reflectionBlend(p, 3, after);
        // a small step changes the weights only a little
        for (int i = 0; i < 2; ++i)
        {
            if (a.index[i] == b.index[i])
            {
                EXPECT_NEAR(a.weight[i], b.weight[i], 0.05f);
            }
        }
    }

    TEST(RenderReflectionMathTest, equal_distances_pick_the_lower_index_first)
    {
        const vsg::vec3 p[3] = { vsg::vec3(-10, 0, 0), vsg::vec3(10, 0, 0), vsg::vec3(0, 50, 0) };
        const auto blend = reflectionBlend(p, 3, vsg::vec3(0, 0, 0));
        EXPECT_EQ(blend.index[0], 0u);
        EXPECT_EQ(blend.index[1], 1u);
        EXPECT_NEAR(blend.weight[0], blend.weight[1], 1e-5f);
    }

    // ---------------------------------------------------------------------------------------------
    // The file
    // ---------------------------------------------------------------------------------------------

    TEST(RenderReflectionFileTest, the_path_sits_beside_the_light_probe_file)
    {
        const CellId cell{ true, "Example Hall", 0, 0 };
        const std::string probes = probeFilePath(cell);
        const std::string reflections = reflectionFilePath(cell);
        EXPECT_EQ(reflections, probes.substr(0, probes.size() - std::string(".json").size()) + ".reflections.json");
    }

    TEST(RenderReflectionFileTest, a_valid_set_has_no_problem)
    {
        EXPECT_EQ(validate(sampleSet()), "");
    }

    TEST(RenderReflectionFileTest, the_canonical_text_starts_with_the_fixed_header)
    {
        const std::string text = writeCanonical(sampleSet());
        EXPECT_TRUE(
            text.rfind("{\n  \"format\": \"openeden.reflections\",\n  \"version\": 1,\n  \"cell\": {\n", 0) == 0)
            << text.substr(0, 120);
        EXPECT_EQ(text.back(), '\n');
        EXPECT_EQ(text.find('\r'), std::string::npos);
    }

    TEST(RenderReflectionFileTest, writing_then_reading_then_writing_gives_the_same_bytes)
    {
        const std::string text = writeCanonical(sampleSet());
        ProbeError error;
        const auto parsed = parseReflections(text, "x.json", error);
        ASSERT_TRUE(parsed.has_value()) << error.describe();
        EXPECT_EQ(writeCanonical(*parsed), text);
    }

    TEST(RenderReflectionFileTest, reading_gives_back_the_set_with_floats_bit_equal)
    {
        const ReflectionSet set = sampleSet();
        ProbeError error;
        const auto parsed = parseReflections(writeCanonical(set), "x.json", error);
        ASSERT_TRUE(parsed.has_value()) << error.describe();
        EXPECT_EQ(parsed->cell.name, set.cell.name);
        EXPECT_TRUE(parsed->cell.interior);
        EXPECT_EQ(parsed->samples, set.samples);
        EXPECT_EQ(parsed->bounces, set.bounces);
        EXPECT_EQ(parsed->seed, set.seed);
        EXPECT_EQ(parsed->resolution, set.resolution);
        ASSERT_EQ(parsed->probes.size(), set.probes.size());
        for (std::size_t p = 0; p < set.probes.size(); ++p)
        {
            EXPECT_EQ(std::memcmp(&parsed->probes[p].position, &set.probes[p].position, sizeof(vsg::vec3)), 0);
            ASSERT_EQ(parsed->probes[p].texels.size(), set.probes[p].texels.size());
            EXPECT_EQ(std::memcmp(parsed->probes[p].texels.data(), set.probes[p].texels.data(),
                          sizeof(vsg::vec3) * set.probes[p].texels.size()),
                0);
        }
        EXPECT_EQ(std::memcmp(&parsed->ambient, &set.ambient, sizeof(vsg::vec3)), 0);
    }

    TEST(RenderReflectionFileTest, a_file_that_is_not_canonical_but_valid_still_reads)
    {
        std::string text = writeCanonical(sampleSet());
        text = replaceOnce(text, "\"version\": 1,", "\"version\"  :   1  ,\n\n\n");
        ProbeError error;
        EXPECT_TRUE(parseReflections(text, "x.json", error).has_value()) << error.describe();
    }

    TEST(RenderReflectionFileTest, the_error_names_the_file_line_and_field)
    {
        const std::string text = writeCanonical(sampleSet());
        const ProbeError error = mustFail(replaceOnce(text, "\"resolution\": 8", "\"resolution\": 9"));
        EXPECT_EQ(error.file, "x.reflections.json");
        EXPECT_GT(error.line, 1);
        EXPECT_TRUE(contains(error.field, "resolution")) << error.field;
        EXPECT_TRUE(contains(error.describe(), "x.reflections.json:"));
    }

    TEST(RenderReflectionFileTest, a_wrong_format_name_or_version_is_refused_with_the_field)
    {
        const std::string text = writeCanonical(sampleSet());
        EXPECT_TRUE(contains(mustFail(replaceOnce(text, "openeden.reflections", "openeden.probes")).field, "format"));
        EXPECT_TRUE(contains(mustFail(replaceOnce(text, "\"version\": 1,", "\"version\": 2,")).field, "version"));
        EXPECT_TRUE(contains(mustFail(replaceOnce(text, "\"version\": 1,", "\"version\": 1.0,")).field, "version"));
    }

    TEST(RenderReflectionFileTest, an_unknown_or_missing_key_is_refused)
    {
        const std::string text = writeCanonical(sampleSet());
        EXPECT_TRUE(mustFail(replaceOnce(text, "  \"version\": 1,\n", "  \"version\": 1,\n  \"extra\": 1,\n"))
                        .message.find("extra")
            != std::string::npos);
        EXPECT_FALSE(mustFail(replaceOnce(text, "\"resolution\": 8,", "")).message.empty());
    }

    TEST(RenderReflectionFileTest, a_probe_with_the_wrong_number_of_rows_or_numbers_is_refused_with_its_location)
    {
        const std::string text = writeCanonical(sampleSet());
        // drop the last number of the first texel row of probe 0
        const std::size_t first = text.find("\"texels\"");
        ASSERT_NE(first, std::string::npos);
        const std::size_t rowStart = text.find('[', text.find('[', first) + 1);
        const std::size_t rowEnd = text.find(']', rowStart);
        const std::size_t lastComma = text.rfind(',', rowEnd);
        std::string broken = text;
        broken.erase(lastComma, rowEnd - lastComma);
        const ProbeError error = mustFail(broken);
        EXPECT_TRUE(contains(error.field, "probes[0].texels[0]")) << error.field;
    }

    TEST(RenderReflectionFileTest, zero_or_too_many_probes_are_refused)
    {
        ReflectionSet none = sampleSet();
        none.probes.clear();
        EXPECT_NE(validate(none), "");
        ReflectionSet many = sampleSet();
        while (many.probes.size() <= maxReflectionProbes)
            many.probes.push_back(many.probes.front());
        EXPECT_NE(validate(many), "");
    }

    TEST(RenderReflectionFileTest, a_bad_resolution_or_a_wrong_texel_count_or_a_non_finite_number_is_invalid)
    {
        ReflectionSet badRes = sampleSet();
        badRes.resolution = 12;
        EXPECT_NE(validate(badRes), "");
        ReflectionSet wrongCount = sampleSet();
        wrongCount.probes[0].texels.pop_back();
        EXPECT_NE(validate(wrongCount), "");
        ReflectionSet nan = sampleSet();
        nan.probes[1].texels[3].y = std::nanf("");
        EXPECT_NE(validate(nan), "");
        ReflectionSet badPos = sampleSet();
        badPos.probes[0].position.x = INFINITY;
        EXPECT_NE(validate(badPos), "");
    }

    TEST(RenderReflectionFileTest, a_hostile_file_does_not_crash_the_reader)
    {
        for (const std::string& text : { std::string(), std::string("{"), std::string("[]"), std::string("null"),
                 std::string("{\"format\":\"openeden.reflections\",\"version\":1}"), std::string(100000, '[') })
        {
            ProbeError error;
            EXPECT_FALSE(parseReflections(text, "x.json", error).has_value());
            EXPECT_FALSE(error.message.empty());
        }
    }

    // ---------------------------------------------------------------------------------------------
    // The GPU layout
    // ---------------------------------------------------------------------------------------------

    TEST(RenderReflectionGpuTest, the_header_holds_count_resolution_levels_enabled_strength_and_baked_ambient)
    {
        const auto packed = packReflections(sampleSet(), 1.5f);
        EXPECT_EQ(packed[0], vsg::vec4(2.f, 8.f, 2.f, 1.f));
        EXPECT_EQ(packed[1], vsg::vec4(1.5f, 0.2f, 0.25f, 0.3f));
    }

    TEST(RenderReflectionGpuTest, positions_are_at_three_plus_p_and_the_texels_follow_the_position_block)
    {
        const ReflectionSet set = sampleSet();
        const auto packed = packReflections(set, 1.f);
        EXPECT_EQ(packed.size(), reflectionPackedSize(2, 8));
        EXPECT_EQ(reflectionTexelsBase, 3u + maxReflectionProbes);
        EXPECT_EQ(packed[3], vsg::vec4(0.f, -5.5f, 100.f, 0.f));
        EXPECT_EQ(packed[4], vsg::vec4(10.f, -5.5f, 100.f, 0.f));
        const std::size_t chain = reflectionChainTexels(8);
        EXPECT_EQ(chain, 64u + 16u);
        // the first texel of each probe's chain
        EXPECT_EQ(packed[reflectionTexelsBase], vsg::vec4(0.f, 1e-5f, -0.f, 0.f));
        EXPECT_EQ(packed[reflectionTexelsBase + chain].x, 0.01f);
    }

    TEST(RenderReflectionGpuTest, the_smaller_copies_follow_the_base_map_of_each_probe)
    {
        const ReflectionSet set = sampleSet();
        const auto packed = packReflections(set, 1.f);
        const auto chain = buildReflectionChain(set.probes[0].texels, 8);
        const std::size_t at = reflectionTexelsBase + reflectionLevelOffset(8, 1) + 5;
        EXPECT_EQ(packed[at].x, chain[reflectionLevelOffset(8, 1) + 5].x);
    }

    TEST(RenderReflectionGpuTest, the_buffer_capacity_holds_sixteen_probes_of_the_largest_map)
    {
        EXPECT_EQ(reflectionBufferCapacity, 19u + 16u * (4096u + 1024u + 256u + 64u + 16u));
        EXPECT_GE(reflectionBufferCapacity, reflectionPackedSize(maxReflectionProbes, maxReflectionResolution));
    }

    // ---------------------------------------------------------------------------------------------
    // The debug view
    // ---------------------------------------------------------------------------------------------

    TEST(RenderProbeDebugTest, a_marker_is_an_octahedron_of_eight_triangles_around_its_centre)
    {
        RecordingSink sink;
        drawDebugMarker(sink, vsg::vec3(10, 20, 30), 4.f, vsg::vec4(1, 0, 0, 1));
        EXPECT_EQ(sink.triangles, 8);
        EXPECT_EQ(sink.lines, 0);
        ASSERT_EQ(sink.vertices.size(), debugMarkerVertices);
        for (const vsg::vec3& v : sink.vertices)
            EXPECT_NEAR(lengthOf(v - vsg::vec3(10, 20, 30)), 4.f, 1e-4f);
    }

    TEST(RenderProbeDebugTest, there_is_one_marker_per_light_probe_at_its_lattice_position)
    {
        Volume v;
        v.origin = vsg::vec3(100, 0, 0);
        v.spacing = 50.f;
        v.size = { 2, 2, 1 };
        v.probes.assign(4, uniformProbe(vsg::vec3(0.5f, 0.5f, 0.5f)));
        RecordingSink sink;
        drawProbes(sink, v);
        EXPECT_EQ(sink.triangles, 4 * 8);
        // the fourth probe sits at (150, 50, 0): its marker's vertices are within the radius of that point
        float nearest = 1e9f;
        for (const vsg::vec3& p : sink.vertices)
            nearest = std::min(nearest, lengthOf(p - vsg::vec3(150, 50, 0)));
        EXPECT_NEAR(nearest, v.spacing * debugProbeRadiusFraction, 1e-3f);
    }

    TEST(RenderProbeDebugTest, a_markers_colour_follows_the_probe_and_is_inside_zero_to_one)
    {
        Volume v;
        v.size = { 1, 1, 1 };
        v.probes.assign(1, uniformProbe(vsg::vec3(0.25f, 0.25f, 0.25f)));
        RecordingSink dim;
        drawProbes(dim, v);
        v.probes[0] = uniformProbe(vsg::vec3(40.f, 0.f, -3.f));
        RecordingSink bright;
        drawProbes(bright, v);
        EXPECT_GT(dim.lastColor.x, 0.f);
        EXPECT_LT(dim.lastColor.x, 1.f);
        EXPECT_LE(bright.lastColor.x, 1.f);
        EXPECT_GE(bright.lastColor.z, 0.f);
        EXPECT_GT(bright.lastColor.x, dim.lastColor.x);
        EXPECT_FLOAT_EQ(dim.lastColor.w, debugMarkerAlpha);
    }

    TEST(RenderProbeDebugTest, there_is_one_marker_per_reflection_probe)
    {
        RecordingSink sink;
        drawReflectionProbes(sink, sampleSet());
        EXPECT_EQ(sink.triangles, 2 * 8);
    }

    // ---------------------------------------------------------------------------------------------
    // The baker
    // ---------------------------------------------------------------------------------------------

    TEST(RenderReflectionBakeTest, a_texel_that_looks_at_a_near_wall_is_darker_than_one_that_escapes)
    {
        // an open world gives the flat ambient for every texel; a wall 50 units away gives openness-weighted ambient
        const BakeSettings settings = roomSettings();
        ReflectionSettings reflection;
        reflection.resolution = 8;
        reflection.samples = 2;
        OpenWorld open;
        const auto openMap = bakeReflectionMap(settings, open, {}, vsg::vec3(0, 0, 0), 0, reflection);
        ASSERT_EQ(openMap.size(), 64u);
        for (const vsg::vec3& t : openMap)
            EXPECT_NEAR(t.x, 0.3f, 1e-5f);

        BoxRoom room(vsg::vec3(-50, -50, -50), vsg::vec3(50, 50, 50));
        BakeSettings noBounce = settings;
        noBounce.bounces = 0;
        const auto roomMap = bakeReflectionMap(noBounce, room, {}, vsg::vec3(0, 0, 0), 0, reflection);
        for (const vsg::vec3& t : roomMap)
        {
            EXPECT_LT(t.x, 0.3f);
            EXPECT_GT(t.x, 0.f);
        }
    }

    TEST(RenderReflectionBakeTest, a_light_in_the_room_brightens_the_wall_nearest_it)
    {
        BoxRoom room(vsg::vec3(-200, -200, -200), vsg::vec3(200, 200, 200));
        BakeSettings settings = roomSettings();
        settings.falloff.linearMethod = 1;
        ReflectionSettings reflection;
        reflection.resolution = 16;
        reflection.samples = 4;
        std::vector<BakeLight> lights{ BakeLight{ vsg::vec3(150, 0, 0), vsg::vec3(3, 3, 3), 500.f } };
        const auto map = bakeReflectionMap(settings, room, lights, vsg::vec3(0, 0, 0), 0, reflection);
        const vsg::vec3 towardLight = map[8 * 16 + 15];
        const vsg::vec3 away = map[8 * 16 + 0];
        // texel (15, 8) looks at +X (the light's wall), texel (0, 8) at -X
        EXPECT_GT(towardLight.x, away.x);
    }

    TEST(RenderReflectionBakeTest, the_same_arguments_give_the_same_bits_whatever_was_baked_before)
    {
        BoxRoom room(vsg::vec3(-100, -100, -100), vsg::vec3(100, 100, 100));
        const BakeSettings settings = roomSettings();
        ReflectionSettings reflection;
        reflection.resolution = 8;
        std::vector<BakeLight> lights{ BakeLight{ vsg::vec3(30, 20, 10), vsg::vec3(1, 0.8f, 0.5f), 300.f } };
        const auto a = bakeReflectionMap(settings, room, lights, vsg::vec3(10, 0, 0), 2, reflection);
        (void)bakeReflectionMap(settings, room, lights, vsg::vec3(0, 0, 0), 1, reflection);
        const auto b = bakeReflectionMap(settings, room, lights, vsg::vec3(10, 0, 0), 2, reflection);
        ASSERT_EQ(a.size(), b.size());
        EXPECT_EQ(std::memcmp(a.data(), b.data(), a.size() * sizeof(vsg::vec3)), 0);
    }

    TEST(RenderReflectionBakeTest, a_different_seed_or_probe_index_gives_different_bounces)
    {
        BoxRoom room(vsg::vec3(-100, -100, -100), vsg::vec3(100, 100, 100));
        BakeSettings settings = roomSettings();
        ReflectionSettings reflection;
        reflection.resolution = 8;
        std::vector<BakeLight> lights{ BakeLight{ vsg::vec3(30, 20, 10), vsg::vec3(1, 0.8f, 0.5f), 300.f } };
        const auto a = bakeReflectionMap(settings, room, lights, vsg::vec3(10, 0, 0), 2, reflection);
        settings.seed = 99;
        const auto b = bakeReflectionMap(settings, room, lights, vsg::vec3(10, 0, 0), 2, reflection);
        EXPECT_NE(std::memcmp(a.data(), b.data(), a.size() * sizeof(vsg::vec3)), 0);
    }

    TEST(RenderReflectionBakeTest, positions_are_inside_the_room_apart_from_each_other_and_at_most_the_maximum)
    {
        BoxRoom room(vsg::vec3(0, 0, 0), vsg::vec3(1000, 800, 300));
        const BakeSettings settings = roomSettings();
        const Volume lattice = latticeFor(vsg::vec3(0, 0, 0), vsg::vec3(1000, 800, 300), 128.f);
        ReflectionSettings reflection;
        reflection.maxProbes = 5;
        reflection.minDistance = 300.f;
        const auto positions = chooseReflectionPositions(settings, room, lattice, reflection);
        ASSERT_FALSE(positions.empty());
        EXPECT_LE(positions.size(), 5u);
        for (std::size_t i = 0; i < positions.size(); ++i)
        {
            EXPECT_GT(positions[i].x, 0.f);
            EXPECT_LT(positions[i].x, 1000.f);
            EXPECT_GT(positions[i].y, 0.f);
            EXPECT_LT(positions[i].y, 800.f);
            for (std::size_t j = 0; j < i; ++j)
                EXPECT_GE(lengthOf(positions[i] - positions[j]), 300.f - 1e-3f);
        }
    }

    TEST(RenderReflectionBakeTest, a_small_room_gets_one_probe_and_the_choice_is_repeatable)
    {
        BoxRoom room(vsg::vec3(0, 0, 0), vsg::vec3(200, 200, 200));
        const BakeSettings settings = roomSettings();
        const Volume lattice = latticeFor(vsg::vec3(0, 0, 0), vsg::vec3(200, 200, 200), 64.f);
        ReflectionSettings reflection;
        reflection.minDistance = 4000.f;
        const auto a = chooseReflectionPositions(settings, room, lattice, reflection);
        const auto b = chooseReflectionPositions(settings, room, lattice, reflection);
        EXPECT_EQ(a.size(), 1u);
        ASSERT_EQ(a.size(), b.size());
        EXPECT_EQ(a[0], b[0]);
    }

    TEST(RenderReflectionBakeTest, an_open_world_has_no_room_to_put_a_probe_in)
    {
        OpenWorld open;
        const Volume lattice = latticeFor(vsg::vec3(0, 0, 0), vsg::vec3(500, 500, 100), 128.f);
        EXPECT_TRUE(chooseReflectionPositions(roomSettings(), open, lattice, ReflectionSettings{}).empty());
    }

    TEST(RenderReflectionBakeTest, the_baker_in_slices_gives_the_same_probes_as_in_one_go_and_resumes)
    {
        BoxRoom room(vsg::vec3(0, 0, 0), vsg::vec3(400, 400, 200));
        const BakeSettings settings = roomSettings();
        ReflectionSettings reflection;
        reflection.resolution = 8;
        reflection.samples = 2;
        const std::vector<vsg::vec3> positions{ vsg::vec3(100, 100, 100), vsg::vec3(300, 100, 100),
            vsg::vec3(200, 300, 100) };

        ReflectionBaker whole(settings, reflection, room, {}, positions);
        EXPECT_TRUE(whole.step(100));
        ASSERT_TRUE(whole.done());
        ASSERT_EQ(whole.probes().size(), 3u);

        ReflectionBaker sliced(settings, reflection, room, {}, positions);
        EXPECT_FALSE(sliced.step(1));
        EXPECT_EQ(sliced.next(), 1u);
        const std::vector<ReflectionProbe> saved = sliced.probes();

        ReflectionBaker resumed(settings, reflection, room, {}, positions);
        EXPECT_TRUE(resumed.resume(saved, 1));
        EXPECT_EQ(resumed.next(), 1u);
        EXPECT_TRUE(resumed.step(10));
        ASSERT_EQ(resumed.probes().size(), 3u);
        for (std::size_t p = 0; p < 3; ++p)
        {
            EXPECT_EQ(resumed.probes()[p].position, whole.probes()[p].position);
            EXPECT_EQ(
                std::memcmp(resumed.probes()[p].texels.data(), whole.probes()[p].texels.data(), 64 * sizeof(vsg::vec3)),
                0);
        }
    }

    TEST(RenderReflectionBakeTest, resuming_from_probes_at_other_positions_is_refused)
    {
        BoxRoom room(vsg::vec3(0, 0, 0), vsg::vec3(400, 400, 200));
        const BakeSettings settings = roomSettings();
        ReflectionSettings reflection;
        reflection.resolution = 8;
        const std::vector<vsg::vec3> positions{ vsg::vec3(100, 100, 100), vsg::vec3(300, 100, 100) };
        ReflectionBaker other(settings, reflection, room, {}, { vsg::vec3(5, 5, 5), vsg::vec3(300, 100, 100) });
        other.step(1);
        ReflectionBaker baker(settings, reflection, room, {}, positions);
        EXPECT_FALSE(baker.resume(other.probes(), 1));
        EXPECT_EQ(baker.next(), 0u);
    }
}
