// Tests for Render::Gi's probe file format, file naming and spherical harmonics maths (task 0028, stage a).
//
// Written from the spec alone, without reading the implementation.

#include <components/render/probes.hpp>
#include <components/render/probesamplermath.hpp>

#include <vsg/maths/vec3.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

namespace
{
    using namespace Render::Gi;

    // ---------------------------------------------------------------------------------------------
    // Helpers for building canonical text
    // ---------------------------------------------------------------------------------------------

    // Numbers whose shortest round-trip text (std::to_chars) is the text itself.
    const std::array<std::string, 9> tokenPalette = { "0", "0.5", "-0.25", "1", "0.125", "1e-05", "-0", "0.1", "2" };

    std::vector<std::string> probeTokens(int index, int count = 27)
    {
        std::vector<std::string> tokens;
        for (int k = 0; k < count; ++k)
            tokens.push_back(tokenPalette[static_cast<std::size_t>((index * 7 + k) % 9)]);
        return tokens;
    }

    std::string joinWith(const std::vector<std::string>& parts, const std::string& separator)
    {
        std::string result;
        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            if (i > 0)
                result += separator;
            result += parts[i];
        }
        return result;
    }

    std::string probeText(const std::vector<std::string>& tokens)
    {
        return "[" + joinWith(tokens, ", ") + "]";
    }

    // One volume object, as it appears in canonical text (four spaces of indent). `probeTexts` are whole "[...]".
    std::string volumeBlock(const std::string& origin, const std::string& spacing, const std::string& size,
        const std::vector<std::string>& probeTexts)
    {
        std::string text = "    {\n";
        text += "      \"origin\": " + origin + ",\n";
        text += "      \"spacing\": " + spacing + ",\n";
        text += "      \"size\": " + size + ",\n";
        text += "      \"probes\": [\n";
        for (std::size_t i = 0; i < probeTexts.size(); ++i)
        {
            text += "        " + probeTexts[i];
            text += (i + 1 < probeTexts.size()) ? ",\n" : "\n";
        }
        text += "      ]\n";
        text += "    }";
        return text;
    }

    // `cellValue` is the content of the "cell" object, e.g. "\"interior\": \"Example Hall\"".
    std::string sampleText(const std::string& cellValue, const std::vector<std::string>& volumeBlocks)
    {
        std::string text = "{\n";
        text += "  \"format\": \"openeden.probes\",\n";
        text += "  \"version\": 1,\n";
        text += "  \"cell\": {\n";
        text += "    " + cellValue + "\n";
        text += "  },\n";
        text += "  \"bake\": {\n";
        text += "    \"rays\": 256,\n";
        text += "    \"bounces\": 2,\n";
        text += "    \"seed\": 1,\n";
        text += "    \"ambient\": [0.2, 0.2, 0.2]\n";
        text += "  },\n";
        text += "  \"volumes\": [\n";
        text += joinWith(volumeBlocks, ",\n");
        text += "\n  ]\n";
        text += "}\n";
        return text;
    }

    // Interior "Example Hall", one volume of 2 probes. Line numbers (1-based) of this text:
    //  2 format, 3 version, 5 interior, 8 rays, 9 bounces, 10 seed, 11 ambient,
    // 15 origin, 16 spacing, 17 size, 19 probes[0], 20 probes[1].
    std::string sampleWithProbe1(const std::vector<std::string>& probe1)
    {
        return sampleText("\"interior\": \"Example Hall\"",
            { volumeBlock("[0, 0, 0]", "128", "[2, 1, 1]", { probeText(probeTokens(0)), probeText(probe1) }) });
    }

    std::string interiorSample()
    {
        return sampleWithProbe1(probeTokens(1));
    }

    std::string exteriorSample()
    {
        return sampleText("\"exterior\": [-3, 7]",
            { volumeBlock(
                "[-1024, 2048.5, 0]", "256", "[1, 2, 1]", { probeText(probeTokens(2)), probeText(probeTokens(3)) }) });
    }

    std::string twoVolumeSample()
    {
        return sampleText("\"interior\": \"Example Hall\"",
            { volumeBlock("[0, 0, 0]", "128", "[2, 1, 1]", { probeText(probeTokens(0)), probeText(probeTokens(1)) }),
                volumeBlock("[-128.5, 0.25, 1e-05]", "64", "[1, 2, 1]",
                    { probeText(probeTokens(4)), probeText(probeTokens(5)) }) });
    }

    std::string replaceOnce(std::string text, const std::string& from, const std::string& to)
    {
        const std::size_t at = text.find(from);
        EXPECT_NE(at, std::string::npos) << "test bug: '" << from << "' not found";
        if (at != std::string::npos)
            text.replace(at, from.size(), to);
        return text;
    }

    // ---------------------------------------------------------------------------------------------
    // Helpers for comparing
    // ---------------------------------------------------------------------------------------------

    bool sameBits(float a, float b)
    {
        return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
    }

    bool sameBits(const vsg::vec3& a, const vsg::vec3& b)
    {
        return sameBits(a.x, b.x) && sameBits(a.y, b.y) && sameBits(a.z, b.z);
    }

    void expectNear(const vsg::vec3& expected, const vsg::vec3& actual, float tolerance)
    {
        EXPECT_NEAR(expected.x, actual.x, tolerance);
        EXPECT_NEAR(expected.y, actual.y, tolerance);
        EXPECT_NEAR(expected.z, actual.z, tolerance);
    }

    void expectSameSet(const ProbeSet& a, const ProbeSet& b)
    {
        EXPECT_EQ(a.cell.interior, b.cell.interior);
        EXPECT_EQ(a.cell.name, b.cell.name);
        EXPECT_EQ(a.cell.x, b.cell.x);
        EXPECT_EQ(a.cell.y, b.cell.y);
        EXPECT_EQ(a.rays, b.rays);
        EXPECT_EQ(a.bounces, b.bounces);
        EXPECT_EQ(a.seed, b.seed);
        EXPECT_TRUE(sameBits(a.ambient, b.ambient));
        ASSERT_EQ(a.volumes.size(), b.volumes.size());
        for (std::size_t v = 0; v < a.volumes.size(); ++v)
        {
            const Volume& va = a.volumes[v];
            const Volume& vb = b.volumes[v];
            EXPECT_TRUE(sameBits(va.origin, vb.origin));
            EXPECT_TRUE(sameBits(va.spacing, vb.spacing));
            EXPECT_EQ(va.size, vb.size);
            ASSERT_EQ(va.probes.size(), vb.probes.size());
            for (std::size_t p = 0; p < va.probes.size(); ++p)
                for (int k = 0; k < shCoefficients; ++k)
                    EXPECT_TRUE(sameBits(va.probes[p].c[k], vb.probes[p].c[k]))
                        << "volume " << v << " probe " << p << " coefficient " << k;
        }
    }

    // Parses text that must be valid.
    ProbeSet mustParse(const std::string& text)
    {
        ProbeError error;
        const auto set = parseProbes(text, "test.json", error);
        EXPECT_TRUE(set.has_value()) << error.describe();
        return set.value_or(ProbeSet{});
    }

    // Parses text that must be refused; checks the error is located and returns it.
    ProbeError mustFail(const std::string& text)
    {
        ProbeError error;
        const auto set = parseProbes(text, "bad.json", error);
        EXPECT_FALSE(set.has_value());
        EXPECT_EQ(error.file, "bad.json");
        EXPECT_GT(error.line, 0);
        EXPECT_FALSE(error.message.empty());
        EXPECT_EQ(error.describe().rfind("bad.json:", 0), 0u) << error.describe();
        return error;
    }

    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    // ---------------------------------------------------------------------------------------------
    // Helpers for the file naming tests (the test's own implementation of the spec's rules)
    // ---------------------------------------------------------------------------------------------

    std::uint32_t fnv1a32(const std::string& bytes)
    {
        std::uint32_t hash = 2166136261u;
        for (unsigned char byte : bytes)
        {
            hash ^= byte;
            hash *= 16777619u;
        }
        return hash;
    }

    std::string lowerAscii(std::string text)
    {
        for (char& c : text)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        return text;
    }

    std::string expectedInteriorKey(const std::string& name)
    {
        const std::string lower = lowerAscii(name);
        std::string sanitized;
        for (char c : lower)
        {
            const bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
            const char out = keep ? c : '_';
            if (out == '_' && !sanitized.empty() && sanitized.back() == '_')
                continue;
            sanitized.push_back(out);
        }
        while (!sanitized.empty() && sanitized.back() == '_')
            sanitized.pop_back();
        while (!sanitized.empty() && sanitized.front() == '_')
            sanitized.erase(sanitized.begin());
        if (sanitized.empty())
            sanitized = "cell";
        if (sanitized.size() > 48)
            sanitized.resize(48);
        char hex[16];
        std::snprintf(hex, sizeof(hex), "%08x", static_cast<unsigned>(fnv1a32(lower)));
        return "interior/" + sanitized + "-" + hex;
    }

    CellId interiorCell(const std::string& name)
    {
        CellId id;
        id.interior = true;
        id.name = name;
        return id;
    }

    // ---------------------------------------------------------------------------------------------
    // Helpers for the maths tests
    // ---------------------------------------------------------------------------------------------

    constexpr float pi = std::numbers::pi_v<float>;

    // N roughly evenly spread unit directions on the sphere.
    std::vector<vsg::vec3> fibonacciSphere(int count)
    {
        std::vector<vsg::vec3> directions;
        const float goldenAngle = pi * (3.f - std::sqrt(5.f));
        for (int i = 0; i < count; ++i)
        {
            const float z = 1.f - 2.f * (static_cast<float>(i) + 0.5f) / static_cast<float>(count);
            const float r = std::sqrt(std::max(0.f, 1.f - z * z));
            const float phi = goldenAngle * static_cast<float>(i);
            directions.push_back(vsg::vec3(r * std::cos(phi), r * std::sin(phi), z));
        }
        return directions;
    }

    // A probe of a uniform radiance field: only the band 0 coefficient is non-zero, and shAmbient gives `radiance` back
    // (band 0 coefficient = radiance * integral of the basis over the sphere = radiance * 4 pi * 0.282095).
    Sh9 uniformProbe(const vsg::vec3& radiance)
    {
        Sh9 sh;
        const float factor = 4.f * pi * 0.282095f;
        sh.c[0] = vsg::vec3(radiance.x * factor, radiance.y * factor, radiance.z * factor);
        return sh;
    }

    Volume uniformVolume(
        const vsg::vec3& origin, float spacing, std::array<std::uint32_t, 3> size, const vsg::vec3& radiance)
    {
        Volume v;
        v.origin = origin;
        v.spacing = spacing;
        v.size = size;
        v.probes.assign(size[0] * size[1] * size[2], uniformProbe(radiance));
        return v;
    }

    // ---------------------------------------------------------------------------------------------
    // Format: round trips
    // ---------------------------------------------------------------------------------------------

    TEST(RenderProbesTest, interior_sample_parses_to_the_expected_values)
    {
        const ProbeSet set = mustParse(interiorSample());
        EXPECT_TRUE(set.cell.interior);
        EXPECT_EQ(set.cell.name, "Example Hall");
        EXPECT_EQ(set.rays, 256u);
        EXPECT_EQ(set.bounces, 2u);
        EXPECT_EQ(set.seed, 1u);
        expectNear(vsg::vec3(0.2f, 0.2f, 0.2f), set.ambient, 1e-7f);
        ASSERT_EQ(set.volumes.size(), 1u);
        const Volume& v = set.volumes[0];
        EXPECT_FLOAT_EQ(v.spacing, 128.f);
        EXPECT_EQ(v.size, (std::array<std::uint32_t, 3>{ 2, 1, 1 }));
        ASSERT_EQ(v.probes.size(), 2u);
        // The first probe's numbers are r, g, b of coefficient 0, then of coefficient 1, and so on.
        const std::vector<std::string> tokens = probeTokens(0);
        EXPECT_FLOAT_EQ(v.probes[0].c[0].x, std::stof(tokens[0]));
        EXPECT_FLOAT_EQ(v.probes[0].c[0].y, std::stof(tokens[1]));
        EXPECT_FLOAT_EQ(v.probes[0].c[0].z, std::stof(tokens[2]));
        EXPECT_FLOAT_EQ(v.probes[0].c[1].x, std::stof(tokens[3]));
        EXPECT_FLOAT_EQ(v.probes[0].c[8].z, std::stof(tokens[26]));
    }

    TEST(RenderProbesTest, exterior_sample_parses_grid_coordinates)
    {
        const ProbeSet set = mustParse(exteriorSample());
        EXPECT_FALSE(set.cell.interior);
        EXPECT_EQ(set.cell.x, -3);
        EXPECT_EQ(set.cell.y, 7);
        ASSERT_EQ(set.volumes.size(), 1u);
        EXPECT_FLOAT_EQ(set.volumes[0].origin.x, -1024.f);
        EXPECT_FLOAT_EQ(set.volumes[0].origin.y, 2048.5f);
        EXPECT_EQ(set.volumes[0].size, (std::array<std::uint32_t, 3>{ 1, 2, 1 }));
    }

    TEST(RenderProbesTest, writing_a_parsed_canonical_file_gives_the_same_bytes)
    {
        for (const std::string& text : { interiorSample(), exteriorSample(), twoVolumeSample() })
        {
            const ProbeSet set = mustParse(text);
            EXPECT_EQ(validate(set), "");
            EXPECT_EQ(writeCanonical(set), text);
        }
    }

    TEST(RenderProbesTest, two_volume_sample_keeps_both_volumes_in_order)
    {
        const ProbeSet set = mustParse(twoVolumeSample());
        ASSERT_EQ(set.volumes.size(), 2u);
        EXPECT_FLOAT_EQ(set.volumes[0].spacing, 128.f);
        EXPECT_FLOAT_EQ(set.volumes[1].spacing, 64.f);
        EXPECT_FLOAT_EQ(set.volumes[1].origin.x, -128.5f);
        EXPECT_EQ(set.volumes[1].size, (std::array<std::uint32_t, 3>{ 1, 2, 1 }));
    }

    TEST(RenderProbesTest, strings_with_escapes_and_utf8_round_trip)
    {
        // Name: Say "Hi" \ <U+0001> and a UTF-8 e-acute (written as it is, not escaped).
        const std::string escaped = "Say \\\"Hi\\\" \\\\ \\u0001 \xC3\xA9";
        const std::string text = sampleText("\"interior\": \"" + escaped + "\"",
            { volumeBlock("[0, 0, 0]", "128", "[1, 1, 1]", { probeText(probeTokens(0)) }) });
        const ProbeSet set = mustParse(text);
        EXPECT_EQ(set.cell.name, std::string("Say \"Hi\" \\ \x01 \xC3\xA9"));
        EXPECT_EQ(writeCanonical(set), text);
    }

    TEST(RenderProbesTest, writing_then_parsing_keeps_floats_bit_equal)
    {
        const std::array<float, 9> awkward = { 0.1f, 1e-05f, -0.f, 16777216.f, 3.4028235e38f, 0.3f, -1.5e-30f,
            123456.79f, std::numeric_limits<float>::denorm_min() };

        ProbeSet set;
        set.cell.interior = false;
        set.cell.x = std::numeric_limits<std::int32_t>::min();
        set.cell.y = std::numeric_limits<std::int32_t>::max();
        set.rays = 65536;
        set.bounces = 8;
        set.seed = std::numeric_limits<std::uint32_t>::max();
        set.ambient = vsg::vec3(0.1f, -0.f, 1e-05f);
        for (int v = 0; v < 2; ++v)
        {
            Volume volume;
            volume.origin = vsg::vec3(awkward[static_cast<std::size_t>(v)], -0.f, 16777216.f);
            volume.spacing = (v == 0) ? 0.1f : 99999.5f;
            volume.size = { 2, 1, 1 };
            for (int p = 0; p < 2; ++p)
            {
                Sh9 sh;
                for (int k = 0; k < shCoefficients; ++k)
                    sh.c[k] = vsg::vec3(awkward[static_cast<std::size_t>((k + p) % 9)],
                        awkward[static_cast<std::size_t>((k + 2 * p + v + 1) % 9)],
                        awkward[static_cast<std::size_t>((k + 5 * v + 3) % 9)]);
                volume.probes.push_back(sh);
            }
            set.volumes.push_back(volume);
        }

        ASSERT_EQ(validate(set), "");
        const std::string text = writeCanonical(set);
        const ProbeSet back = mustParse(text);
        expectSameSet(set, back);
        // And once more: writing what we read back gives the very same text.
        EXPECT_EQ(writeCanonical(back), text);
    }

    TEST(RenderProbesTest, written_text_uses_shortest_float_text_and_the_canonical_layout)
    {
        ProbeSet set;
        set.cell = interiorCell("Example Hall");
        set.rays = 256;
        set.bounces = 2;
        set.seed = 1;
        set.ambient = vsg::vec3(0.2f, 0.2f, 0.2f);
        Volume volume;
        volume.origin = vsg::vec3(0.f, -0.f, 1e-05f);
        volume.spacing = 128.f;
        volume.size = { 1, 1, 1 };
        volume.probes.assign(1, Sh9{});
        set.volumes.push_back(volume);

        const std::string text = writeCanonical(set);
        EXPECT_EQ(text.back(), '\n');
        EXPECT_FALSE(contains(text, "\r"));
        EXPECT_TRUE(contains(text, "\n  \"format\": \"openeden.probes\",\n  \"version\": 1,\n  \"cell\": {\n"));
        EXPECT_TRUE(contains(text, "\"origin\": [0, -0, 1e-05],\n"));
        EXPECT_TRUE(contains(text, "\"spacing\": 128,\n"));
        EXPECT_TRUE(contains(text, "\"ambient\": [0.2, 0.2, 0.2]\n"));
    }

    TEST(RenderProbesTest, non_canonical_whitespace_still_parses)
    {
        std::string text = interiorSample();
        // Put the colons, commas and line breaks in unusual places.
        std::string messy;
        for (char c : text)
        {
            if (c == '\n')
                messy += "\r\n\t  \n";
            else if (c == ',')
                messy += " ,\t";
            else if (c == ':')
                messy += " :  ";
            else
                messy.push_back(c);
        }
        const ProbeSet set = mustParse(messy);
        EXPECT_EQ(writeCanonical(set), text);
    }

    TEST(RenderProbesTest, a_compact_one_line_file_parses)
    {
        std::string compact;
        for (char c : interiorSample())
            if (c != '\n' && c != ' ')
                compact.push_back(c);
        // The cell name contains a space, which we just removed; use a name without one.
        ProbeError error;
        EXPECT_TRUE(parseProbes(compact, "compact.json", error).has_value()) << error.describe();
    }

    // ---------------------------------------------------------------------------------------------
    // Format: located errors
    // ---------------------------------------------------------------------------------------------

    TEST(RenderProbesTest, bad_json_syntax_is_a_located_error)
    {
        const std::string text = interiorSample();
        mustFail(text.substr(0, text.size() - 3)); // cut off the closing braces
        mustFail("not json at all");
        mustFail("");
        const ProbeError error = mustFail(replaceOnce(text, "\"version\": 1,", "\"version\": 1 \"oops\","));
        EXPECT_EQ(error.line, 3);
    }

    TEST(RenderProbesTest, wrong_format_name_is_refused)
    {
        const ProbeError error = mustFail(replaceOnce(interiorSample(), "openeden.probes", "openeden.material"));
        EXPECT_EQ(error.line, 2);
        EXPECT_TRUE(contains(error.field, "format")) << error.field;
    }

    TEST(RenderProbesTest, version_other_than_1_is_refused)
    {
        const ProbeError two = mustFail(replaceOnce(interiorSample(), "\"version\": 1,", "\"version\": 2,"));
        EXPECT_EQ(two.line, 3);
        EXPECT_TRUE(contains(two.field, "version")) << two.field;

        const ProbeError fraction = mustFail(replaceOnce(interiorSample(), "\"version\": 1,", "\"version\": 1.0,"));
        EXPECT_EQ(fraction.line, 3);
        EXPECT_TRUE(contains(fraction.field, "version")) << fraction.field;
    }

    TEST(RenderProbesTest, a_missing_key_is_refused_and_named)
    {
        const ProbeError error = mustFail(replaceOnce(interiorSample(), "    \"seed\": 1,\n", ""));
        EXPECT_TRUE(contains(error.field + error.message, "seed")) << error.describe();
    }

    TEST(RenderProbesTest, an_unknown_key_is_refused_at_its_line)
    {
        const ProbeError error
            = mustFail(replaceOnce(interiorSample(), "  \"version\": 1,\n", "  \"version\": 1,\n  \"extra\": 1,\n"));
        EXPECT_EQ(error.line, 4);
        EXPECT_TRUE(contains(error.field + error.message, "extra")) << error.describe();
    }

    TEST(RenderProbesTest, bake_settings_are_range_checked)
    {
        const std::string text = interiorSample();

        const ProbeError raysZero = mustFail(replaceOnce(text, "\"rays\": 256", "\"rays\": 0"));
        EXPECT_EQ(raysZero.line, 8);
        EXPECT_TRUE(contains(raysZero.field, "rays")) << raysZero.field;
        mustFail(replaceOnce(text, "\"rays\": 256", "\"rays\": 65537"));
        EXPECT_EQ(mustParse(replaceOnce(text, "\"rays\": 256", "\"rays\": 65536")).rays, 65536u);

        const ProbeError bounces = mustFail(replaceOnce(text, "\"bounces\": 2", "\"bounces\": 9"));
        EXPECT_EQ(bounces.line, 9);
        EXPECT_TRUE(contains(bounces.field, "bounces")) << bounces.field;
        mustFail(replaceOnce(text, "\"bounces\": 2", "\"bounces\": -1"));
        EXPECT_EQ(mustParse(replaceOnce(text, "\"bounces\": 2", "\"bounces\": 0")).bounces, 0u);
        EXPECT_EQ(mustParse(replaceOnce(text, "\"bounces\": 2", "\"bounces\": 8")).bounces, 8u);

        EXPECT_EQ(mustParse(replaceOnce(text, "\"seed\": 1", "\"seed\": 4294967295")).seed, 4294967295u);
        mustFail(replaceOnce(text, "\"seed\": 1", "\"seed\": 4294967296"));
        mustFail(replaceOnce(text, "\"seed\": 1", "\"seed\": -1"));
    }

    TEST(RenderProbesTest, a_fraction_or_exponent_in_an_integer_field_is_refused)
    {
        const std::string text = interiorSample();
        mustFail(replaceOnce(text, "\"rays\": 256", "\"rays\": 256.5"));
        mustFail(replaceOnce(text, "\"rays\": 256", "\"rays\": 2.56e2"));
        mustFail(replaceOnce(text, "\"seed\": 1", "\"seed\": 1.0"));
        mustFail(replaceOnce(text, "\"size\": [2, 1, 1]", "\"size\": [2, 1.5, 1]"));
        mustFail(replaceOnce(exteriorSample(), "[-3, 7]", "[-3, 7.5]"));
    }

    TEST(RenderProbesTest, exterior_coordinates_must_fit_32_bit_signed)
    {
        const ProbeError error = mustFail(replaceOnce(exteriorSample(), "[-3, 7]", "[2147483648, 7]"));
        EXPECT_TRUE(contains(error.field, "cell") || contains(error.field, "exterior")) << error.field;
        mustFail(replaceOnce(exteriorSample(), "[-3, 7]", "[-3, -2147483649]"));
        const ProbeSet edge = mustParse(replaceOnce(exteriorSample(), "[-3, 7]", "[-2147483648, 2147483647]"));
        EXPECT_EQ(edge.cell.x, std::numeric_limits<std::int32_t>::min());
        EXPECT_EQ(edge.cell.y, std::numeric_limits<std::int32_t>::max());
    }

    TEST(RenderProbesTest, volume_size_is_range_checked)
    {
        const std::string text = interiorSample();

        const ProbeError zero = mustFail(replaceOnce(text, "\"size\": [2, 1, 1]", "\"size\": [2, 0, 1]"));
        EXPECT_TRUE(contains(zero.field, "size")) << zero.field;

        // 90 * 92 = 8280 probes, more than maxVolumeProbes (8192).
        const ProbeError tooMany = mustFail(replaceOnce(text, "\"size\": [2, 1, 1]", "\"size\": [90, 92, 1]"));
        EXPECT_TRUE(contains(tooMany.field, "size")) << tooMany.field;

        mustFail(replaceOnce(text, "\"size\": [2, 1, 1]", "\"size\": [2, 1]"));
        mustFail(replaceOnce(text, "\"size\": [2, 1, 1]", "\"size\": [2, 1, -1]"));
    }

    TEST(RenderProbesTest, the_probe_count_must_match_the_size)
    {
        // Size says 3 probes, the file has 2.
        const ProbeError error = mustFail(replaceOnce(interiorSample(), "\"size\": [2, 1, 1]", "\"size\": [3, 1, 1]"));
        EXPECT_TRUE(contains(error.field, "probes")) << error.field;
        // Size says 1 probe, the file has 2.
        mustFail(replaceOnce(interiorSample(), "\"size\": [2, 1, 1]", "\"size\": [1, 1, 1]"));
    }

    TEST(RenderProbesTest, a_probe_must_have_exactly_27_numbers)
    {
        const ProbeError few = mustFail(sampleWithProbe1(probeTokens(1, 26)));
        EXPECT_EQ(few.line, 20);
        EXPECT_TRUE(contains(few.field, "volumes[0].probes[1]")) << few.field;

        mustFail(sampleWithProbe1(probeTokens(1, 28)));
    }

    TEST(RenderProbesTest, a_bad_number_inside_a_probe_names_its_index)
    {
        std::vector<std::string> tokens = probeTokens(1);
        tokens[5] = "\"x\"";
        const ProbeError error = mustFail(sampleWithProbe1(tokens));
        EXPECT_EQ(error.line, 20);
        EXPECT_EQ(error.field, "volumes[0].probes[1][5]");
    }

    TEST(RenderProbesTest, a_number_that_overflows_a_float_is_refused)
    {
        std::vector<std::string> tokens = probeTokens(1);
        tokens[5] = "1e999";
        const ProbeError error = mustFail(sampleWithProbe1(tokens));
        EXPECT_EQ(error.line, 20);
        EXPECT_TRUE(contains(error.field, "volumes[0].probes[1]")) << error.field;

        mustFail(replaceOnce(interiorSample(), "[0.2, 0.2, 0.2]", "[0.2, 1e999, 0.2]"));
    }

    TEST(RenderProbesTest, spacing_must_be_positive_and_not_huge)
    {
        const std::string text = interiorSample();
        const ProbeError zero = mustFail(replaceOnce(text, "\"spacing\": 128", "\"spacing\": 0"));
        EXPECT_EQ(zero.line, 16);
        EXPECT_TRUE(contains(zero.field, "spacing")) << zero.field;
        mustFail(replaceOnce(text, "\"spacing\": 128", "\"spacing\": -128"));
        mustFail(replaceOnce(text, "\"spacing\": 128", "\"spacing\": 100001"));
        EXPECT_FLOAT_EQ(
            mustParse(replaceOnce(text, "\"spacing\": 128", "\"spacing\": 100000")).volumes[0].spacing, 100000.f);
    }

    TEST(RenderProbesTest, origin_and_ambient_need_three_numbers)
    {
        const std::string text = interiorSample();
        mustFail(replaceOnce(text, "\"origin\": [0, 0, 0]", "\"origin\": [0, 0]"));
        mustFail(replaceOnce(text, "\"origin\": [0, 0, 0]", "\"origin\": [0, 0, 0, 0]"));
        mustFail(replaceOnce(text, "[0.2, 0.2, 0.2]", "[0.2, 0.2]"));
        const ProbeError error = mustFail(replaceOnce(text, "[0.2, 0.2, 0.2]", "[0.2, \"a\", 0.2]"));
        EXPECT_EQ(error.line, 11);
    }

    TEST(RenderProbesTest, volume_count_is_limited_to_1_to_64)
    {
        mustFail(sampleText("\"interior\": \"Example Hall\"", {}));

        std::vector<std::string> blocks;
        for (int i = 0; i < 64; ++i)
            blocks.push_back(volumeBlock("[0, 0, 0]", "128", "[1, 1, 1]", { probeText(probeTokens(i)) }));
        EXPECT_EQ(mustParse(sampleText("\"interior\": \"Example Hall\"", blocks)).volumes.size(), 64u);

        blocks.push_back(blocks.front());
        mustFail(sampleText("\"interior\": \"Example Hall\"", blocks));
    }

    TEST(RenderProbesTest, the_cell_must_be_interior_or_exterior_not_both_or_neither)
    {
        const std::vector<std::string> volume
            = { volumeBlock("[0, 0, 0]", "128", "[1, 1, 1]", { probeText(probeTokens(0)) }) };
        mustFail(sampleText("\"interior\": \"A\",\n    \"exterior\": [1, 2]", volume));
        mustFail(sampleText("\"other\": \"A\"", volume));
    }

    // ---------------------------------------------------------------------------------------------
    // validate()
    // ---------------------------------------------------------------------------------------------

    TEST(RenderProbesTest, validate_accepts_a_parsed_file_and_names_the_first_problem_otherwise)
    {
        const ProbeSet good = mustParse(interiorSample());
        EXPECT_EQ(validate(good), "");

        ProbeSet badSpacing = good;
        badSpacing.volumes[0].spacing = 0.f;
        EXPECT_NE(validate(badSpacing), "");

        ProbeSet negativeSpacing = good;
        negativeSpacing.volumes[0].spacing = -1.f;
        EXPECT_NE(validate(negativeSpacing), "");

        ProbeSet badCount = good;
        badCount.volumes[0].probes.pop_back();
        EXPECT_NE(validate(badCount), "");

        ProbeSet badSize = good;
        badSize.volumes[0].size = { 0, 1, 1 };
        EXPECT_NE(validate(badSize), "");

        ProbeSet notFinite = good;
        notFinite.volumes[0].probes[1].c[4].y = std::numeric_limits<float>::quiet_NaN();
        EXPECT_NE(validate(notFinite), "");

        ProbeSet infiniteOrigin = good;
        infiniteOrigin.volumes[0].origin.z = std::numeric_limits<float>::infinity();
        EXPECT_NE(validate(infiniteOrigin), "");

        ProbeSet noVolumes = good;
        noVolumes.volumes.clear();
        EXPECT_NE(validate(noVolumes), "");
    }

    // ---------------------------------------------------------------------------------------------
    // Cell keys and file paths
    // ---------------------------------------------------------------------------------------------

    TEST(RenderProbesTest, fnv1a_reference_value_for_the_test_helper)
    {
        // Known FNV-1a 32-bit value: "a" gives 0xe40c292c; the empty string gives the offset basis.
        EXPECT_EQ(fnv1a32("a"), 0xe40c292cu);
        EXPECT_EQ(fnv1a32(""), 2166136261u);
    }

    TEST(RenderProbesTest, interior_key_for_a_plain_name)
    {
        EXPECT_EQ(cellKey(interiorCell("a")), "interior/a-e40c292c");
        EXPECT_EQ(cellKey(interiorCell("A")), "interior/a-e40c292c"); // hash is of the lower-cased name
    }

    TEST(RenderProbesTest, interior_key_example_from_the_spec)
    {
        const std::string key = cellKey(interiorCell("Caldera, Mages Guild"));
        EXPECT_EQ(key.rfind("interior/caldera_mages_guild-", 0), 0u) << key;
        EXPECT_EQ(key, expectedInteriorKey("Caldera, Mages Guild"));
        EXPECT_EQ(key.size(), std::string("interior/caldera_mages_guild-").size() + 8);
    }

    TEST(RenderProbesTest, interior_key_sanitising_rules)
    {
        for (const std::string name : { "Example Hall", "  Hall  ", "A--B", "__x__", "a  b   c",
                 "Tel Aruhn, Sathil Dres: Cellar", "UPPER lower 123", "dots.and/slashes\\too" })
            EXPECT_EQ(cellKey(interiorCell(name)), expectedInteriorKey(name)) << name;

        EXPECT_EQ(cellKey(interiorCell("  Hall  ")).rfind("interior/hall-", 0), 0u);
        EXPECT_EQ(cellKey(interiorCell("A--B")).rfind("interior/a_b-", 0), 0u);
        EXPECT_EQ(cellKey(interiorCell("__x__")).rfind("interior/x-", 0), 0u);
    }

    TEST(RenderProbesTest, interior_key_non_ascii_bytes_are_replaced_but_hashed_as_they_are)
    {
        const std::string name = "Caf\xC3\xA9 Bar";
        const std::string key = cellKey(interiorCell(name));
        EXPECT_EQ(key, expectedInteriorKey(name));
        EXPECT_EQ(key.rfind("interior/caf_bar-", 0), 0u) << key;
    }

    TEST(RenderProbesTest, interior_key_with_nothing_usable_becomes_cell)
    {
        for (const std::string name : { "", "!!!", " ", "___" })
        {
            const std::string key = cellKey(interiorCell(name));
            EXPECT_EQ(key, expectedInteriorKey(name)) << "'" << name << "'";
            EXPECT_EQ(key.rfind("interior/cell-", 0), 0u) << key;
        }
    }

    TEST(RenderProbesTest, interior_key_is_cut_to_48_bytes_before_the_hash)
    {
        std::string name;
        for (int i = 0; i < 6; ++i)
            name += "abcdefghij"; // 60 letters
        const std::string key = cellKey(interiorCell(name));
        EXPECT_EQ(key, expectedInteriorKey(name));
        EXPECT_EQ(key.rfind("interior/" + name.substr(0, 48) + "-", 0), 0u) << key;
        EXPECT_EQ(key.size(), std::string("interior/").size() + 48 + 1 + 8);
    }

    TEST(RenderProbesTest, names_that_sanitize_alike_get_different_keys)
    {
        EXPECT_NE(cellKey(interiorCell("Hall One")), cellKey(interiorCell("Hall_One")));
        EXPECT_NE(cellKey(interiorCell("Hall  One")), cellKey(interiorCell("Hall One")));
    }

    TEST(RenderProbesTest, exterior_key_uses_grid_coordinates)
    {
        CellId id;
        id.interior = false;
        id.x = -3;
        id.y = 7;
        EXPECT_EQ(cellKey(id), "exterior/-3_7");
        EXPECT_EQ(probeFilePath(id), "openeden/probes/exterior/-3_7.json");
    }

    TEST(RenderProbesTest, probe_file_path_is_the_key_in_the_probes_folder)
    {
        const CellId id = interiorCell("Example Hall");
        EXPECT_EQ(probeFilePath(id), "openeden/probes/" + cellKey(id) + ".json");
    }

    // ---------------------------------------------------------------------------------------------
    // Spherical harmonics
    // ---------------------------------------------------------------------------------------------

    TEST(RenderProbesMathTest, basis_constants_along_the_axes)
    {
        const float t = 1e-5f;

        const auto z = shBasis(vsg::vec3(0.f, 0.f, 1.f));
        const std::array<float, 9> expectedZ = { 0.282095f, 0.f, 0.488603f, 0.f, 0.f, 0.f, 0.630784f, 0.f, 0.f };
        for (int i = 0; i < 9; ++i)
            EXPECT_NEAR(z[static_cast<std::size_t>(i)], expectedZ[static_cast<std::size_t>(i)], t) << "z, term " << i;

        const auto x = shBasis(vsg::vec3(1.f, 0.f, 0.f));
        const std::array<float, 9> expectedX = { 0.282095f, 0.f, 0.f, 0.488603f, 0.f, 0.f, -0.315392f, 0.f, 0.546274f };
        for (int i = 0; i < 9; ++i)
            EXPECT_NEAR(x[static_cast<std::size_t>(i)], expectedX[static_cast<std::size_t>(i)], t) << "x, term " << i;

        const auto y = shBasis(vsg::vec3(0.f, 1.f, 0.f));
        const std::array<float, 9> expectedY
            = { 0.282095f, 0.488603f, 0.f, 0.f, 0.f, 0.f, -0.315392f, 0.f, -0.546274f };
        for (int i = 0; i < 9; ++i)
            EXPECT_NEAR(y[static_cast<std::size_t>(i)], expectedY[static_cast<std::size_t>(i)], t) << "y, term " << i;
    }

    TEST(RenderProbesMathTest, basis_cross_terms_for_a_diagonal_direction)
    {
        const float s = 1.f / std::sqrt(3.f);
        const auto b = shBasis(vsg::vec3(1.f, 1.f, 1.f)); // not unit length on purpose
        EXPECT_NEAR(b[1], 0.488603f * s, 1e-5f);
        EXPECT_NEAR(b[2], 0.488603f * s, 1e-5f);
        EXPECT_NEAR(b[3], 0.488603f * s, 1e-5f);
        EXPECT_NEAR(b[4], 1.092548f * s * s, 1e-5f);
        EXPECT_NEAR(b[5], 1.092548f * s * s, 1e-5f);
        EXPECT_NEAR(b[6], 0.315392f * (3.f * s * s - 1.f), 1e-5f);
        EXPECT_NEAR(b[7], 1.092548f * s * s, 1e-5f);
        EXPECT_NEAR(b[8], 0.f, 1e-5f);
    }

    TEST(RenderProbesMathTest, basis_normalises_its_direction)
    {
        const auto unit = shBasis(vsg::vec3(0.f, 0.6f, 0.8f));
        const auto scaled = shBasis(vsg::vec3(0.f, 6.f, 8.f));
        for (std::size_t i = 0; i < 9; ++i)
            EXPECT_NEAR(unit[i], scaled[i], 1e-5f) << i;
    }

    TEST(RenderProbesMathTest, basis_of_a_zero_vector_is_the_basis_of_plus_z)
    {
        const auto zero = shBasis(vsg::vec3(0.f, 0.f, 0.f));
        const auto up = shBasis(vsg::vec3(0.f, 0.f, 1.f));
        for (std::size_t i = 0; i < 9; ++i)
        {
            EXPECT_TRUE(std::isfinite(zero[i])) << i;
            EXPECT_NEAR(zero[i], up[i], 1e-6f) << i;
        }
    }

    TEST(RenderProbesMathTest, shAdd_accumulates_radiance_times_basis_times_weight)
    {
        Sh9 sh;
        const vsg::vec3 dir(1.f, 0.f, 0.f);
        shAdd(sh, dir, vsg::vec3(1.f, 2.f, 3.f), 0.5f);
        const auto basis = shBasis(dir);
        for (std::size_t i = 0; i < 9; ++i)
            expectNear(vsg::vec3(0.5f * basis[i], 1.f * basis[i], 1.5f * basis[i]), sh.c[i], 1e-6f);

        // A second call adds to the first.
        shAdd(sh, dir, vsg::vec3(1.f, 2.f, 3.f), 0.5f);
        for (std::size_t i = 0; i < 9; ++i)
            expectNear(vsg::vec3(1.f * basis[i], 2.f * basis[i], 3.f * basis[i]), sh.c[i], 1e-6f);
    }

    TEST(RenderProbesMathTest, uniform_radiance_gives_back_the_same_ambient_for_every_normal)
    {
        const vsg::vec3 radiance(0.2f, 0.5f, 0.9f);
        Sh9 sh;
        const std::vector<vsg::vec3> directions = fibonacciSphere(2000);
        const float weight = 4.f * pi / static_cast<float>(directions.size());
        for (const vsg::vec3& d : directions)
            shAdd(sh, d, radiance, weight);

        const std::vector<vsg::vec3> normals
            = { vsg::vec3(0.f, 0.f, 1.f), vsg::vec3(0.f, 0.f, -1.f), vsg::vec3(1.f, 0.f, 0.f),
                  vsg::vec3(0.f, -1.f, 0.f), vsg::vec3(1.f, 2.f, 3.f), vsg::vec3(-0.3f, 0.8f, -0.1f) };
        for (const vsg::vec3& n : normals)
            expectNear(radiance, shAmbient(sh, n), 2e-3f);
    }

    TEST(RenderProbesMathTest, a_single_direction_gives_the_hand_computed_ambient)
    {
        // Radiance 1 arriving from +Z only, weight 1: coefficients are the basis values at +Z.
        Sh9 sh;
        shAdd(sh, vsg::vec3(0.f, 0.f, 1.f), vsg::vec3(1.f, 1.f, 1.f), 1.f);

        // shAmbient = (band 0 * 1 + band 1 * 2/3 + band 2 * 1/4) with the basis of the normal.
        const float b0 = 0.282095f, b2 = 0.488603f, b6 = 0.630784f;
        const float up = b0 * b0 + (2.f / 3.f) * b2 * b2 + 0.25f * b6 * b6;
        const float down = b0 * b0 - (2.f / 3.f) * b2 * b2 + 0.25f * b6 * b6;
        EXPECT_NEAR(shAmbient(sh, vsg::vec3(0.f, 0.f, 1.f)).x, up, 1e-4f);
        EXPECT_NEAR(shAmbient(sh, vsg::vec3(0.f, 0.f, -1.f)).y, down, 1e-4f);
        EXPECT_GT(up, down);
    }

    TEST(RenderProbesMathTest, shAmbient_is_clamped_to_zero)
    {
        // Negative radiance in the green channel gives a negative irradiance, which must clamp to 0.
        Sh9 sh;
        shAdd(sh, vsg::vec3(0.f, 0.f, 1.f), vsg::vec3(1.f, -1.f, 0.f), 1.f);
        const vsg::vec3 result = shAmbient(sh, vsg::vec3(0.f, 0.f, 1.f));
        EXPECT_GT(result.x, 0.f);
        EXPECT_FLOAT_EQ(result.y, 0.f);
        EXPECT_FLOAT_EQ(result.z, 0.f);
    }

    TEST(RenderProbesMathTest, shLerp_endpoints_and_midpoint)
    {
        Sh9 a, b;
        for (int i = 0; i < shCoefficients; ++i)
        {
            a.c[i] = vsg::vec3(float(i), float(i) + 1.f, -float(i));
            b.c[i] = vsg::vec3(2.f * float(i) + 4.f, 0.f, float(i) * 0.5f);
        }
        const Sh9 atZero = shLerp(a, b, 0.f);
        const Sh9 atOne = shLerp(a, b, 1.f);
        const Sh9 middle = shLerp(a, b, 0.5f);
        const Sh9 quarter = shLerp(a, b, 0.25f);
        for (int i = 0; i < shCoefficients; ++i)
        {
            expectNear(a.c[i], atZero.c[i], 1e-6f);
            expectNear(b.c[i], atOne.c[i], 1e-6f);
            expectNear(vsg::vec3((a.c[i].x + b.c[i].x) / 2.f, (a.c[i].y + b.c[i].y) / 2.f, (a.c[i].z + b.c[i].z) / 2.f),
                middle.c[i], 1e-5f);
            expectNear(vsg::vec3(0.75f * a.c[i].x + 0.25f * b.c[i].x, 0.75f * a.c[i].y + 0.25f * b.c[i].y,
                           0.75f * a.c[i].z + 0.25f * b.c[i].z),
                quarter.c[i], 1e-5f);
        }
    }

    // ---------------------------------------------------------------------------------------------
    // sampleAmbient
    // ---------------------------------------------------------------------------------------------

    TEST(RenderProbesMathTest, a_single_probe_volume_returns_that_probes_ambient_anywhere)
    {
        Volume v;
        v.origin = vsg::vec3(10.f, 20.f, 30.f);
        v.spacing = 100.f;
        v.size = { 1, 1, 1 };
        Sh9 sh;
        shAdd(sh, vsg::vec3(0.f, 0.f, 1.f), vsg::vec3(1.f, 0.5f, 0.25f), 1.f);
        shAdd(sh, vsg::vec3(1.f, 0.f, 0.f), vsg::vec3(0.1f, 0.2f, 0.3f), 1.f);
        v.probes.push_back(sh);

        const std::vector<vsg::vec3> positions = { vsg::vec3(10.f, 20.f, 30.f), vsg::vec3(-5000.f, 3.f, 7.f),
            vsg::vec3(60.f, 70.f, 80.f), vsg::vec3(1e5f, 1e5f, -1e5f) };
        const std::vector<vsg::vec3> normals
            = { vsg::vec3(0.f, 0.f, 1.f), vsg::vec3(1.f, 0.f, 0.f), vsg::vec3(-1.f, -2.f, 0.5f) };
        for (const vsg::vec3& p : positions)
            for (const vsg::vec3& n : normals)
                expectNear(shAmbient(sh, n), sampleAmbient(v, p, n), 1e-5f);
    }

    TEST(RenderProbesMathTest, two_probes_interpolate_linearly_along_x)
    {
        Volume v;
        v.origin = vsg::vec3(0.f, 0.f, 0.f);
        v.spacing = 100.f;
        v.size = { 2, 1, 1 };
        v.probes = { uniformProbe(vsg::vec3(1.f, 0.f, 0.f)), uniformProbe(vsg::vec3(0.f, 0.f, 1.f)) };

        // The normal bias moves the sample along the normal; with +Z or +Y normals it does not change x.
        for (const vsg::vec3& normal :
            { vsg::vec3(0.f, 0.f, 1.f), vsg::vec3(0.f, 1.f, 0.f), vsg::vec3(0.f, 0.f, -1.f) })
        {
            expectNear(vsg::vec3(1.f, 0.f, 0.f), sampleAmbient(v, vsg::vec3(0.f, 0.f, 0.f), normal), 1e-4f);
            expectNear(vsg::vec3(0.75f, 0.f, 0.25f), sampleAmbient(v, vsg::vec3(25.f, 0.f, 0.f), normal), 1e-4f);
            expectNear(vsg::vec3(0.5f, 0.f, 0.5f), sampleAmbient(v, vsg::vec3(50.f, 0.f, 0.f), normal), 1e-4f);
            expectNear(vsg::vec3(0.f, 0.f, 1.f), sampleAmbient(v, vsg::vec3(100.f, 0.f, 0.f), normal), 1e-4f);
        }
    }

    TEST(RenderProbesMathTest, the_sample_position_is_moved_along_the_normal_by_the_bias)
    {
        Volume v;
        v.origin = vsg::vec3(0.f, 0.f, 0.f);
        v.spacing = 100.f;
        v.size = { 2, 1, 1 };
        v.probes = { uniformProbe(vsg::vec3(1.f, 0.f, 0.f)), uniformProbe(vsg::vec3(0.f, 0.f, 1.f)) };

        // Bias distance = normalBiasFraction * spacing = 30 units.
        EXPECT_FLOAT_EQ(normalBiasFraction, 0.3f);
        // A point at x = 20 facing +X is sampled at x = 50, the middle of the two probes.
        expectNear(
            vsg::vec3(0.5f, 0.f, 0.5f), sampleAmbient(v, vsg::vec3(20.f, 0.f, 0.f), vsg::vec3(2.f, 0.f, 0.f)), 1e-4f);
        // Facing -X it is sampled at x = -10, which clamps to the first probe.
        expectNear(
            vsg::vec3(1.f, 0.f, 0.f), sampleAmbient(v, vsg::vec3(20.f, 0.f, 0.f), vsg::vec3(-1.f, 0.f, 0.f)), 1e-4f);
    }

    TEST(RenderProbesMathTest, positions_outside_the_lattice_clamp_to_the_edge_probes)
    {
        Volume v;
        v.origin = vsg::vec3(0.f, 0.f, 0.f);
        v.spacing = 100.f;
        v.size = { 2, 1, 1 };
        v.probes = { uniformProbe(vsg::vec3(1.f, 0.f, 0.f)), uniformProbe(vsg::vec3(0.f, 0.f, 1.f)) };
        const vsg::vec3 up(0.f, 0.f, 1.f);

        expectNear(vsg::vec3(1.f, 0.f, 0.f), sampleAmbient(v, vsg::vec3(-500.f, 40.f, -9.f), up), 1e-4f);
        expectNear(vsg::vec3(0.f, 0.f, 1.f), sampleAmbient(v, vsg::vec3(900.f, -40.f, 50.f), up), 1e-4f);
    }

    TEST(RenderProbesMathTest, trilinear_interpolation_is_exact_for_a_linear_field_and_uses_the_x_fastest_layout)
    {
        Volume v;
        v.origin = vsg::vec3(10.f, 20.f, 30.f);
        v.spacing = 64.f;
        v.size = { 2, 2, 2 };
        v.probes.resize(8);
        for (std::uint32_t z = 0; z < 2; ++z)
            for (std::uint32_t y = 0; y < 2; ++y)
                for (std::uint32_t x = 0; x < 2; ++x)
                {
                    const float value = float(x + 2 * y + 4 * z) / 7.f;
                    v.probes[x + v.size[0] * (y + v.size[1] * z)] = uniformProbe(vsg::vec3(value, value, value));
                }

        // Normal +Z: the bias is 0.3 spacing along z, so the z lattice coordinate is 0.25 + 0.3 = 0.55.
        const vsg::vec3 position(10.f + 0.25f * 64.f, 20.f + 0.5f * 64.f, 30.f + 0.25f * 64.f);
        const float expected = (0.25f + 2.f * 0.5f + 4.f * 0.55f) / 7.f;
        expectNear(
            vsg::vec3(expected, expected, expected), sampleAmbient(v, position, vsg::vec3(0.f, 0.f, 1.f)), 1e-4f);

        // Normal -Z: z coordinate 0.25 - 0.3 clamps to 0.
        const float expectedDown = (0.25f + 2.f * 0.5f) / 7.f;
        expectNear(vsg::vec3(expectedDown, expectedDown, expectedDown),
            sampleAmbient(v, position, vsg::vec3(0.f, 0.f, -1.f)), 1e-4f);
    }

    // ---------------------------------------------------------------------------------------------
    // volumeWeight
    // ---------------------------------------------------------------------------------------------

    TEST(RenderProbesMathTest, volume_weight_is_one_inside_zero_one_spacing_outside_and_linear_between)
    {
        Volume v = uniformVolume(vsg::vec3(0.f, 0.f, 0.f), 100.f, { 3, 2, 1 }, vsg::vec3(1.f, 1.f, 1.f));
        // The lattice box is x 0..200, y 0..100, z 0..0.
        EXPECT_FLOAT_EQ(volumeWeight(v, vsg::vec3(100.f, 50.f, 0.f)), 1.f);
        EXPECT_FLOAT_EQ(volumeWeight(v, vsg::vec3(0.f, 0.f, 0.f)), 1.f);
        EXPECT_FLOAT_EQ(volumeWeight(v, vsg::vec3(200.f, 100.f, 0.f)), 1.f);

        EXPECT_NEAR(volumeWeight(v, vsg::vec3(250.f, 50.f, 0.f)), 0.5f, 1e-5f);
        EXPECT_NEAR(volumeWeight(v, vsg::vec3(-50.f, 50.f, 0.f)), 0.5f, 1e-5f);
        EXPECT_NEAR(volumeWeight(v, vsg::vec3(100.f, 150.f, 0.f)), 0.5f, 1e-5f);
        EXPECT_NEAR(volumeWeight(v, vsg::vec3(100.f, -75.f, 0.f)), 0.25f, 1e-5f);

        EXPECT_NEAR(volumeWeight(v, vsg::vec3(300.f, 50.f, 0.f)), 0.f, 1e-5f);
        EXPECT_NEAR(volumeWeight(v, vsg::vec3(5000.f, 50.f, 0.f)), 0.f, 1e-5f);
    }

    TEST(RenderProbesMathTest, volume_weight_never_increases_moving_away)
    {
        Volume v = uniformVolume(vsg::vec3(0.f, 0.f, 0.f), 100.f, { 3, 2, 1 }, vsg::vec3(1.f, 1.f, 1.f));
        float previous = volumeWeight(v, vsg::vec3(200.f, 50.f, 0.f));
        for (float x = 205.f; x <= 320.f; x += 5.f)
        {
            const float w = volumeWeight(v, vsg::vec3(x, 50.f, 0.f));
            EXPECT_LE(w, previous + 1e-6f) << "x = " << x;
            EXPECT_GE(w, 0.f);
            EXPECT_LE(w, 1.f);
            previous = w;
        }
    }

    // ---------------------------------------------------------------------------------------------
    // giAmbient
    // ---------------------------------------------------------------------------------------------

    // The spec's formula, written out: w = volumeWeight; gi = max(baked * strength + (flat - bakedAmbient), 0);
    // result = mix(flat, gi, w).
    vsg::vec3 expectedGi(
        const vsg::vec3& probe, const vsg::vec3& flat, const vsg::vec3& bakedAmbient, float strength, float w)
    {
        auto channel = [&](float p, float f, float b) {
            const float gi = std::max(p * strength + (f - b), 0.f);
            return f + (gi - f) * w;
        };
        return vsg::vec3(channel(probe.x, flat.x, bakedAmbient.x), channel(probe.y, flat.y, bakedAmbient.y),
            channel(probe.z, flat.z, bakedAmbient.z));
    }

    const std::vector<vsg::vec3> someNormals
        = { vsg::vec3(0.f, 0.f, 1.f), vsg::vec3(0.f, 0.f, -1.f), vsg::vec3(1.f, 0.f, 0.f), vsg::vec3(-1.f, 1.f, 0.5f) };

    TEST(RenderProbesMathTest, giAmbient_without_a_volume_is_the_flat_ambient)
    {
        const vsg::vec3 flat(0.3f, 0.2f, 0.1f);
        for (const vsg::vec3& n : someNormals)
            expectNear(flat, giAmbient(nullptr, vsg::vec3(12.f, 34.f, 56.f), n, flat, vsg::vec3(0.5f, 0.5f, 0.5f), 1.f),
                1e-6f);
    }

    TEST(RenderProbesMathTest, giAmbient_of_a_uniform_probe_equal_to_the_baked_ambient_reproduces_the_flat_ambient)
    {
        const vsg::vec3 ambient(0.2f, 0.3f, 0.4f);
        const Volume v = uniformVolume(vsg::vec3(0.f, 0.f, 0.f), 100.f, { 2, 2, 2 }, ambient);
        for (const vsg::vec3& n : someNormals)
            for (const vsg::vec3& p :
                { vsg::vec3(50.f, 50.f, 50.f), vsg::vec3(10.f, 90.f, 30.f), vsg::vec3(0.f, 0.f, 0.f) })
                expectNear(ambient, giAmbient(&v, p, n, ambient, ambient, 1.f), 1e-4f);
    }

    TEST(RenderProbesMathTest, giAmbient_carries_the_difference_between_flat_and_baked_ambient_as_an_offset)
    {
        const vsg::vec3 baked(0.2f, 0.3f, 0.4f);
        const vsg::vec3 flat(0.9f, 1.0f, 1.1f); // baked + 0.7, as night eye does
        const Volume v = uniformVolume(vsg::vec3(0.f, 0.f, 0.f), 100.f, { 2, 2, 2 }, baked);
        for (const vsg::vec3& n : someNormals)
            expectNear(flat, giAmbient(&v, vsg::vec3(50.f, 50.f, 50.f), n, flat, baked, 1.f), 1e-4f);

        // A brighter probe than the baked ambient shows up on top of the offset.
        const Volume brighter
            = uniformVolume(vsg::vec3(0.f, 0.f, 0.f), 100.f, { 2, 2, 2 }, vsg::vec3(0.5f, 0.5f, 0.5f));
        expectNear(vsg::vec3(1.2f, 1.2f, 1.2f),
            giAmbient(&brighter, vsg::vec3(50.f, 50.f, 50.f), vsg::vec3(0.f, 0.f, 1.f), flat, baked, 1.f), 1e-4f);
    }

    TEST(RenderProbesMathTest, giAmbient_strength_scales_the_baked_term_only)
    {
        const vsg::vec3 probe(0.2f, 0.3f, 0.4f);
        const vsg::vec3 flat(0.3f, 0.3f, 0.3f);
        const vsg::vec3 baked(0.2f, 0.2f, 0.2f);
        const Volume v = uniformVolume(vsg::vec3(0.f, 0.f, 0.f), 100.f, { 2, 2, 2 }, probe);
        const vsg::vec3 position(50.f, 50.f, 50.f);
        const vsg::vec3 normal(0.f, 0.f, 1.f);

        for (const float strength : { 0.f, 0.5f, 1.f, 2.f })
            expectNear(expectedGi(probe, flat, baked, strength, 1.f),
                giAmbient(&v, position, normal, flat, baked, strength), 1e-4f);

        // With the unchanged ambient (flat == baked) strength 2 doubles the probe and strength 0 leaves nothing.
        expectNear(vsg::vec3(0.4f, 0.6f, 0.8f), giAmbient(&v, position, normal, probe, probe, 2.f), 1e-4f);
        expectNear(vsg::vec3(0.f, 0.f, 0.f), giAmbient(&v, position, normal, probe, probe, 0.f), 1e-4f);
    }

    TEST(RenderProbesMathTest, giAmbient_never_goes_below_zero)
    {
        // Baked ambient was bright, the flat ambient is now much darker: the offset is -0.5.
        const vsg::vec3 probe(0.2f, 0.6f, 0.9f);
        const vsg::vec3 baked(0.8f, 0.8f, 0.8f);
        const vsg::vec3 flat(0.3f, 0.3f, 0.3f);
        const Volume v = uniformVolume(vsg::vec3(0.f, 0.f, 0.f), 100.f, { 2, 2, 2 }, probe);
        const vsg::vec3 result = giAmbient(&v, vsg::vec3(50.f, 50.f, 50.f), vsg::vec3(0.f, 0.f, 1.f), flat, baked, 1.f);
        expectNear(vsg::vec3(0.f, 0.1f, 0.4f), result, 1e-4f);
        EXPECT_GE(result.x, 0.f);
    }

    TEST(RenderProbesMathTest, giAmbient_fades_to_the_flat_ambient_outside_the_volume)
    {
        const vsg::vec3 probe(0.1f, 0.5f, 0.9f);
        const vsg::vec3 flat(0.3f, 0.3f, 0.3f);
        const vsg::vec3 baked(0.3f, 0.3f, 0.3f);
        const Volume v = uniformVolume(vsg::vec3(0.f, 0.f, 0.f), 100.f, { 2, 2, 2 }, probe);
        const vsg::vec3 normal(0.f, 0.f, 1.f);

        // Inside: the probe.
        expectNear(probe, giAmbient(&v, vsg::vec3(50.f, 50.f, 50.f), normal, flat, baked, 1.f), 1e-4f);

        // Half a spacing outside the box in x: half way between the probe and the flat ambient.
        const vsg::vec3 halfway(150.f, 50.f, 50.f);
        EXPECT_NEAR(volumeWeight(v, halfway), 0.5f, 1e-5f);
        expectNear(expectedGi(probe, flat, baked, 1.f, 0.5f), giAmbient(&v, halfway, normal, flat, baked, 1.f), 1e-4f);
        expectNear(vsg::vec3(0.2f, 0.4f, 0.6f), giAmbient(&v, halfway, normal, flat, baked, 1.f), 1e-4f);

        // One spacing or more outside: exactly the flat ambient.
        expectNear(flat, giAmbient(&v, vsg::vec3(200.f, 50.f, 50.f), normal, flat, baked, 1.f), 1e-5f);
        expectNear(flat, giAmbient(&v, vsg::vec3(-3000.f, 50.f, 50.f), normal, flat, baked, 1.f), 1e-5f);
    }
}

