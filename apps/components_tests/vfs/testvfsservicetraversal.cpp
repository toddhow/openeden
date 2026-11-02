#include <components/extension/vfs_service.hpp>
#include <components/vfs/entryname.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace
{
    // The extension VFS service resolves a virtual path to a file under a mounted folder. A path with '..' in it must
    // never resolve to a file outside that folder (task 0050, stage b).
    TEST(VFSServiceTraversalTest, parentComponentsShouldNotLeaveTheMount)
    {
        const std::filesystem::path root = std::filesystem::temp_directory_path() / "openeden_vfs_traversal_test";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "mount");
        std::ofstream(root / "secret.txt") << "secret";
        std::ofstream(root / "mount" / "inside.txt") << "inside";

        openeden::extension::VfsServiceImpl service;
        service.mount("data", root / "mount");

        EXPECT_TRUE(service.hasFile("data/inside.txt"));
        EXPECT_FALSE(service.hasFile("data/../secret.txt"));
        EXPECT_FALSE(service.hasFile("data/sub/../../secret.txt"));
        EXPECT_FALSE(service.hasFile("data//secret.txt"));
        EXPECT_FALSE(service.hasFile("data\\..\\secret.txt"));

        std::filesystem::remove_all(root);
    }

    // Review findings of task 0050: names Windows treats as devices although they look different.
    TEST(VFSEntryNameExtraTest, windowsDeviceNamesInDisguiseShouldNotBeSafeToExtract)
    {
        EXPECT_TRUE(VFS::Path::checkExtractionSafe("con .txt").has_value());
        EXPECT_TRUE(VFS::Path::checkExtractionSafe("a/COM\xC2\xB9").has_value());
        EXPECT_TRUE(VFS::Path::checkExtractionSafe("lpt\xC2\xB3.log").has_value());
        EXPECT_TRUE(VFS::Path::checkExtractionSafe("conout$").has_value());
        EXPECT_FALSE(VFS::Path::checkExtractionSafe("com0.txt").has_value());
        EXPECT_FALSE(VFS::Path::checkExtractionSafe("console.txt").has_value());
    }
}
