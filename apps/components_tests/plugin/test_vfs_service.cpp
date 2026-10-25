#include <components/plugin/load_error.hpp>
#include <components/plugin/vfs_service.hpp>
#include <openeden/vfs.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using openeden::plugin::LoadError;
    using openeden::plugin::VfsServiceImpl;
    using openeden::sdk::FileError;
    using openeden::sdk::FileFormatReader;
    using openeden::sdk::VfsService;

    // RAII directory fixture to safely manage test directories on disk
    class TempTestDirectory
    {
    public:
        TempTestDirectory(std::string_view name)
        {
            mPath = std::filesystem::temp_directory_path() / ("openeden_vfs_test_" + std::string(name));
            std::filesystem::remove_all(mPath);
            std::filesystem::create_directories(mPath);
        }

        ~TempTestDirectory()
        {
            std::error_code ec;
            std::filesystem::remove_all(mPath, ec);
        }

        void writeFile(const std::string& relativePath, std::string_view content)
        {
            const auto fullPath = mPath / relativePath;
            std::filesystem::create_directories(fullPath.parent_path());
            std::ofstream out(fullPath, std::ios::binary);
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
        }

        const std::filesystem::path& path() const noexcept { return mPath; }

    private:
        std::filesystem::path mPath;
    };

    // Mock format reader for testing dispatch and located error handling
    class MockAnimGraphReader : public FileFormatReader
    {
    public:
        bool read(std::string_view path, std::span<const std::uint8_t> data, FileError* outError) override
        {
            mLastReadPath = path;
            mLastReadData.assign(data.begin(), data.end());

            std::string content(data.begin(), data.end());
            if (content.find("INVALID") != std::string::npos)
            {
                if (outError)
                {
                    outError->file = std::string(path);
                    outError->line = 42;
                    outError->field = "/nodes/transition_0";
                    outError->message = "Malformed state transition definition";
                }
                return false;
            }

            return true;
        }

        std::string mLastReadPath;
        std::vector<std::uint8_t> mLastReadData;
    };

    TEST(VfsServiceTest, MountPluginDataDirectoryOverBaseVfs)
    {
        TempTestDirectory baseDir("base");
        TempTestDirectory pluginDir("plugin_motion");

        baseDir.writeFile("base_asset.txt", "base_content");
        pluginDir.writeFile("motion_asset.txt", "motion_content");

        VfsServiceImpl vfs;
        vfs.mount("data", baseDir.path(), 0);
        vfs.mount("data", pluginDir.path(), 10);

        EXPECT_TRUE(vfs.hasFile("data/base_asset.txt"));
        EXPECT_TRUE(vfs.hasFile("data/motion_asset.txt"));

        const auto motionBytes = vfs.readFile("data/motion_asset.txt");
        const std::string motionStr(motionBytes.begin(), motionBytes.end());
        EXPECT_EQ(motionStr, "motion_content");
    }

    TEST(VfsServiceTest, LaterMountedPluginOverlaysEarlierInLoadOrder)
    {
        TempTestDirectory baseDir("overlay_base");
        TempTestDirectory pluginADir("overlay_plugin_a");
        TempTestDirectory pluginBDir("overlay_plugin_b");

        baseDir.writeFile("shared.txt", "content_from_base");
        pluginADir.writeFile("shared.txt", "content_from_plugin_a");
        pluginBDir.writeFile("shared.txt", "content_from_plugin_b");

        VfsServiceImpl vfs;
        vfs.setPluginLoadOrder({ "plugin_a", "plugin_b" });

        vfs.mount("data", baseDir.path(), 0);
        vfs.mountPluginData("plugin_a", pluginADir.path());
        vfs.mountPluginData("plugin_b", pluginBDir.path());

        // Invariant: later plugin in load order overlays earlier files
        const auto sharedBytes = vfs.readFile("data/shared.txt");
        const std::string sharedStr(sharedBytes.begin(), sharedBytes.end());
        EXPECT_EQ(sharedStr, "content_from_plugin_b");
    }

    TEST(VfsServiceTest, FallbackToEarlierMountWhenFileNotInLaterMount)
    {
        TempTestDirectory pluginADir("fallback_plugin_a");
        TempTestDirectory pluginBDir("fallback_plugin_b");

        pluginADir.writeFile("unique_to_a.txt", "alpha_data");

        VfsServiceImpl vfs;
        vfs.setPluginLoadOrder({ "plugin_a", "plugin_b" });

        vfs.mountPluginData("plugin_a", pluginADir.path());
        vfs.mountPluginData("plugin_b", pluginBDir.path());

        EXPECT_TRUE(vfs.hasFile("data/unique_to_a.txt"));

        const auto bytes = vfs.readFile("data/unique_to_a.txt");
        const std::string str(bytes.begin(), bytes.end());
        EXPECT_EQ(str, "alpha_data");
    }

    TEST(VfsServiceTest, RegisterFormatReaderAndDispatchRead)
    {
        TempTestDirectory dataDir("reader_valid");
        dataDir.writeFile("anims/walk.animgraph", "VALID_ANIMGRAPH_DATA");

        VfsServiceImpl vfs;
        vfs.mount("data", dataDir.path(), 0);

        auto reader = std::make_shared<MockAnimGraphReader>();
        vfs.registerFormatReader(".animgraph", reader);

        FileError error;
        const bool success = vfs.readAsset("data/anims/walk.animgraph", &error);

        EXPECT_TRUE(success);
        EXPECT_EQ(reader->mLastReadPath, "data/anims/walk.animgraph");
        const std::string readContent(reader->mLastReadData.begin(), reader->mLastReadData.end());
        EXPECT_EQ(readContent, "VALID_ANIMGRAPH_DATA");
        EXPECT_TRUE(error.message.empty());
    }

    TEST(VfsServiceTest, FormatReaderReturnsLocatedErrorsWithoutThrowing)
    {
        TempTestDirectory dataDir("reader_corrupt");
        dataDir.writeFile("anims/broken.animgraph", "INVALID_ANIMGRAPH_DATA");

        VfsServiceImpl vfs;
        vfs.mount("data", dataDir.path(), 0);

        auto reader = std::make_shared<MockAnimGraphReader>();
        vfs.registerFormatReader(".animgraph", reader);

        FileError error;
        bool success = false;

        // Invariant: reader reports located error and never throws
        EXPECT_NO_THROW({ success = vfs.readAsset("data/anims/broken.animgraph", &error); });

        EXPECT_FALSE(success);
        EXPECT_EQ(error.file, "data/anims/broken.animgraph");
        EXPECT_EQ(error.line, 42u);
        EXPECT_EQ(error.field, "/nodes/transition_0");
        EXPECT_EQ(error.message, "Malformed state transition definition");
    }

    TEST(VfsServiceTest, DispatchToUnregisteredExtensionReportsErrorWithoutThrowing)
    {
        TempTestDirectory dataDir("reader_unregistered");
        dataDir.writeFile("models/cube.custom_mesh", "MESH_DATA");

        VfsServiceImpl vfs;
        vfs.mount("data", dataDir.path(), 0);

        FileError error;
        bool success = false;

        EXPECT_NO_THROW({ success = vfs.readAsset("data/models/cube.custom_mesh", &error); });

        EXPECT_FALSE(success);
        EXPECT_FALSE(error.message.empty());
    }

    TEST(VfsServiceTest, ReadNonExistentFileReportsFileNotFound)
    {
        VfsServiceImpl vfs;
        auto reader = std::make_shared<MockAnimGraphReader>();
        vfs.registerFormatReader(".animgraph", reader);

        FileError error;
        const bool success = vfs.readAsset("data/missing.animgraph", &error);

        EXPECT_FALSE(success);
        EXPECT_FALSE(error.message.empty());
    }

    TEST(VfsServiceTest, MultiDotExtensionMatchingAndExceptionContainment)
    {
        TempTestDirectory dataDir("multi_dot_vfs");
        dataDir.writeFile("anims/idle.opas.animgraph", "OPAS_DATA");

        VfsServiceImpl vfs;
        vfs.mount("data", dataDir.path(), 0);

        class MultiDotReader : public FileFormatReader
        {
        public:
            bool called = false;
            bool read(std::string_view, std::span<const std::uint8_t>, FileError*) override
            {
                called = true;
                return true;
            }
        };

        auto opasReader = std::make_shared<MultiDotReader>();
        vfs.registerFormatReader(".opas.animgraph", opasReader);

        FileError error;
        EXPECT_TRUE(vfs.readAsset("data/anims/idle.opas.animgraph", &error));
        EXPECT_TRUE(opasReader->called);

        // Reader throwing exception
        class ThrowingReader : public FileFormatReader
        {
        public:
            bool read(std::string_view, std::span<const std::uint8_t>, FileError*) override
            {
                throw std::runtime_error("Parser crashed");
            }
        };

        dataDir.writeFile("anims/crash.broken", "CRASH_DATA");
        vfs.registerFormatReader(".broken", std::make_shared<ThrowingReader>());

        EXPECT_NO_THROW({
            const bool result = vfs.readAsset("data/anims/crash.broken", &error);
            EXPECT_FALSE(result);
            EXPECT_NE(error.message.find("Parser crashed"), std::string::npos);
        });
    }
} // namespace
