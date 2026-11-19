// Task 0355 follow-up (toddhow, 2026-11-19): per-pixel lighting for Skyrim (neutral) models, so a local light makes a
// round pool instead of a per-vertex smear. A new fragment define FORCE_PER_PIXEL_LIGHTING makes the object shader sum
// the point lights per pixel (the path normal and specular maps already take). Written before the implementation.
// The loader of a neutral model sets Pipeline::Mode::FORCE_PER_PIXEL_LIGHTING; Pipeline::Graphics also sets it for
// every lit draw with `forcePerPixelLighting` (the wired `[Shaders] force per pixel lighting`, off by default), and
// leaves it out where the material model (PBR) already lights per pixel, so no set exists twice.
#include <gtest/gtest.h>

#include <components/pipeline/graphics.hpp>
#include <components/pipeline/mode.hpp>
#include <components/pipeline/options.hpp>

#include <vsg/io/Options.h>
#include <vsg/io/glsl.h>
#include <vsg/io/spirv.h>
#include <vsg/state/GraphicsPipeline.h>
#include <vsg/state/ShaderModule.h>
#include <vsg/state/ShaderStage.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

namespace
{
    std::string readSource(const std::string& relative)
    {
        const std::filesystem::path source
            = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".." / std::filesystem::path(relative);
        std::ifstream in(source);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    // The object shader's two stages, trivial but importing the names the real fragment shader imports.
    class ForceFolder
    {
    public:
        ForceFolder()
        {
            static std::atomic<unsigned> counter{ 0 };
            mDir = std::filesystem::temp_directory_path()
                / ("openeden_forcepp_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                    + "_" + std::to_string(counter++));
            std::filesystem::create_directories(mDir / "default");
            write("default/main.vert", "#version 450\n#pragma import_defines(VERTEX, NORMAL)\nvoid main() {}\n");
            write("default/main.frag",
                "#version 450\n#pragma import_defines(NORMAL, PBR, FORCE_PER_PIXEL_LIGHTING, PARTICLE)\nvoid main() "
                "{}\n");
            mOptions = vsg::Options::create();
            mOptions->paths = { vsg::Path(mDir.generic_string()) };
            mOptions->readerWriters = { vsg::glsl::create(), vsg::spirv::create() };
        }
        ~ForceFolder()
        {
            std::error_code ec;
            std::filesystem::remove_all(mDir, ec);
        }
        ForceFolder(const ForceFolder&) = delete;
        ForceFolder& operator=(const ForceFolder&) = delete;
        vsg::ref_ptr<const vsg::Options> options() const { return mOptions; }

    private:
        void write(const std::string& file, const std::string& text)
        {
            std::ofstream out(mDir / file, std::ios::binary);
            out << text;
        }
        std::filesystem::path mDir;
        vsg::ref_ptr<vsg::Options> mOptions;
    };

    std::set<std::string> fragmentDefines(const vsg::ref_ptr<vsg::BindGraphicsPipeline>& bind)
    {
        std::set<std::string> result;
        for (const auto& stage : bind->pipeline->stages)
            if (stage->stage == VK_SHADER_STAGE_FRAGMENT_BIT && stage->module && stage->module->hints)
                result.insert(stage->module->hints->defines.begin(), stage->module->hints->defines.end());
        return result;
    }

    Pipeline::Options litDraw(bool force)
    {
        Pipeline::Options key;
        key.shader.addMode(Pipeline::Mode::VERTEX);
        key.shader.addMode(Pipeline::Mode::NORMAL);
        if (force)
            key.shader.addMode(Pipeline::Mode::FORCE_PER_PIXEL_LIGHTING);
        return key;
    }

    TEST(ForcePerPixel, a_loader_that_asks_for_it_gets_the_define_in_the_fragment_stage)
    {
        ForceFolder folder;
        Pipeline::Graphics graphics(folder.options());
        EXPECT_TRUE(fragmentDefines(graphics.create(litDraw(true))).count("FORCE_PER_PIXEL_LIGHTING"));
    }

    TEST(ForcePerPixel, a_draw_that_does_not_ask_keeps_the_old_define_set)
    {
        ForceFolder folder;
        Pipeline::Graphics graphics(folder.options());
        EXPECT_FALSE(fragmentDefines(graphics.create(litDraw(false))).count("FORCE_PER_PIXEL_LIGHTING"));
    }

    TEST(ForcePerPixel, the_material_model_already_lights_per_pixel_so_the_define_is_left_out)
    {
        ForceFolder folder;
        Pipeline::Graphics graphics(folder.options());
        graphics.pbrLook = 1;
        const auto defines = fragmentDefines(graphics.create(litDraw(true)));
        EXPECT_TRUE(defines.count("PBR"));
        EXPECT_FALSE(defines.count("FORCE_PER_PIXEL_LIGHTING"));
    }

    TEST(ForcePerPixel, the_wired_setting_forces_every_lit_draw_and_is_off_by_default)
    {
        ForceFolder folder;
        {
            Pipeline::Graphics graphics(folder.options());
            EXPECT_FALSE(graphics.forcePerPixelLighting);
        }
        Pipeline::Graphics graphics(folder.options());
        graphics.forcePerPixelLighting = true;
        EXPECT_TRUE(fragmentDefines(graphics.create(litDraw(false))).count("FORCE_PER_PIXEL_LIGHTING"));
    }

    TEST(ForcePerPixel, the_setting_does_not_touch_a_particle_draw)
    {
        ForceFolder folder;
        Pipeline::Graphics graphics(folder.options());
        graphics.forcePerPixelLighting = true;
        Pipeline::Options key = litDraw(false);
        key.shader.addMode(Pipeline::Mode::PARTICLE);
        EXPECT_FALSE(fragmentDefines(graphics.create(key)).count("FORCE_PER_PIXEL_LIGHTING"));
    }

    // The shader text: both languages take the define as one more reason to light per pixel, and the GLSL imports it.
    TEST(ForcePerPixel, the_glsl_object_shader_imports_the_define_and_lights_per_pixel_with_it)
    {
        const std::string text = readSource("files/shaders/default/main.frag");
        ASSERT_FALSE(text.empty());
        const auto pragma = text.substr(0, text.find('\n'));
        EXPECT_NE(pragma.find("FORCE_PER_PIXEL_LIGHTING"), std::string::npos);
        EXPECT_NE(text.find("defined(FORCE_PER_PIXEL_LIGHTING)"), std::string::npos);
    }

    TEST(ForcePerPixel, the_slang_twin_lights_per_pixel_with_the_define_too)
    {
        const std::string text = readSource("files/shaders/default/default.frag.slang");
        ASSERT_FALSE(text.empty());
        EXPECT_NE(text.find("defined(FORCE_PER_PIXEL_LIGHTING)"), std::string::npos);
    }

    // The neutral loader asks for it on both of its draw paths (static and skinned shapes).
    TEST(ForcePerPixel, the_skyrim_model_loader_asks_for_it_for_static_and_skinned_shapes)
    {
        const std::string text = readSource("apps/openmw/mwrender/skyrimmodel.cpp");
        ASSERT_FALSE(text.empty());
        std::size_t count = 0;
        for (auto at = text.find("Mode::FORCE_PER_PIXEL_LIGHTING"); at != std::string::npos;
             at = text.find("Mode::FORCE_PER_PIXEL_LIGHTING", at + 1))
            ++count;
        EXPECT_EQ(count, 2u);
    }
}
