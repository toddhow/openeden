// Task 0135 stage S6f, gated: the root motion of the furniture clips read from the installed Skyrim SE animation data.
// OPENEDEN_SKYRIMSE_ANIMDATA_DIR names a folder that holds `meshes/animationdata/defaultmale.txt` and
// `meshes/animationdata/boundanims/anims_defaultmale.txt` (the user's own `Skyrim - Animations.bsa` extracted; never in
// the repository). The expected numbers are the ones read independently by the Python walker of the stage log
// (`I:/Projects/OpenEden-work/skynpc/py/mot.py`), not by this reader.
#include <game_skyrimse/anim_motion.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
    using namespace game_skyrimse;

    bool readFile(const std::filesystem::path& path, std::string& out)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        std::ostringstream text;
        text << in.rdbuf();
        out = text.str();
        return true;
    }

    TEST(SkyrimAnimMotionRealData, theFurnitureClipsHaveTheMeasuredMotion)
    {
        const char* dir = std::getenv("OPENEDEN_SKYRIMSE_ANIMDATA_DIR");
        if (dir == nullptr || *dir == '\0')
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_ANIMDATA_DIR is not set";
        std::string project, bound;
        const std::filesystem::path root(dir);
        ASSERT_TRUE(readFile(root / "meshes/animationdata/defaultmale.txt", project));
        ASSERT_TRUE(readFile(root / "meshes/animationdata/boundanims/anims_defaultmale.txt", bound));
        MotionTable table;
        const std::size_t joined = table.add(project, bound);
        std::cout << "[motion] clips joined: " << joined << "\n";
        EXPECT_GT(joined, 500u);
        struct Expect
        {
            const char* name;
            double duration, dx, dy, yawAbs;
        };
        // From the independent walker (cumulative displacement at the last row, yaw in radians counter-clockwise).
        const Expect expected[] = {
            { "chair_frontentervar1", 2.83333, 0.0, 63.5, 3.14159 },
            { "chair_leftenter", 1.76667, 0.0, 52.088, 1.5708 },
            { "chair_frontexit", 1.5, -1.07, 47.65, 0.001 },
            { "chair_leftexit", 1.5, -52.088, 0.0, 1.5708 },
            { "bed_leftenter", 6.83333, 2.5177, 74.8192, 1.5708 },
            { "bed_leftexit", 8.06667, -67.2253, 2.71536, 1.546 },
        };
        for (const Expect& e : expected)
        {
            const ClipMotion* m = table.find(e.name);
            ASSERT_NE(m, nullptr) << e.name;
            double dx = 0, dy = 0, yaw = 0;
            m->total(dx, dy, yaw);
            std::cout << "[motion] " << e.name << " " << m->mDuration << " s, (" << dx << ", " << dy << ") yaw " << yaw
                      << "\n";
            EXPECT_NEAR(m->mDuration, e.duration, 0.01) << e.name;
            EXPECT_NEAR(dx, e.dx, 0.5) << e.name;
            EXPECT_NEAR(dy, e.dy, 0.5) << e.name;
            EXPECT_NEAR(std::abs(yaw), e.yawAbs, 0.02) << e.name;
        }
    }
}
