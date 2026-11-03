// Task 0064: Files::isDataPackFile and the places in the files component that must let a pack file stand in a data
// path list: ConfigurationManager::filterOutNonExistingPaths, Collections::getPath and doesExist, and the
// MultiDirCollection behind getCollection. Written from the task text alone; the files are generated.

#include <components/files/collections.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/multidircollection.hpp>
#include <components/testing/util.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace Files
{
    namespace
    {
        using namespace testing;

        void writeText(const std::filesystem::path& path, const std::string& text)
        {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream << text;
        }

        struct FilesDataPackTest : Test
        {
            std::filesystem::path mRoot;
            std::filesystem::path mFolderA;
            std::filesystem::path mFolderC;
            std::filesystem::path mPack;

            void SetUp() override
            {
                static std::atomic<unsigned> counter{ 0 };
                static const unsigned salt = std::random_device{}();
                mRoot = TestingOpenMW::outputDir() / std::format("datapack-{}-{}", salt, counter++);
                std::error_code ec;
                std::filesystem::remove_all(mRoot, ec);
                mFolderA = mRoot / "a";
                mFolderC = mRoot / "c";
                mPack = mRoot / "mod.edenpak";

                writeText(mFolderA / "a.esp", "plugin a");
                writeText(mFolderA / "shared.txt", "shared a");
                writeText(mFolderC / "c.esp", "plugin c");
                writeText(mFolderC / "only_c.txt", "only c");
                // The content of a pack does not matter to this component: it only looks at the name.
                writeText(mPack, "placeholder bytes");
            }

            void TearDown() override
            {
                std::error_code ec;
                std::filesystem::remove_all(mRoot, ec);
            }
        };

        // ---- isDataPackFile ----

        TEST_F(FilesDataPackTest, isDataPackFileShouldBeTrueForAPackFile)
        {
            EXPECT_TRUE(isDataPackFile(mPack));
        }

        TEST_F(FilesDataPackTest, isDataPackFileShouldIgnoreTheCaseOfTheExtension)
        {
            const std::filesystem::path upper = mRoot / "UPPER.EDENPAK";
            const std::filesystem::path mixed = mRoot / "Mixed.EdenPak";
            writeText(upper, "x");
            writeText(mixed, "x");
            EXPECT_TRUE(isDataPackFile(upper));
            EXPECT_TRUE(isDataPackFile(mixed));
        }

        TEST_F(FilesDataPackTest, isDataPackFileShouldBeFalseForADirectoryNamedLikeAPack)
        {
            const std::filesystem::path directory = mRoot / "folder.edenpak";
            std::filesystem::create_directories(directory);
            EXPECT_FALSE(isDataPackFile(directory));
        }

        TEST_F(FilesDataPackTest, isDataPackFileShouldBeFalseForAMissingPath)
        {
            EXPECT_FALSE(isDataPackFile(mRoot / "missing.edenpak"));
        }

        TEST_F(FilesDataPackTest, isDataPackFileShouldBeFalseForOtherFiles)
        {
            const std::filesystem::path text = mRoot / "notes.txt";
            const std::filesystem::path suffixed = mRoot / "mod.edenpak.bak";
            writeText(text, "x");
            writeText(suffixed, "x");
            EXPECT_FALSE(isDataPackFile(text));
            EXPECT_FALSE(isDataPackFile(suffixed));
            EXPECT_FALSE(isDataPackFile(mFolderA));
        }

        // ---- ConfigurationManager::filterOutNonExistingPaths ----

        TEST_F(FilesDataPackTest, filterOutNonExistingPathsShouldKeepFoldersAndPacksInOrder)
        {
            const std::filesystem::path text = mRoot / "notes.txt";
            writeText(text, "x");
            ConfigurationManager manager(true);
            PathContainer paths{ mFolderC, mPack, text, mRoot / "missing", mFolderA, mRoot / "missing.edenpak" };

            manager.filterOutNonExistingPaths(paths);

            EXPECT_THAT(paths, ElementsAre(mFolderC, mPack, mFolderA));
        }

        TEST_F(FilesDataPackTest, filterOutNonExistingPathsShouldKeepAnUppercasePack)
        {
            const std::filesystem::path upper = mRoot / "UPPER.EDENPAK";
            writeText(upper, "x");
            ConfigurationManager manager(true);
            PathContainer paths{ upper };

            manager.filterOutNonExistingPaths(paths);

            EXPECT_THAT(paths, ElementsAre(upper));
        }

        TEST_F(FilesDataPackTest, filterOutNonExistingPathsShouldStillKeepFolders)
        {
            ConfigurationManager manager(true);
            PathContainer paths{ mFolderA, mFolderC };

            manager.filterOutNonExistingPaths(paths);

            EXPECT_THAT(paths, ElementsAre(mFolderA, mFolderC));
        }

        // ---- Collections with a pack in the list ----

        TEST_F(FilesDataPackTest, collectionsShouldFindFilesInFoldersAroundAPack)
        {
            const Collections collections({ mFolderA, mPack, mFolderC });

            EXPECT_TRUE(collections.doesExist("only_c.txt"));
            EXPECT_TRUE(collections.doesExist("shared.txt"));
            EXPECT_FALSE(collections.doesExist("missing.txt"));
            EXPECT_EQ(collections.getPath("only_c.txt"), mFolderC / "only_c.txt");
            EXPECT_EQ(collections.getPath("shared.txt"), mFolderA / "shared.txt");
        }

        TEST_F(FilesDataPackTest, collectionsShouldThrowForAMissingFileWithAPackInTheList)
        {
            const Collections collections({ mPack, mFolderA });

            EXPECT_THROW((void)collections.getPath("missing.txt"), std::runtime_error);
            EXPECT_FALSE(collections.doesExist("missing.txt"));
        }

        TEST_F(FilesDataPackTest, collectionsShouldKeepThePackInGetPaths)
        {
            const Collections collections({ mFolderA, mPack, mFolderC });

            EXPECT_THAT(collections.getPaths(), ElementsAre(mFolderA, mPack, mFolderC));
        }

        TEST_F(FilesDataPackTest, collectionShouldListThePluginsOfTheFoldersAndIgnoreThePack)
        {
            const Collections collections({ mFolderA, mPack, mFolderC });

            const MultiDirCollection& plugins = collections.getCollection("esp");

            std::vector<std::filesystem::path> found;
            for (const auto& [name, path] : plugins)
                found.push_back(path);
            EXPECT_THAT(found, UnorderedElementsAre(mFolderA / "a.esp", mFolderC / "c.esp"));
            EXPECT_TRUE(plugins.doesExist("a.esp"));
            EXPECT_TRUE(plugins.doesExist("c.esp"));
            EXPECT_EQ(plugins.getPath("c.esp"), mFolderC / "c.esp");
        }

        TEST_F(FilesDataPackTest, multiDirCollectionShouldSkipAPackFile)
        {
            const MultiDirCollection plugins({ mPack, mFolderA }, "esp");
            EXPECT_TRUE(plugins.doesExist("a.esp"));
        }
    }
}