namespace Render::Gi
{
    // Added after the review: hostile or sloppy input must fail cleanly and cheaply.
    TEST(RenderProbesHostileInputTest, a_huge_array_is_refused_before_it_is_built)
    {
        std::string text
            = "{\"format\": \"openeden.probes\", \"version\": 1, \"cell\": {\"interior\": \"x\"}, "
              "\"bake\": {\"rays\": 1, \"bounces\": 0, \"seed\": 0, \"ambient\": [0, 0, 0]}, "
              "\"volumes\": [{\"origin\": [0, 0, 0], \"spacing\": 1, \"size\": [1, 1, 1], \"probes\": [";
        for (int i = 0; i < 20000; ++i)
            text += "0,";
        text += "0]}]}";
        ProbeError error;
        EXPECT_FALSE(parseProbes(text, "big.json", error));
        EXPECT_FALSE(error.message.empty());
    }

    TEST(RenderProbesHostileInputTest, a_leading_zero_is_not_json)
    {
        ProbeError error;
        EXPECT_FALSE(parseProbes("{\"format\": 007}", "z.json", error));
        EXPECT_FALSE(error.message.empty());
    }

    TEST(RenderProbesHostileInputTest, deep_nesting_fails_cleanly)
    {
        const std::string text(10000, '[');
        ProbeError error;
        EXPECT_FALSE(parseProbes(text, "deep.json", error));
        EXPECT_EQ(error.file, "deep.json");
    }
}
