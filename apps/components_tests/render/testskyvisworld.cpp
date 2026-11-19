// Task 0358: the sky-visibility field of a game pack's own world. A field cell is 8192 units wide whatever the game's
// cells measure (Skyrim's are 4096), and each world has its own coordinates and so its own files.
#include <components/render/skyvisibility.hpp>

#include <gtest/gtest.h>

namespace
{
    using namespace Render::Gi;

    TEST(SkyVisWorld, theCellsOfMorrowindMapToThemselves)
    {
        EXPECT_EQ(skyFieldCellOf(3, -7, 8192), std::make_pair(3, -7));
        EXPECT_EQ(skyFieldCellOf(0, 0, 8192), std::make_pair(0, 0));
    }

    TEST(SkyVisWorld, fourSkyrimCellsShareOneFieldCell)
    {
        EXPECT_EQ(skyFieldCellOf(0, 0, 4096), std::make_pair(0, 0));
        EXPECT_EQ(skyFieldCellOf(1, 1, 4096), std::make_pair(0, 0));
        EXPECT_EQ(skyFieldCellOf(2, 3, 4096), std::make_pair(1, 1));
        EXPECT_EQ(skyFieldCellOf(5, 4, 4096), std::make_pair(2, 2));
    }

    TEST(SkyVisWorld, negativeCellsRoundDownNotTowardZero)
    {
        EXPECT_EQ(skyFieldCellOf(-1, -1, 4096), std::make_pair(-1, -1));
        EXPECT_EQ(skyFieldCellOf(-2, -3, 4096), std::make_pair(-1, -2));
        EXPECT_EQ(skyFieldCellOf(-4, -5, 4096), std::make_pair(-2, -3));
    }

    TEST(SkyVisWorld, aCellWiderThanTheFieldStartsAtItsFirstFieldCell)
    {
        EXPECT_EQ(skyFieldCellOf(1, -1, 16384), std::make_pair(2, -2));
    }

    TEST(SkyVisWorld, aNonsenseCellSizeLeavesTheCellAlone)
    {
        EXPECT_EQ(skyFieldCellOf(4, 5, 0), std::make_pair(4, 5));
        EXPECT_EQ(skyFieldCellOf(4, 5, -4096), std::make_pair(4, 5));
    }

    TEST(SkyVisWorld, noWorldIsTheOldPath)
    {
        EXPECT_EQ(skyVisFilePath(-3, 7, ""), skyVisFilePath(-3, 7));
        EXPECT_EQ(skyVisFilePath(-3, 7, ""), "openeden/probes/exterior/-3_7.skyvis.json");
    }

    TEST(SkyVisWorld, aWorldHasAFolderOfItsOwn)
    {
        EXPECT_EQ(skyVisFilePath(-3, 7, "skyrimse:w92445"), "openeden/probes/exterior/skyrimse_w92445/-3_7.skyvis.json");
        EXPECT_EQ(skyVisFilePath(1, 2, "SkyrimSE:W92445"), skyVisFilePath(1, 2, "skyrimse:w92445"));
        EXPECT_NE(skyVisFilePath(1, 2, "skyrimse:w1"), skyVisFilePath(1, 2, "skyrimse:w2"));
    }

    TEST(SkyVisWorld, aWorldNameCannotLeaveItsFolder)
    {
        const std::string path = skyVisFilePath(0, 0, "../..\\evil/x");
        EXPECT_EQ(path.find(".."), std::string::npos);
        EXPECT_EQ(path.find(static_cast<char>(92)), std::string::npos);
        EXPECT_EQ(path.rfind("openeden/probes/exterior/", 0), 0u);
    }
}
