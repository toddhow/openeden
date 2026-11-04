// Task 0072, stage g: the stages of the passes whose pipelines C++ builds by hand (vsgUtil::readPassStages,
// components/vsgutil/passstages.hpp). Their GLSL was a string in each pass and is now a file, read like every other
// built-in shader; each stage is compiled on its own, or taken as a Slang twin when every stage of the pass has one.
// Written before the implementation.
#include <gtest/gtest.h>

#include <components/vsgutil/passstages.hpp>
#include <components/vsgutil/shaderpermutation.hpp>

#include <vsg/core/Value.h>
#include <vsg/io/Options.h>
#include <vsg/io/glsl.h>
#include <vsg/io/read.h>
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
#include <string>
#include <vector>

namespace
{
    const std::string sVertex
        = "#version 450\nout gl_PerVertex { vec4 gl_Position; };\n"
          "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    const std::string sFragment
        = "#version 450\n#pragma import_defines (RED)\nlayout(location=0) out vec4 color;\n"
          "void main()\n{\n#ifdef RED\n    color = vec4(1.0, 0.0, 0.0, 1.0);\n#else\n"
          "    color = vec4(0.0);\n#endif\n}\n";
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
    const std::vector<uint32_t> sFragmentTwin = twinWords(200, 1004);

    // The fixture's GLSL compiled as the passes compiled their strings before (one stage, no options), with the
    // given "#define" lines after the version line.
    std::vector<uint32_t> compiledAlone(VkShaderStageFlagBits stage, std::string source, const std::string& defines)
    {
        source.insert(source.find('\n') + 1, defines);
        auto shaderStage = vsg::ShaderStage::create(stage, "main", source);
        if (!vsg::ShaderCompiler::create()->compile(shaderStage))
            return {};
        return shaderStage->module->code;
    }

    // A folder of GLSL stages and Slang twins, read as the game reads the shaders (with shared objects).
    class PassFolder
    {
    public:
        PassFolder()
            : mDir(std::filesystem::temp_directory_path() / uniqueName())
        {
            std::filesystem::create_directories(mDir / "post");
            writeText("post/pass.vert", sVertex);
            writeText("post/pass.frag", sFragment);
            writeText("post/broken.frag", sBroken);
            mOptions = vsg::Options::create();
            mOptions->paths = { vsg::Path(mDir.generic_string()) };
            mOptions->readerWriters = { vsg::glsl::create(), vsg::spirv::create() };
            mOptions->sharedObjects = vsg::SharedObjects::create();
        }
        ~PassFolder()
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
        // Twins of both stages for the set "-" and the fragment's for "RED".
        void writeTwins(bool vertex, bool fragment)
        {
            if (vertex)
            {
                write("post/pass.vert.p00000001.spv", sVertexTwin);
                writeText("post/pass.vert.permutations", "pass.vert.p00000001.spv\t\n");
            }
            if (fragment)
            {
                write("post/pass.frag.p00000002.spv", sFragmentTwin);
                writeText("post/pass.frag.permutations", "pass.frag.p00000002.spv\tRED\n");
            }
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
            return "openeden_passstages_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                + "_" + std::to_string(counter++);
        }

        std::filesystem::path mDir;
        vsg::ref_ptr<vsg::Options> mOptions;
    };

    const std::vector<vsgUtil::PassStage> sRed = { { "post/pass.vert", {} }, { "post/pass.frag", { "RED" } } };

    TEST(PassStages, each_glsl_stage_is_compiled_alone_with_its_defines)
    {
        PassFolder folder;
        const auto stages = vsgUtil::readPassStages(sRed, folder.options());
        ASSERT_EQ(stages.size(), 2u);
        EXPECT_EQ(stages[0]->stage, VK_SHADER_STAGE_VERTEX_BIT);
        EXPECT_EQ(stages[1]->stage, VK_SHADER_STAGE_FRAGMENT_BIT);
        EXPECT_EQ(stages[0]->entryPointName, "main");
        // Exactly the SPIR-V the strings gave when the passes compiled them themselves.
        EXPECT_EQ(stages[0]->module->code, compiledAlone(VK_SHADER_STAGE_VERTEX_BIT, sVertex, ""));
        EXPECT_EQ(stages[1]->module->code, compiledAlone(VK_SHADER_STAGE_FRAGMENT_BIT, sFragment, "#define RED\n"));
        EXPECT_NE(stages[1]->module->code, compiledAlone(VK_SHADER_STAGE_FRAGMENT_BIT, sFragment, ""));
        EXPECT_TRUE(stages[1]->specializationConstants.empty());
    }

    TEST(PassStages, a_name_the_stage_does_not_import_is_not_defined)
    {
        PassFolder folder;
        const auto stages
            = vsgUtil::readPassStages({ { "post/pass.vert", {} }, { "post/pass.frag", { "BLUE" } } }, folder.options());
        ASSERT_EQ(stages.size(), 2u);
        EXPECT_EQ(stages[1]->module->code, compiledAlone(VK_SHADER_STAGE_FRAGMENT_BIT, sFragment, ""));
    }

