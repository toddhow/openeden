// Task 0072: every Slang permutation the build wrote (each "<stage>.permutations" file under the built shaders) must
// declare exactly what its GLSL stage declares when compiled with the same defines: descriptor sets and bindings,
// block layouts, push constants, interface locations, built-ins, specialization constants and the entry point. The
// C++ side builds the pipeline layouts by hand and links stages by location, so any difference is a bug.
#include <gtest/gtest.h>

#include <components/vsgutil/shaderpermutation.hpp>
#include <components/vsgutil/spirvinterface.hpp>

#include <vsg/all.h>
#include <vsg/io/glsl.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    std::vector<uint32_t> readWords(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in)
            return {};
        const std::streamsize size = in.tellg();
        std::vector<uint32_t> words(static_cast<std::size_t>(size) / sizeof(uint32_t));
        in.seekg(0);
        in.read(reinterpret_cast<char*>(words.data()), size);
        return words;
    }

    std::vector<uint32_t> compileGlsl(const std::string& file, const std::string& canonicalDefines)
    {
        auto options = vsg::Options::create();
        options->paths = { OPENEDEN_SHADER_SOURCE_DIR };
        options->readerWriters = { vsg::glsl::create() };
        auto stage = vsg::read_cast<vsg::ShaderStage>(file, options);
        if (!stage)
            return {};
        std::vector<std::string> defines;
        std::istringstream names(canonicalDefines);
        for (std::string name; names >> name;)
            defines.push_back(name);
        vsg::ShaderStages stages{ stage };
        if (!vsg::ShaderCompiler::create()->compile(stages, defines, options))
            return {};
        return stage->module->code;
    }

    TEST(SlangPermutations, every_permutation_has_the_interface_of_its_glsl_stage)
    {
#if !OPENEDEN_SLANG_ENABLED
        GTEST_SKIP() << "this build has no slangc (OPENEDEN_BUILD_SLANG_SHADERS is off or there is no archive)";
#else
        const std::filesystem::path built(OPENEDEN_SHADER_BUILD_DIR);
        std::size_t checked = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(built))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".permutations")
                continue;
            // "<built>/gui/gui.vert.permutations" -> the GLSL stage "gui/gui.vert"
            std::filesystem::path relative = std::filesystem::relative(entry.path(), built);
            relative.replace_extension();
            const std::string stageFile = relative.generic_string();
            std::ifstream in(entry.path(), std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const auto manifest = vsgUtil::parsePermutationManifest(text);
            for (const auto& [defines, file] : manifest.mFiles)
            {
                SCOPED_TRACE(stageFile + " [" + defines + "] " + file);
                const auto slangWords = readWords(entry.path().parent_path() / file);
                ASSERT_FALSE(slangWords.empty()) << file << " was not built";
                const auto glslWords = compileGlsl(stageFile, defines);
                ASSERT_FALSE(glslWords.empty()) << "the GLSL stage did not compile";
                const auto slang = vsgUtil::readSpirvInterface(slangWords);
                const auto glsl = vsgUtil::readSpirvInterface(glslWords);
                // The SPIR-V version the Vulkan 1.0 instance can take.
                EXPECT_EQ(slang.mVersion, 0x00010000u);
                const auto diff = vsgUtil::compareSpirvInterfaces(slang, glsl);
                for (const std::string& line : diff.mOnlyInSecond)
                    ADD_FAILURE() << "GLSL declares, Slang does not: " << line;
                for (const std::string& line : diff.mOnlyInFirst)
                    ADD_FAILURE() << "Slang declares, GLSL does not: " << line;
                EXPECT_GT(slang.mLines.size(), 2u);
                ++checked;
            }
        }
        EXPECT_GE(checked, 2u) << "no permutations were built";
#endif
    }
}
