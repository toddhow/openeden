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

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
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

    // The settings file a settings define is written into as "#define <name> 1" (Resource::ShaderSettings,
    // components/resource/shadersettings.cpp): DEBUG_CHUNKS into terrain.settings, the others into shadow.settings.
    [[maybe_unused]] std::string settingsFileOf(std::string_view name)
    {
        return name == "DEBUG_CHUNKS" ? "terrain.settings" : "shadow.settings";
    }

    // Task 0072, stage b: the `*.settings` includes are virtual files the engine's shader options serve
    // (createShaderOptions in components/resource/resourcesystem.cpp), so the GLSL compiles as in the game. The test's
    // default settings write no "#define" line; each name of settingsDefines is added to its settings file, so that it
    // lands where the stage includes that file, as the game's settings file puts it (review of the water port).
    [[maybe_unused]] vsg::ref_ptr<vsg::Options> glslOptions(const std::set<std::string>& settingsDefines = {})
    {
        auto options = vsg::Options::create();
        options->paths = { OPENEDEN_SHADER_SOURCE_DIR };
        vsg::ref_ptr<Resource::ShaderSettings> settings{ new Resource::ShaderSettings };
        for (const std::string& name : settingsDefines)
        {
            auto& object = settings->objects[settingsFileOf(name)];
            const auto* module = dynamic_cast<const vsg::ShaderModule*>(object.get());
            std::string text = module ? module->source : std::string();
            if (!text.empty() && text.back() != '\n')
                text += '\n';
            object = vsg::ShaderModule::create(text + "#define " + name + " 1\n");
        }
        options->readerWriters = { settings, vsg::glsl::create() };
        return options;
    }

    // The GLSL source of a stage with its includes inserted, as the game reads it.
    [[maybe_unused]] std::string glslSource(const std::string& file, const std::set<std::string>& settingsDefines = {})
    {
        auto stage = vsg::read_cast<vsg::ShaderStage>(file, glslOptions(settingsDefines));
        return stage && stage->module ? stage->module->source : std::string();
    }

    [[maybe_unused]] std::vector<uint32_t> compileGlsl(const std::string& file, const std::string& canonicalDefines)
    {
        std::vector<std::string> defines;
        std::set<std::string> settingsDefines;
        std::istringstream names(canonicalDefines);
        const auto settingsNames = Resource::settingsDefineNames();
        for (std::string name; names >> name;)
        {
            // Task 0072 (water family): a name of Resource::settingsDefineNames() is a "#define <name> 1" line of a
            // settings file, not a compiler define.
            if (std::find(settingsNames.begin(), settingsNames.end(), name) != settingsNames.end())
                settingsDefines.insert(name);
            else
                defines.push_back(name);
        }
        auto options = glslOptions(settingsDefines);
        auto stage = vsg::read_cast<vsg::ShaderStage>(file, options);
        if (!stage)
            return {};
        vsg::ShaderStages stages{ stage };
        if (!vsg::ShaderCompiler::create()->compile(stages, defines, options))
            return {};
        return stage->module->code;
    }

    // Task 0072, stage b: the differences a family accepts on purpose, from "<stage>.interface-exceptions" beside the
    // GLSL stage (each line "glsl-only: <interface line>" or "slang-only: <interface line>", # for comments).
    // Review of stages b and c: a line may start with "[<canonical define set>] " to hold for that permutation only;
    // a line without it holds for every permutation and is allowed only for a stage that has one.
    struct Exceptions
    {
        std::map<std::pair<std::string, std::string>, bool> mLines; // (set or "", line) -> seen in a difference
        bool accept(const std::string& defines, const std::string& line)
        {
            for (const std::string& key : { defines, std::string() })
            {
                const auto it = mLines.find({ key, line });
                if (it != mLines.end())
                {
                    it->second = true;
                    return true;
                }
            }
            return false;
        }
        bool hasUnscoped() const
        {
            for (const auto& [key, used] : mLines)
            {
                if (key.first.empty())
                    return true;
            }
            return false;
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
            std::string defines;
            if (line[0] == '[')
            {
                const std::size_t close = line.find("] ");
                if (close == std::string::npos)
                {
                    ADD_FAILURE() << stageFile << ".interface-exceptions: no \"] \" after the define set: " << line;
                    continue;
                }
                defines = line.substr(1, close - 1);
                line = line.substr(close + 2);
            }
            exceptions.mLines.emplace(std::make_pair(defines, line), false);
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

    // Task 0072 (review of stages b and c): the stages with twins, exactly. A family added to
    // files/shaders/CMakeLists.txt is added here too, so that a family that stops being built cannot pass unseen.
    [[maybe_unused]] const std::set<std::string>& expectedStages()
    {
        static const std::set<std::string> stages = { "comp/fogofwar/update.comp", "comp/light/assign.comp",
            "comp/particle/simulate.comp", "comp/worldoverlay/update.comp", "default/main.frag", "default/main.vert",
            "gui/gui.frag", "gui/gui.vert", "gui/rml.frag", "gui/rml.vert",
            // Task 0072, stage g: the post passes whose GLSL was written inside C++; stage h: the .omwfx passes' vertex
            // stage.
            "post/ao.frag", "post/bloomdown.frag", "post/bloomup.frag", "post/fullscreen.vert", "post/omwfx.vert",
            "post/present.frag", "post/present.vert", "post/tonemap.frag", "post/velocity.frag",
            "sky/atmosphere/main.frag", "sky/atmosphere/main.vert", "sky/clouds/main.frag", "sky/clouds/main.vert",
            "sky/moon/main.frag", "sky/moon/main.vert", "sky/night/main.frag", "sky/night/main.vert",
            "sky/sun/main.frag", "sky/sun/main.vert", "sky/sunglare/main.frag", "sky/sunglare/main.vert",
            "terrain/main.frag", "terrain/main.vert", "water/main.frag", "water/main.vert" };
        return stages;
    }

    // Task 0072, stage f: the texture coordinate count a set names ("UVSETS_<n>", vsgUtil::uvSetsDefine), or none.
    [[maybe_unused]] std::optional<uint32_t> uvSetsCount(const std::string& name)
    {
        for (uint32_t count = 0; count <= 64; ++count)
        {
            if (name == vsgUtil::uvSetsDefine(count))
                return count;
        }
        return std::nullopt;
    }

    // The table's name for a settings constant id, or none.
    [[maybe_unused]] std::string settingsConstantName(uint32_t id)
    {
        for (const auto& entry : Resource::settingsConstantTable())
        {
            if (entry.mId == id)
                return std::string(entry.mName);
        }
        return {};
    }

    TEST(SlangPermutations, every_permutation_has_the_interface_of_its_glsl_stage)
    {
#if !OPENEDEN_SLANG_ENABLED
        GTEST_SKIP() << "this build has no slangc (OPENEDEN_BUILD_SLANG_SHADERS is off or there is no archive)";
#else
        const std::filesystem::path built(OPENEDEN_SHADER_BUILD_DIR);
        std::size_t checked = 0;
        std::set<std::string> found;
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
            found.insert(stageFile);
            Exceptions exceptions = readExceptions(stageFile);
            if (manifest.mFiles.size() > 1)
            {
                EXPECT_FALSE(exceptions.hasUnscoped())
                    << stageFile << ".interface-exceptions: with several permutations every line names its set";
            }
            // Review of stages b and c: a listed name the stage neither imports nor gets from a settings file gives a
            // permutation that can never be chosen.
            // A settings define counts only for a stage that includes its settings file (review of the water port).
            std::set<std::string> possible = vsgUtil::importedDefines(glslSource(stageFile));
            std::set<std::string> allSettingsDefines;
            for (const std::string_view name : Resource::settingsDefineNames())
                allSettingsDefines.emplace(name);
            for (const std::string& name :
                vsgUtil::includedSettingsDefines(glslSource(stageFile, allSettingsDefines), allSettingsDefines))
                possible.insert(name);
            for (const auto& [defines, file] : manifest.mFiles)
            {
                SCOPED_TRACE(stageFile + " [" + defines + "] " + file);
                std::istringstream names(defines == "-" ? std::string() : defines);
                // Stage f: a texture coordinate count is not a GLSL define but the pipeline's numUvSets (id 0), which
                // the twin of that count was built for: it stands only beside the arrays (TEXCOORD or PARTICLE),
                // once, and the GLSL is read with id 0 pinned to it.
                std::optional<uint32_t> uvSets;
                std::set<std::string> setNames;
                for (std::string name; names >> name;)
                {
                    setNames.insert(name);
                    if (const auto count = uvSetsCount(name))
                    {
                        EXPECT_FALSE(uvSets) << "two texture coordinate counts";
                        uvSets = count;
                        continue;
                    }
                    EXPECT_EQ(possible.count(name), 1u) << name << " is neither imported nor a settings define";
                }
                std::map<uint32_t, uint32_t> pinned;
                if (uvSets)
                {
                    EXPECT_TRUE(vsgUtil::declaresUvSetArrays(setNames)) << "a count without the texture coordinates";
                    pinned[0] = *uvSets;
                }
                const auto slangWords = readWords(entry.path().parent_path() / file);
                const auto glslWords = compileGlsl(stageFile, defines);
                // Stage b: every family is checked, not only those before the first failure.
                if (slangWords.empty() || glslWords.empty())
                {
                    ADD_FAILURE() << (slangWords.empty() ? file + " was not built" : "the GLSL stage did not compile");
                    continue;
                }
                const auto slang = vsgUtil::readSpirvInterface(slangWords);
                const auto glsl = vsgUtil::readSpirvInterface(glslWords, pinned);
                // The SPIR-V version the Vulkan 1.0 instance can take.
                EXPECT_EQ(slang.mVersion, 0x00010000u);
                const auto diff = vsgUtil::compareSpirvInterfaces(slang, glsl);
                for (const std::string& line : diff.mOnlyInSecond)
                {
                    if (!exceptions.accept(defines, "glsl-only: " + line))
                        ADD_FAILURE() << "GLSL declares, Slang does not: " << line;
                }
                for (const std::string& line : diff.mOnlyInFirst)
                {
                    if (!isSettingsConstant(line) && !exceptions.accept(defines, "slang-only: " + line))
                        ADD_FAILURE() << "Slang declares, GLSL does not: " << line;
                }
                // Review of stages b and c: no expression over specialization constants (an OpSpecConstantOp, the
                // pattern apps/openmw/mwrender/CLAUDE.md records as miscompiled by the NVIDIA driver); branch on the
                // constant itself.
                EXPECT_EQ(slang.mSpecConstantOps, 0u) << "OpSpecConstantOp in the twin";
                // A settings constant's id carries the table's name, so that two ids of the same type cannot swap.
                for (const auto& [id, name] : slang.mSpecConstantNames)
                {
                    if (id < Resource::settingsConstantBase)
                        continue;
                    const std::string expected = settingsConstantName(id);
                    EXPECT_FALSE(expected.empty()) << "id " << id << " is not in the settings constant table";
                    EXPECT_EQ(name, expected) << "id " << id;
                }
                EXPECT_GT(slang.mLines.size(), 2u);
                ++checked;
            }
            // A stale entry would let a real difference through later.
            for (const auto& [key, used] : exceptions.mLines)
                EXPECT_TRUE(used) << stageFile << ".interface-exceptions lists a difference that does not occur: ["
                                  << key.first << "] " << key.second;
        }
        // Review of stages b and c: exactly the declared stages, and the declared stages are the expected ones.
        std::set<std::string> declared;
        std::ifstream list(built / "slang-permutation-stages.txt", std::ios::binary);
        for (std::string line; std::getline(list, line);)
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (!line.empty())
                declared.insert(line);
        }
        EXPECT_EQ(found, declared) << "manifests in the build folder differ from slang-permutation-stages.txt";
        EXPECT_EQ(declared, expectedStages());
        EXPECT_GE(checked, expectedStages().size());
#endif
    }
}