    TEST(PassStages, the_stages_the_reader_shares_are_never_changed)
    {
        // The reader keeps one stage per file (shared objects); compiling it in place would give every later variant
        // the first variant's code, since VSG compiles only a module without code.
        PassFolder folder;
        const auto shared = vsg::read_cast<vsg::ShaderStage>("post/pass.frag", folder.options());
        ASSERT_TRUE(shared);
        const auto red = vsgUtil::readPassStages(sRed, folder.options());
        const auto plain
            = vsgUtil::readPassStages({ { "post/pass.vert", {} }, { "post/pass.frag", {} } }, folder.options());
        ASSERT_EQ(red.size(), 2u);
        ASSERT_EQ(plain.size(), 2u);
        EXPECT_NE(red[1], shared);
        EXPECT_NE(red[1]->module, shared->module);
        EXPECT_TRUE(shared->module->code.empty());
        EXPECT_NE(red[1]->module->code, plain[1]->module->code);
        EXPECT_EQ(plain[1]->module->code, compiledAlone(VK_SHADER_STAGE_FRAGMENT_BIT, sFragment, ""));
    }

    TEST(PassStages, with_the_switch_on_every_stage_takes_its_twin_and_its_constants)
    {
        PassFolder folder;
        folder.writeTwins(true, true);
        const auto stages = vsgUtil::readPassStages(sRed, folder.slangOptions());
        ASSERT_EQ(stages.size(), 2u);
        EXPECT_EQ(stages[0]->module->code, sVertexTwin);
        EXPECT_EQ(stages[1]->module->code, sFragmentTwin);
        EXPECT_TRUE(stages[0]->module->source.empty());
        EXPECT_TRUE(stages[1]->module->source.empty());
        EXPECT_EQ(stages[0]->stage, VK_SHADER_STAGE_VERTEX_BIT);
        EXPECT_EQ(stages[1]->stage, VK_SHADER_STAGE_FRAGMENT_BIT);
        EXPECT_EQ(stages[0]->specializationConstants.count(1004), 1u);
        EXPECT_EQ(stages[1]->specializationConstants.count(1004), 1u);
    }

    TEST(PassStages, a_stage_without_a_twin_keeps_the_glsl_in_every_stage)
    {
        for (const bool vertexTwin : { true, false })
        {
            SCOPED_TRACE(vertexTwin ? "only the vertex twin" : "only the fragment twin");
            PassFolder folder;
            folder.writeTwins(vertexTwin, !vertexTwin);
            const auto stages = vsgUtil::readPassStages(sRed, folder.slangOptions());
            ASSERT_EQ(stages.size(), 2u);
            EXPECT_EQ(stages[0]->module->code, compiledAlone(VK_SHADER_STAGE_VERTEX_BIT, sVertex, ""));
            EXPECT_EQ(stages[1]->module->code, compiledAlone(VK_SHADER_STAGE_FRAGMENT_BIT, sFragment, "#define RED\n"));
            EXPECT_TRUE(stages[0]->specializationConstants.empty());
            EXPECT_TRUE(stages[1]->specializationConstants.empty());
        }
    }

    TEST(PassStages, an_unlisted_set_or_the_switch_off_keeps_the_glsl)
    {
        PassFolder folder;
        folder.writeTwins(true, true);
        // The fragment twin is built for RED only.
        const auto unlisted
            = vsgUtil::readPassStages({ { "post/pass.vert", {} }, { "post/pass.frag", {} } }, folder.slangOptions());
        ASSERT_EQ(unlisted.size(), 2u);
        EXPECT_EQ(unlisted[0]->module->code, compiledAlone(VK_SHADER_STAGE_VERTEX_BIT, sVertex, ""));
        EXPECT_EQ(unlisted[1]->module->code, compiledAlone(VK_SHADER_STAGE_FRAGMENT_BIT, sFragment, ""));
        const auto off = vsgUtil::readPassStages(sRed, folder.options());
        ASSERT_EQ(off.size(), 2u);
        EXPECT_EQ(off[1]->module->code, compiledAlone(VK_SHADER_STAGE_FRAGMENT_BIT, sFragment, "#define RED\n"));
        EXPECT_FALSE(off[1]->module->source.empty());
    }

    TEST(PassStages, a_missing_file_or_a_compile_error_gives_no_stages)
    {
        PassFolder folder;
        EXPECT_TRUE(vsgUtil::readPassStages({ { "post/pass.vert", {} }, { "post/missing.frag", {} } }, folder.options())
                        .empty());
        EXPECT_TRUE(vsgUtil::readPassStages({ { "post/pass.vert", {} }, { "post/broken.frag", {} } }, folder.options())
                        .empty());
        EXPECT_TRUE(vsgUtil::readPassStages({ { "post/pass.vert", {} } }, nullptr).empty());
    }
}
