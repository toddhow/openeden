// Task 0072, stage j (1): recorded interfaces. SlangPermutations compares each built Slang twin with the interface of
// the GLSL stage it replaces, compiled live. Deleting a GLSL file would delete that check, so the GLSL's interfaces are
// recorded once, per stage and define set, in "<stage>.interface" beside the sources (files/shaders):
//
//     # comments
//     defines <names a set of this stage may contain: imported defines plus settings defines>
//     set <canonical define set, or ->
//     <one SpirvInterface line per line>
//     end
//
// The records are GOLDEN DATA: they change only in a pull request that says why, reviewed by toddhow. They are made
// by `SlangInterfaceRecords.write_the_records_when_asked` (set OPENEDEN_RECORD_INTERFACES=1 and run that test in a
// build that has slangc) and are proven against the live GLSL by `records_equal_the_live_glsl_interfaces`, which stays
// as long as the GLSL does. `every_permutation_has_the_recorded_interface` is SlangPermutations' check read from the
// records, with the same `.interface-exceptions` files; it is what remains when a GLSL file is deleted.
#include <gtest/gtest.h>

#include <components/resource/shadersettings.hpp>
#include <components/vsgutil/shaderpermutation.hpp>
#include <components/vsgutil/spirvinterface.hpp>

#include <vsg/all.h>
#include <vsg/io/glsl.h>

