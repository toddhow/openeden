// Task 0363, stage e: the small shader families (GUI, sky, compute, post) must have every set the game asks for built.
// These tests read the shader sources and CMake lists of the repository (OPENEDEN_SHADER_SOURCE_DIR), so they do not
// need a slangc build.
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    std::string readText(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    const std::filesystem::path sShaders = OPENEDEN_SHADER_SOURCE_DIR;

    // The sets of a ".sets" file: one canonical set a line, "-" for the empty one, "#" lines are comments.
    std::set<std::string> readSets(const std::filesystem::path& path)
    {
        std::set<std::string> sets;
        std::istringstream in(readText(path));
        for (std::string line; std::getline(in, line);)
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            sets.insert(line);
        }
        return sets;
    }

    TEST(SmallShaderFamilies, theTonemapListsEveryCombinationOfItsFiveDefines)
    {
        // Render::Tonemap::setSource asks for any subset of these (hdr bloom, gbuffer debug view, ao, sss, and the
        // image space grade of a Skyrim session): all reachable, so all 32 are built.
        const std::vector<std::string> names = { "AO", "BLOOM", "DEBUG_VIEW", "GRADE", "SSS" };
        std::set<std::string> expected;
        for (unsigned mask = 0; mask < 32; ++mask)
        {
            std::string set;
            for (unsigned bit = 0; bit < 5; ++bit)
            {
                if ((mask & (1u << bit)) == 0)
                    continue;
                if (!set.empty())
                    set += ' ';
                set += names[bit];
            }
            expected.insert(set.empty() ? "-" : set);
        }
        EXPECT_EQ(readSets(sShaders / "post/tonemap.frag.sets"), expected);
    }

    TEST(SmallShaderFamilies, everyGlslStageFileHasItsTwinDeclared)
    {
        const std::string cmake = readText(sShaders / "CMakeLists.txt");
        ASSERT_FALSE(cmake.empty());
        std::vector<std::string> missing;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(sShaders))
        {
            const auto extension = entry.path().extension().string();
            if (!entry.is_regular_file() || (extension != ".frag" && extension != ".vert" && extension != ".comp"))
                continue;
            const std::string relative = std::filesystem::relative(entry.path(), sShaders).generic_string();
            if (cmake.find("STAGE_FILE " + relative) == std::string::npos)
                missing.push_back(relative);
        }
        EXPECT_TRUE(missing.empty()) << "GLSL stage files with no declared twin: " << missing.size() << " first "
                                     << (missing.empty() ? "" : missing.front());
    }
}
