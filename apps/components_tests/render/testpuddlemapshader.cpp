// OpenEden (task 0095, stage W-d): the text parity of the puddle map's shader twins with their C++ twin
// (components/render/puddlemapgpu.hpp). files/shaders/lib/material/puddlemap.glsl and puddlemap.slang repeat the
// buffer's word indices and the visual constants; the bindings files give the binding number; the terrain stages
// include the map only under WETNESS_PUDDLES. This reads the shader files from the source tree (a missing file fails
// the test). It is a check of the text only: that the shaders compute what the C++ twin does is the frame check and the
// debug views (the stage log), there is no GPU read-back test for this file yet.

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>

#include <components/render/puddlemapgpu.hpp>

namespace
{
    namespace PM = Render::PuddleMap;

    std::string readShader(const std::string& relative)
    {
        const std::filesystem::path source = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".."
            / "files" / "shaders" / std::filesystem::path(relative);
        std::ifstream in(source);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    // The value of `<prefix> name = value;` (const float / static const uint / ...), or nothing.
    std::optional<double> constantOf(const std::string& text, const std::string& name)
    {
        const std::regex re("\\b" + name + "\\s*=\\s*(-?[0-9]*\\.?[0-9]+)u?\\s*;");
        std::smatch m;
        if (!std::regex_search(text, m, re))
            return std::nullopt;
        return std::stod(m[1].str());
    }

    class RenderPuddleMapShader : public testing::TestWithParam<const char*>
    {
    };

    TEST_P(RenderPuddleMapShader, the_visual_constants_equal_the_cpp_twins)
    {
        const std::string text = readShader(GetParam());
        ASSERT_FALSE(text.empty()) << GetParam() << " is missing";
        struct Item
        {
            const char* name;
            double value;
        };
        const Item items[] = {
            { "puddleEdgeDepth", PM::edgeDepth },
            { "puddleRippleFrom", PM::rippleDepthFrom },
            { "puddleRippleTo", PM::rippleDepthTo },
            { "puddleDeepDarkening", PM::deepDarkening },
            { "puddleDeepDarkeningDepth", PM::deepDarkeningDepth },
            { "puddleMaxCapacity", Render::Puddle::maxQuantisedCapacity },
            { "puddleDebugDepthScale", PM::debugDepthScale },
            { "puddleDebugLevelScale", PM::debugLevelScale },
        };
        for (const Item& item : items)
        {
            const std::optional<double> v = constantOf(text, item.name);
            ASSERT_TRUE(v.has_value()) << item.name << " is not declared in " << GetParam();
            EXPECT_NEAR(*v, item.value, 1e-6) << item.name << " in " << GetParam();
        }
    }

    TEST_P(RenderPuddleMapShader, the_word_indices_equal_the_cpp_layout)
    {
        const std::string text = readShader(GetParam());
        ASSERT_FALSE(text.empty());
        struct Item
        {
            const char* name;
            std::uint32_t value;
        };
        const Item items[] = {
            { "PUDDLE_ENABLED", PM::wordEnabled },
            { "PUDDLE_DEBUG", PM::wordDebug },
            { "PUDDLE_ORIGIN_X", PM::wordOriginX },
            { "PUDDLE_ORIGIN_Y", PM::wordOriginY },
            { "PUDDLE_WINDOW_SIDE", PM::wordWindowSide },
            { "PUDDLE_TEXELS", PM::wordTexelsPerCell },
            { "PUDDLE_SPACING", PM::wordSpacing },
            { "PUDDLE_CELL_SIZE", PM::wordCellSize },
            { "PUDDLE_SUPPLY", PM::wordSupply },
            { "PUDDLE_ABSORPTION", PM::wordAbsorption },
            { "PUDDLE_TABLE_BASE", PM::tableBase },
        };
        for (const Item& item : items)
        {
            const std::optional<double> v = constantOf(text, item.name);
            ASSERT_TRUE(v.has_value()) << item.name << " is not declared in " << GetParam();
            EXPECT_EQ(static_cast<std::uint32_t>(*v), item.value) << item.name << " in " << GetParam();
        }
    }

    TEST_P(RenderPuddleMapShader, the_shader_decodes_the_texel_the_way_the_cpp_twin_does)
    {
        const std::string text = readShader(GetParam());
        ASSERT_FALSE(text.empty());
        // Capacity and pit maximum in 1/16 units, kappa as 2^(code / 15), bytes 0, 1 and 2 of the word.
        EXPECT_NE(text.find("& 0xFFu) * (1.0 / 16.0)"), std::string::npos);
        EXPECT_NE(text.find("(pit >> 8) & 0xFFu"), std::string::npos);
        EXPECT_NE(text.find("(pit >> 16) & 0xFFu) * (1.0 / 15.0)"), std::string::npos);
        EXPECT_NE(text.find("exp2("), std::string::npos);
        EXPECT_FLOAT_EQ(Render::Puddle::capacityStepsPerUnit, 16.f);
        EXPECT_FLOAT_EQ(Render::Puddle::logKappaStepsPerOctave, 15.f);
        EXPECT_FLOAT_EQ(Render::Puddle::minLogKappa, 0.f);
        // The window is read from the header, not assumed.
        EXPECT_NE(text.find("PUDDLE_WINDOW_SIDE"), std::string::npos);
    }

    INSTANTIATE_TEST_SUITE_P(
        Twins, RenderPuddleMapShader, testing::Values("lib/material/puddlemap.glsl", "lib/material/puddlemap.slang"));

    TEST(RenderPuddleMapShaderFiles, the_binding_is_20_in_both_binding_files_and_the_layout)
    {
        const std::string glsl = readShader("lib/view/bindings.glsl");
        const std::string slang = readShader("lib/view/bindings.slang");
        ASSERT_FALSE(glsl.empty());
        ASSERT_FALSE(slang.empty());
        EXPECT_NE(glsl.find("VIEW_PUDDLE_BINDING=20;"), std::string::npos);
        EXPECT_NE(slang.find("#define VIEW_PUDDLE_BINDING 20"), std::string::npos);
        // The map's buffer uses it in both twins.
        EXPECT_NE(readShader("lib/material/puddlemap.glsl").find("binding=VIEW_PUDDLE_BINDING"), std::string::npos);
        EXPECT_NE(readShader("lib/material/puddlemap.slang").find("vk::binding(VIEW_PUDDLE_BINDING, VIEW_SET)"),
            std::string::npos);
    }

    TEST(
        RenderPuddleMapShaderFiles, only_the_terrain_and_object_stages_include_the_map_and_only_under_the_puddle_define)
    {
        // Stage W-c: the object shader reads the map too (under the height test), so a road takes the baked puddles.
        for (const char* file :
            { "terrain/main.frag", "terrain/terrain.frag.slang", "default/main.frag", "default/default.frag.slang" })
        {
            const std::string text = readShader(file);
            ASSERT_FALSE(text.empty()) << file;
            const std::size_t include = text.find("lib/material/puddlemap.");
            ASSERT_NE(include, std::string::npos) << file;
            // The nearest #ifdef before the include is WETNESS_PUDDLES.
            const std::size_t guard = text.rfind("#ifdef WETNESS_PUDDLES", include);
            ASSERT_NE(guard, std::string::npos) << file;
            EXPECT_LT(include - guard, 200u) << file;
        }
        EXPECT_EQ(readShader("lib/material/wetness.glsl").find("puddlemap"), std::string::npos);
    }

    TEST(RenderPuddleMapShaderFiles, the_stand_in_buffer_is_big_enough_for_the_header_and_the_table)
    {
        // View::dummyPuddleMap holds PM::dataBase words; the shader reads the table before it looks at any texel.
        EXPECT_GE(PM::dataBase, PM::tableBase + PM::windowSide * PM::windowSide);
    }
}
