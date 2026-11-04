// Task 0072: every Slang permutation the build wrote (each "<stage>.permutations" file under the built shaders) must
// declare exactly what its GLSL stage declares when compiled with the same defines: descriptor sets and bindings,
// block layouts, push constants, interface locations, built-ins, specialization constants and the entry point. The
// C++ side builds the pipeline layouts by hand and links stages by location, so any difference is a bug.
#include <gtest/gtest.h>

#include <components/resource/shadersettings.hpp>
#include <components/vsgutil/shaderpermutation.hpp>
#include <components/vsgutil/spirvinterface.hpp>

#include <vsg/all.h>
#include <vsg/io/glsl.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    // Used only when the build has slangc; [[maybe_unused]] keeps GCC's -Werror=unused-function quiet otherwise.
    [[maybe_unused]] std::vector<uint32_t> readWords(const std::filesystem::path& path)
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

    [[maybe_unused]] std::vector<uint32_t> compileGlsl(const std::string& file, const std::string& canonicalDefines)
    {
        auto options = vsg::Options::create();
        options->paths = { OPENEDEN_SHADER_SOURCE_DIR };
        // Task 0072, stage b: the `*.settings` includes are virtual files the engine's shader options serve
        // (createShaderOptions in components/resource/resourcesystem.cpp), so the GLSL compiles as in the game.
        options->readerWriters = { vsg::ref_ptr{ new Resource::ShaderSettings }, vsg::glsl::create() };
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

    // Task 0072, stage b: the differences a family accepts on purpose, from "<stage>.interface-exceptions" beside the
    // GLSL stage (each line "glsl-only: <interface line>" or "slang-only: <interface line>", # for comments).
    struct Exceptions
    {
        std::map<std::string, bool> mLines; // line -> seen in a difference
        bool accept(const std::string& line)
        {
            const auto it = mLines.find(line);
            if (it == mLines.end())
                return false;
            it->second = true;
            return true;
        }
    };

    [[maybe_unused]] Exceptions readExceptions(const std::string& stageFile)
    {
        Exceptions exceptions;
        std::ifstream in(std::filesystem::path(OPENEDEN_SHADER_SOURCE_DIR) / (stageFile + ".interface-exceptions"),
            std::ios::binary);
        for (std::string line; std::getline(in, line);)
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            exceptions.mLines.emplace(line, false);
        }
        return exceptions;
    }

    // A twin takes the GLSL's `*.settings` constants as specialization constants with the ids and types of
    // Resource::settingsConstantTable; the GLSL has them as plain constants, so these lines exist in the twin only.
    [[maybe_unused]] bool isSettingsConstant(const std::string& line)
    {
        for (const auto& entry : Resource::settingsConstantTable())
        {
            const std::string id = "id=" + std::to_string(entry.mId) + " ";
            const char* type = entry.mType == 'b' ? "bool" : entry.mType == 'i' ? "i32" : "f32";
            if (line == "spec_constant " + id + type || line.rfind("spec_constant_default " + id, 0) == 0)
                return true;
        }
        return false;
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
            Exceptions exceptions = readExceptions(stageFile);
            for (const auto& [defines, file] : manifest.mFiles)
            {
                SCOPED_TRACE(stageFile + " [" + defines + "] " + file);
                const auto slangWords = readWords(entry.path().parent_path() / file);
                const auto glslWords = compileGlsl(stageFile, defines);
                // Stage b: every family is checked, not only those before the first failure.
                if (slangWords.empty() || glslWords.empty())
                {
                    ADD_FAILURE() << (slangWords.empty() ? file + " was not built" : "the GLSL stage did not compile");
                    continue;
                }
                const auto slang = vsgUtil::readSpirvInterface(slangWords);
                const auto glsl = vsgUtil::readSpirvInterface(glslWords);
                // The SPIR-V version the Vulkan 1.0 instance can take.
                EXPECT_EQ(slang.mVersion, 0x00010000u);
                const auto diff = vsgUtil::compareSpirvInterfaces(slang, glsl);
                for (const std::string& line : diff.mOnlyInSecond)
                    if (!exceptions.accept("glsl-only: " + line))
                        ADD_FAILURE() << "GLSL declares, Slang does not: " << line;
                for (const std::string& line : diff.mOnlyInFirst)
                    if (!isSettingsConstant(line) && !exceptions.accept("slang-only: " + line))
                        ADD_FAILURE() << "Slang declares, GLSL does not: " << line;
                EXPECT_GT(slang.mLines.size(), 2u);
                ++checked;
            }
            // A stale entry would let a real difference through later.
            for (const auto& [line, used] : exceptions.mLines)
                EXPECT_TRUE(used) << stageFile
                                  << ".interface-exceptions lists a difference that does not occur: " << line;
        }
        EXPECT_GE(checked, 2u) << "no permutations were built";
#endif
    }
}