#include <algorithm>
#include <cstdlib>
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

    [[maybe_unused]] std::string settingsFileOf(std::string_view name)
    {
        return name == "DEBUG_CHUNKS" ? "terrain.settings" : "shadow.settings";
    }

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

    [[maybe_unused]] std::optional<uint32_t> uvSetsCount(const std::string& name)
    {
        for (uint32_t count = 0; count <= 64; ++count)
        {
            if (name == vsgUtil::uvSetsDefine(count))
                return count;
        }
        return std::nullopt;
    }

    // The pinned constants of a set (its texture coordinate count is the specialization constant 0 of the GLSL).
    [[maybe_unused]] std::map<uint32_t, uint32_t> pinnedConstants(const std::string& defines)
    {
        std::map<uint32_t, uint32_t> pinned;
        std::istringstream names(defines == "-" ? std::string() : defines);
        for (std::string name; names >> name;)
        {
            if (const auto count = uvSetsCount(name))
                pinned[0] = *count;
        }
        return pinned;
    }

    struct Record
    {
        std::set<std::string> mDefines; // the names a set of this stage may contain
        std::map<std::string, std::set<std::string>> mInterface; // canonical set -> interface lines
    };

    [[maybe_unused]] std::optional<Record> readRecord(const std::string& stageFile)
    {
        std::ifstream in(
            std::filesystem::path(OPENEDEN_SHADER_SOURCE_DIR) / (stageFile + ".interface"), std::ios::binary);
        if (!in)
            return std::nullopt;
        Record record;
        std::string current;
        bool inSet = false;
        for (std::string line; std::getline(in, line);)
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (inSet)
            {
                if (line == "end")
                    inSet = false;
                else
                    record.mInterface[current].insert(line);
                continue;
            }
            if (line.empty() || line[0] == '#')
                continue;
            if (line.rfind("defines", 0) == 0)
            {
                std::istringstream names(line.substr(7));
                for (std::string name; names >> name;)
                    record.mDefines.insert(name);
            }
            else if (line.rfind("set ", 0) == 0)
            {
                current = line.substr(4);
                inSet = true;
                record.mInterface[current];
            }
            else
            {
                ADD_FAILURE() << stageFile << ".interface: unknown line: " << line;
            }
        }
        return record;
    }

    [[maybe_unused]] std::set<std::string> possibleDefines(const std::string& stageFile)
    {
        std::set<std::string> possible = vsgUtil::importedDefines(glslSource(stageFile));
        std::set<std::string> allSettingsDefines;
        for (const std::string_view name : Resource::settingsDefineNames())
            allSettingsDefines.emplace(name);
        for (const std::string& name :
            vsgUtil::includedSettingsDefines(glslSource(stageFile, allSettingsDefines), allSettingsDefines))
            possible.insert(name);
        return possible;
    }

    struct Manifest
    {
        std::string mStageFile;
        std::filesystem::path mDirectory;
        vsgUtil::PermutationManifest mFiles;
    };

    [[maybe_unused]] std::vector<Manifest> builtManifests()
    {
        std::vector<Manifest> result;
        const std::filesystem::path built(OPENEDEN_SHADER_BUILD_DIR);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(built))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".permutations")
                continue;
            std::filesystem::path relative = std::filesystem::relative(entry.path(), built);
            relative.replace_extension();
            std::ifstream in(entry.path(), std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            result.push_back(
                { relative.generic_string(), entry.path().parent_path(), vsgUtil::parsePermutationManifest(text) });
        }
        std::sort(result.begin(), result.end(),
            [](const Manifest& a, const Manifest& b) { return a.mStageFile < b.mStageFile; });
        return result;
    }

    // The same exceptions SlangPermutations reads (copied: that test's helpers are private to its file).
    struct Exceptions
    {
        std::map<std::pair<std::string, std::string>, bool> mLines;
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

    TEST(SlangInterfaceRecords, write_the_records_when_asked)
    {
#if !OPENEDEN_SLANG_ENABLED
        GTEST_SKIP() << "this build has no slangc";
#else
        if (!std::getenv("OPENEDEN_RECORD_INTERFACES"))
            GTEST_SKIP() << "set OPENEDEN_RECORD_INTERFACES=1 to write the records into files/shaders";
        for (const Manifest& manifest : builtManifests())
        {
            std::ostringstream out;
            out << "# Recorded interface of the GLSL stage " << manifest.mStageFile
                << " per define set (task 0072, stage j). GOLDEN DATA: changes only in a pull request that says why,\n"
                << "# reviewed by toddhow. Made by SlangInterfaceRecords.write_the_records_when_asked.\n";
            out << "defines";
            for (const std::string& name : possibleDefines(manifest.mStageFile))
                out << ' ' << name;
            out << '\n';
            std::map<std::string, std::set<std::string>> byDefines;
            for (const auto& [defines, file] : manifest.mFiles.mFiles)
            {
                const auto words = compileGlsl(manifest.mStageFile, defines);
                ASSERT_FALSE(words.empty()) << manifest.mStageFile << " [" << defines << "] did not compile";
                byDefines[defines] = vsgUtil::readSpirvInterface(words, pinnedConstants(defines)).mLines;
            }
            for (const auto& [defines, lines] : byDefines)
            {
                out << "set " << defines << '\n';
                for (const std::string& line : lines)
                    out << line << '\n';
                out << "end\n";
            }
            std::ofstream file(std::filesystem::path(OPENEDEN_SHADER_SOURCE_DIR) / (manifest.mStageFile + ".interface"),
                std::ios::binary | std::ios::trunc);
            file << out.str();
        }
#endif
    }

    TEST(SlangInterfaceRecords, records_equal_the_live_glsl_interfaces)
    {
#if !OPENEDEN_SLANG_ENABLED
        GTEST_SKIP() << "this build has no slangc";
#else
        std::size_t checked = 0;
        for (const Manifest& manifest : builtManifests())
        {
            SCOPED_TRACE(manifest.mStageFile);
            const auto record = readRecord(manifest.mStageFile);
            ASSERT_TRUE(record) << manifest.mStageFile << ".interface is missing";
            EXPECT_EQ(record->mDefines, possibleDefines(manifest.mStageFile));
            std::set<std::string> listed;
            for (const auto& [defines, file] : manifest.mFiles.mFiles)
            {
                SCOPED_TRACE("[" + defines + "]");
                listed.insert(defines);
                const auto words = compileGlsl(manifest.mStageFile, defines);
                if (words.empty())
                {
                    ADD_FAILURE() << "the GLSL stage did not compile";
                    continue;
                }
                const auto it = record->mInterface.find(defines);
                if (it == record->mInterface.end())
                {
                    ADD_FAILURE() << "no record for this set";
                    continue;
                }
                EXPECT_EQ(it->second, vsgUtil::readSpirvInterface(words, pinnedConstants(defines)).mLines);
                ++checked;
            }
            for (const auto& [defines, lines] : record->mInterface)
                EXPECT_EQ(listed.count(defines), 1u) << "the record has a set the build does not list: " << defines;
        }
        EXPECT_GT(checked, 0u);
#endif
    }

    TEST(SlangInterfaceRecords, every_permutation_has_the_recorded_interface)
    {
#if !OPENEDEN_SLANG_ENABLED
        GTEST_SKIP() << "this build has no slangc";
#else
        std::size_t checked = 0;
        for (const Manifest& manifest : builtManifests())
        {
            const auto record = readRecord(manifest.mStageFile);
            ASSERT_TRUE(record) << manifest.mStageFile << ".interface is missing";
            Exceptions exceptions = readExceptions(manifest.mStageFile);
            for (const auto& [defines, file] : manifest.mFiles.mFiles)
            {
                SCOPED_TRACE(manifest.mStageFile + " [" + defines + "] " + file);
                std::istringstream names(defines == "-" ? std::string() : defines);
                std::set<std::string> setNames;
                std::optional<uint32_t> uvSets;
                for (std::string name; names >> name;)
                {
                    setNames.insert(name);
                    if (const auto count = uvSetsCount(name))
                    {
                        EXPECT_FALSE(uvSets) << "two texture coordinate counts";
                        uvSets = count;
                        continue;
                    }
                    EXPECT_EQ(record->mDefines.count(name), 1u) << name << " is neither imported nor a settings define";
                }
                if (uvSets)
                {
                    EXPECT_TRUE(vsgUtil::declaresUvSetArrays(setNames)) << "a count without the texture coordinates";
                }
                const auto recorded = record->mInterface.find(defines);
                if (recorded == record->mInterface.end())
                {
                    ADD_FAILURE() << "no record for this set";
                    continue;
                }
                const auto slangWords = readWords(manifest.mDirectory / file);
                if (slangWords.empty())
                {
                    ADD_FAILURE() << file << " was not built";
                    continue;
                }
                const auto slang = vsgUtil::readSpirvInterface(slangWords);
                vsgUtil::SpirvInterface glsl;
                glsl.mLines = recorded->second;
                EXPECT_EQ(slang.mVersion, 0x00010000u);
                const auto diff = vsgUtil::compareSpirvInterfaces(slang, glsl);
                for (const std::string& line : diff.mOnlyInSecond)
                {
                    if (!exceptions.accept(defines, "glsl-only: " + line))
                        ADD_FAILURE() << "the GLSL declares, Slang does not: " << line;
                }
                for (const std::string& line : diff.mOnlyInFirst)
                {
                    if (!isSettingsConstant(line) && !exceptions.accept(defines, "slang-only: " + line))
                        ADD_FAILURE() << "Slang declares, the GLSL does not: " << line;
                }
                EXPECT_EQ(slang.mSpecConstantOps, 0u) << "OpSpecConstantOp in the twin";
                ++checked;
            }
            for (const auto& [key, used] : exceptions.mLines)
            {
                EXPECT_TRUE(used) << manifest.mStageFile
                                  << ".interface-exceptions lists a difference that does not occur: [" << key.first
                                  << "] " << key.second;
            }
        }
        EXPECT_GT(checked, 0u);
#endif
    }
}
