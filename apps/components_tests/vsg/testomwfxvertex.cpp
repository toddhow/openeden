// Task 0072, stage h: the vertex stage of the .omwfx post-processing passes (vsgUtil::readOmwfxVertexStage,
// components/vsgutil/passstages.hpp), the one stage allowed to be a Slang twin beside a GLSL fragment stage. Written
// from the spec, before the implementation.
#include <gtest/gtest.h>

#include <components/vsgutil/passstages.hpp>
#include <components/vsgutil/shaderpermutation.hpp>

#include <vsg/core/Value.h>
#include <vsg/io/Options.h>
#include <vsg/io/glsl.h>
#include <vsg/io/spirv.h>
#include <vsg/state/ShaderModule.h>
#include <vsg/state/ShaderStage.h>
#include <vsg/utils/ShaderCompiler.h>
#include <vsg/utils/SharedObjects.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    const std::string sVertex
        = "#version 450\nout gl_PerVertex { vec4 gl_Position; };\n"
          "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    const std::string sFragment
        = "#version 450\nlayout(location=0) out vec4 color;\nvoid main() { color = vec4(0.0); }\n";
    const std::string sBroken = "#version 450\nvoid main() { undeclared = 1; }\n";

    // A SPIR-V module that declares one float specialization constant with the given id, as
    // vsgUtil::addSettingsConstants reads it; the bound (word 3) tells two such modules apart.
    std::vector<uint32_t> twinWords(uint32_t bound, uint32_t constantId)
    {
        const auto op = [](uint32_t opcode, uint32_t count) { return (count << 16) | opcode; };
        std::vector<uint32_t> words = { 0x07230203u, 0x00010000u, 0u, bound, 0u };
        words.insert(words.end(), { op(17, 2), 1u }); // OpCapability Shader
        words.insert(words.end(), { op(22, 3), 2u, 32u }); // %2 = OpTypeFloat 32
        words.insert(words.end(), { op(71, 4), 10u, 1u, constantId }); // OpDecorate %10 SpecId constantId
        words.insert(words.end(), { op(50, 4), 2u, 10u, 0x3f000000u }); // %10 = OpSpecConstant 0.5
        return words;
    }
    const std::vector<uint32_t> sVertexTwin = twinWords(100, 1004);
    const std::vector<uint32_t> sOtherTwin = twinWords(200, 1004);

    std::vector<uint32_t> compiledAlone(VkShaderStageFlagBits stage, const std::string& source)
    {
        auto shaderStage = vsg::ShaderStage::create(stage, "main", source);
        if (!vsg::ShaderCompiler::create()->compile(shaderStage))
        {
            return {};
        }
        return shaderStage->module->code;
    }

    // vsg::GraphicsPipeline::compile links the GLSL source of its stages when one has source and no code.
    bool needsGlslang(const vsg::ShaderStage& stage)
    {
        return stage.module->code.empty() && !stage.module->source.empty();
    }

    // A folder holding post/omwfx.vert (and post/other.frag), read as the game reads the shaders.
    class OmwfxFolder
    {
    public:
        explicit OmwfxFolder(bool withVertex = true)
            : mDir(std::filesystem::temp_directory_path() / uniqueName())
        {
            std::filesystem::create_directories(mDir / "post");
            if (withVertex)
            {
                writeText("post/omwfx.vert", sVertex);
            }
            writeText("post/other.frag", sFragment);
            mOptions = vsg::Options::create();
            mOptions->paths = { vsg::Path(mDir.generic_string()) };
            mOptions->readerWriters = { vsg::glsl::create(), vsg::spirv::create() };
            mOptions->sharedObjects = vsg::SharedObjects::create();
        }
        ~OmwfxFolder()
        {
            std::error_code ec;
            std::filesystem::remove_all(mDir, ec);
        }
        void writeText(const std::string& file, const std::string& text)
        {
            std::ofstream out(mDir / file, std::ios::binary);
            out << text;
        }
        void write(const std::string& file, const std::vector<uint32_t>& words)
        {
            std::ofstream out(mDir / file, std::ios::binary);
            out.write(reinterpret_cast<const char*>(words.data()),
                static_cast<std::streamsize>(words.size() * sizeof(uint32_t)));
        }
        void writeVertexTwin()
        {
            write("post/omwfx.vert.p00000001.spv", sVertexTwin);
            writeText("post/omwfx.vert.permutations", "omwfx.vert.p00000001.spv\t\n");
        }
        void writeOtherTwin()
        {
            write("post/other.frag.p00000002.spv", sOtherTwin);
            writeText("post/other.frag.permutations", "other.frag.p00000002.spv\t\n");
        }
        vsg::ref_ptr<const vsg::Options> options() const { return mOptions; }
        // As the game makes them with `[Rendering] slang shaders` on, with one settings constant.
        vsg::ref_ptr<const vsg::Options> slangOptions() const
        {
            auto options = vsg::Options::create(*mOptions);
            options->setValue(vsgUtil::preferSlangKey, true);
            auto constants = vsgUtil::SettingsConstants::create();
            constants->constants[1004] = vsg::floatValue::create(0.25f);
            options->setObject(vsgUtil::settingsConstantsKey, constants);
            return options;
        }

    private:
        // Manifests are cached per process by folder, so no two fixtures share one.
        static std::string uniqueName()
        {
            static std::atomic<unsigned> counter{ 0 };
            return "openeden_omwfxvertex_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                + "_" + std::to_string(counter++);
        }

        std::filesystem::path mDir;
        vsg::ref_ptr<vsg::Options> mOptions;
    };

    TEST(OmwfxVertexStage, with_the_switch_off_it_is_the_glsl_compiled_alone)
    {
        OmwfxFolder folder;
        std::string error = "an earlier error";
        const auto stage = vsgUtil::readOmwfxVertexStage(folder.options(), &error);
        ASSERT_TRUE(stage);
        EXPECT_TRUE(error.empty()) << error;
        EXPECT_EQ(stage->stage, VK_SHADER_STAGE_VERTEX_BIT);
        EXPECT_EQ(stage->entryPointName, "main");
        ASSERT_TRUE(stage->module);
        EXPECT_FALSE(stage->module->code.empty());
        EXPECT_FALSE(stage->module->source.empty());
        EXPECT_EQ(stage->module->code, compiledAlone(VK_SHADER_STAGE_VERTEX_BIT, sVertex));
    }

    TEST(OmwfxVertexStage, with_the_switch_on_and_a_twin_it_is_the_twin_with_its_constants)
    {
        OmwfxFolder folder;
        folder.writeVertexTwin();
        std::string error;
        const auto stage = vsgUtil::readOmwfxVertexStage(folder.slangOptions(), &error);
        ASSERT_TRUE(stage);
        EXPECT_TRUE(error.empty()) << error;
        ASSERT_TRUE(stage->module);
        EXPECT_EQ(stage->module->code, sVertexTwin);
        EXPECT_EQ(stage->stage, VK_SHADER_STAGE_VERTEX_BIT);
        EXPECT_EQ(stage->specializationConstants.count(1004), 1u);
        // The pipeline must not find a stage that needs glslang, or it would link the twin with the GLSL fragment.
        EXPECT_FALSE(needsGlslang(*stage));
    }

    TEST(OmwfxVertexStage, with_the_switch_on_and_no_twin_it_is_the_glsl_compiled_alone)
    {
        const auto expected = compiledAlone(VK_SHADER_STAGE_VERTEX_BIT, sVertex);
        {
            SCOPED_TRACE("no manifest");
            OmwfxFolder folder;
            const auto stage = vsgUtil::readOmwfxVertexStage(folder.slangOptions());
            ASSERT_TRUE(stage);
            EXPECT_EQ(stage->module->code, expected);
            EXPECT_FALSE(stage->module->code.empty());
            EXPECT_TRUE(stage->specializationConstants.empty());
        }
        {
            SCOPED_TRACE("the set is not listed");
            OmwfxFolder folder;
            folder.write("post/omwfx.vert.p00000003.spv", sVertexTwin);
            folder.writeText("post/omwfx.vert.permutations", "omwfx.vert.p00000003.spv\tSOMETHING\n");
            const auto stage = vsgUtil::readOmwfxVertexStage(folder.slangOptions());
            ASSERT_TRUE(stage);
            EXPECT_EQ(stage->module->code, expected);
            EXPECT_TRUE(stage->specializationConstants.empty());
        }
    }

    TEST(OmwfxVertexStage, every_call_returns_a_stage_and_module_of_its_own)
    {
        for (const bool slang : { false, true })
        {
            SCOPED_TRACE(slang ? "twin" : "glsl");
            OmwfxFolder folder;
            folder.writeVertexTwin();
            const auto options = slang ? folder.slangOptions() : folder.options();
            const auto first = vsgUtil::readOmwfxVertexStage(options);
            const auto second = vsgUtil::readOmwfxVertexStage(options);
            ASSERT_TRUE(first);
            ASSERT_TRUE(second);
            EXPECT_NE(first, second);
            EXPECT_NE(first->module, second->module);
            const auto before = second->module->code;
            first->module->code.clear();
            first->module->code.push_back(0xdeadbeefu);
            EXPECT_EQ(second->module->code, before);
            EXPECT_NE(first->module->code, second->module->code);
        }
    }

    TEST(OmwfxVertexStage, a_missing_file_gives_null_and_names_the_file)
    {
        OmwfxFolder folder(false);
        std::string error = "an earlier error";
        EXPECT_FALSE(vsgUtil::readOmwfxVertexStage(folder.options(), &error));
        EXPECT_FALSE(error.empty());
        EXPECT_NE(error.find("post/omwfx.vert"), std::string::npos) << error;
        EXPECT_FALSE(vsgUtil::readOmwfxVertexStage(folder.options(), nullptr));
    }

    TEST(OmwfxVertexStage, a_file_that_does_not_compile_gives_null_and_names_the_file)
    {
        OmwfxFolder folder;
        folder.writeText("post/omwfx.vert", sBroken);
        std::string error;
        EXPECT_FALSE(vsgUtil::readOmwfxVertexStage(folder.options(), &error));
        EXPECT_FALSE(error.empty());
        EXPECT_NE(error.find("post/omwfx.vert"), std::string::npos) << error;
        EXPECT_FALSE(vsgUtil::readOmwfxVertexStage(folder.options(), nullptr));
    }

    TEST(OmwfxVertexStage, a_success_clears_an_earlier_error)
    {
        OmwfxFolder folder;
        std::string error = "an earlier error";
        EXPECT_TRUE(vsgUtil::readOmwfxVertexStage(folder.options(), &error));
        EXPECT_TRUE(error.empty()) << error;
    }

    TEST(OmwfxVertexStage, the_exemption_does_not_reach_readPassStages)
    {
        const std::vector<vsgUtil::PassStage> pass = { { "post/omwfx.vert", {} }, { "post/other.frag", {} } };
        {
            SCOPED_TRACE("a twin for the vertex stage only");
            OmwfxFolder folder;
            folder.writeVertexTwin();
            std::string error;
            const auto stages = vsgUtil::readPassStages(pass, folder.slangOptions(), &error);
            ASSERT_EQ(stages.size(), 2u) << error;
            EXPECT_EQ(stages[0]->module->code, compiledAlone(VK_SHADER_STAGE_VERTEX_BIT, sVertex));
            EXPECT_NE(stages[0]->module->code, sVertexTwin);
            EXPECT_EQ(stages[1]->module->code, compiledAlone(VK_SHADER_STAGE_FRAGMENT_BIT, sFragment));
        }
        {
            SCOPED_TRACE("a twin for both");
            OmwfxFolder folder;
            folder.writeVertexTwin();
            folder.writeOtherTwin();
            std::string error;
            const auto stages = vsgUtil::readPassStages(pass, folder.slangOptions(), &error);
            ASSERT_EQ(stages.size(), 2u) << error;
            EXPECT_EQ(stages[0]->module->code, sVertexTwin);
            EXPECT_EQ(stages[1]->module->code, sOtherTwin);
        }
    }

    TEST(OmwfxVertexStage, the_shipped_file_compiles_as_its_text_does_alone)
    {
        const std::filesystem::path file = std::filesystem::path(OPENEDEN_SHADER_SOURCE_DIR) / "post" / "omwfx.vert";
        std::ifstream in(file, std::ios::binary);
        ASSERT_TRUE(in) << file.generic_string();
        std::stringstream text;
        text << in.rdbuf();

        auto options = vsg::Options::create();
        options->paths = { vsg::Path(std::string(OPENEDEN_SHADER_SOURCE_DIR)) };
        options->readerWriters = { vsg::glsl::create(), vsg::spirv::create() };
        options->sharedObjects = vsg::SharedObjects::create();

        std::string error;
        const auto stage = vsgUtil::readOmwfxVertexStage(options, &error);
        ASSERT_TRUE(stage) << error;
        EXPECT_FALSE(stage->module->code.empty());
        EXPECT_EQ(stage->module->code, compiledAlone(VK_SHADER_STAGE_VERTEX_BIT, text.str()));
    }
}
