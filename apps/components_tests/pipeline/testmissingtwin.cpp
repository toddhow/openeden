// Task 0363, stage g: there is no GLSL beside the Slang twins, so a pipeline whose define set has no twin (not
// prebuilt, and the run-time compile failed, was refused or ran out of time) cannot be made. What draws instead:
// nothing of that model. Pipeline::Graphics::create throws a std::runtime_error that names the stage and its defines;
// every builder that makes a model catches it, logs it (located by the model's file) and leaves the model out, as it
// does for a model whose file cannot be read. These tests pin the throw and that it leaves the graphics usable.
#include <gtest/gtest.h>

#include <components/pipeline/graphics.hpp>
#include <components/pipeline/mode.hpp>
#include <components/pipeline/options.hpp>

#include <vsg/io/Options.h>
#include <vsg/io/spirv.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace
{
    // An empty shader folder: no .slang-stage file for anything, as in a build that had no slangc.
    class EmptyShaderFolder
    {
    public:
        EmptyShaderFolder()
        {
            static std::atomic<unsigned> counter{ 0 };
            mDir = std::filesystem::temp_directory_path()
                / ("openeden_missingtwin_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                    + "_" + std::to_string(counter++));
            std::filesystem::create_directories(mDir / "default");
            mOptions = vsg::Options::create();
            mOptions->paths = { vsg::Path(mDir.generic_string()) };
            mOptions->readerWriters = { vsg::spirv::create() };
        }
        ~EmptyShaderFolder()
        {
            std::error_code ec;
            std::filesystem::remove_all(mDir, ec);
        }
        EmptyShaderFolder(const EmptyShaderFolder&) = delete;
        EmptyShaderFolder& operator=(const EmptyShaderFolder&) = delete;
        vsg::ref_ptr<const vsg::Options> options() const { return mOptions; }

    private:
        std::filesystem::path mDir;
        vsg::ref_ptr<vsg::Options> mOptions;
    };

    Pipeline::Options litDraw()
    {
        Pipeline::Options key;
        key.shader.addMode(Pipeline::Mode::VERTEX);
        key.shader.addMode(Pipeline::Mode::NORMAL);
        return key;
    }

    TEST(MissingTwin, a_draw_with_no_twin_throws_naming_the_stage_and_makes_no_pipeline)
    {
        EmptyShaderFolder folder;
        Pipeline::Graphics graphics(folder.options());
        try
        {
            graphics.create(litDraw());
            FAIL() << "a pipeline with no shader was made";
        }
        catch (const std::runtime_error& e)
        {
            const std::string text = e.what();
            EXPECT_NE(text.find("default/main.vert"), std::string::npos) << text;
        }
    }

    TEST(MissingTwin, the_graphics_stay_usable_after_a_refused_draw)
    {
        EmptyShaderFolder folder;
        Pipeline::Graphics graphics(folder.options());
        EXPECT_THROW(graphics.create(litDraw()), std::runtime_error);
        // A second ask answers the same way (nothing half-made is cached) and does not crash.
        EXPECT_THROW(graphics.create(litDraw()), std::runtime_error);
    }
}
