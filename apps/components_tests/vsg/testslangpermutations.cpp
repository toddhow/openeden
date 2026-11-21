// Task 0072 and task 0363, stage g: the stages with Slang twins are exactly the declared ones (the build wrote one
// "<stage>.permutations" file for each, and `slang-permutation-stages.txt` lists them). The interface of every twin is
// checked against the recorded interface of its stage in testslanginterfacerecords.cpp (there is no GLSL to compare
// with since stage g).
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace
{
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

    TEST(SlangPermutations, the_stages_with_twins_are_exactly_the_declared_ones)
    {
#if !OPENEDEN_SLANG_ENABLED
        GTEST_SKIP() << "this build has no slangc (OPENEDEN_BUILD_SLANG_SHADERS is off or there is no archive)";
#else
        const std::filesystem::path built(OPENEDEN_SHADER_BUILD_DIR);
        std::set<std::string> found;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(built))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".permutations")
                continue;
            // "<built>/gui/gui.vert.permutations" -> the stage "gui/gui.vert"
            std::filesystem::path relative = std::filesystem::relative(entry.path(), built);
            relative.replace_extension();
            found.insert(relative.generic_string());
        }
        std::set<std::string> declared;
        std::ifstream list(built / "slang-permutation-stages.txt", std::ios::binary);
        for (std::string line; std::getline(list, line);)
        {
            if (!line.empty() && line.back() == '')
                line.pop_back();
            if (!line.empty())
                declared.insert(line);
        }
        EXPECT_EQ(found, declared) << "manifests in the build folder differ from slang-permutation-stages.txt";
        EXPECT_EQ(declared, expectedStages());
#endif
    }
}
