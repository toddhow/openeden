#include <components/extension/load_error.hpp>
#include <components/extension/vfs_service.hpp>
#include <components/vfs/filesystemarchive.hpp>
#include <components/vfs/manager.hpp>
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
    using openeden::extension::LoadError;
    using openeden::extension::VfsServiceImpl;
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

    TEST(VfsServiceTest, MountExtensionDataDirectoryOverBaseVfs)
    {
        TempTestDirectory baseDir("base");
        TempTestDirectory extensionDir("extension_motion");

        baseDir.writeFile("base_asset.txt", "base_content");
        extensionDir.writeFile("motion_asset.txt", "motion_content");

        VfsServiceImpl vfs;
        vfs.mount("data", baseDir.path(), 0);
        vfs.mount("data", extensionDir.path(), 10);

        EXPECT_TRUE(vfs.hasFile("data/base_asset.txt"));
        EXPECT_TRUE(vfs.hasFile("data/motion_asset.txt"));

        const auto motionBytes = vfs.readFile("data/motion_asset.txt");
        const std::string motionStr(motionBytes.begin(), motionBytes.end());
        EXPECT_EQ(motionStr, "motion_content");
    }

    TEST(VfsServiceTest, LaterMountedExtensionOverlaysEarlierInLoadOrder)
    {
        TempTestDirectory baseDir("overlay_base");
        TempTestDirectory extensionADir("overlay_extension_a");
        TempTestDirectory extensionBDir("overlay_extension_b");

        baseDir.writeFile("shared.txt", "content_from_base");
        extensionADir.writeFile("shared.txt", "content_from_extension_a");
        extensionBDir.writeFile("shared.txt", "content_from_extension_b");

        VfsServiceImpl vfs;
        vfs.setExtensionLoadOrder({ "extension_a", "extension_b" });

        vfs.mount("data", baseDir.path(), 0);
        vfs.mountExtensionData("extension_a", extensionADir.path());
        vfs.mountExtensionData("extension_b", extensionBDir.path());

        // Invariant: later extension in load order overlays earlier files
        const auto sharedBytes = vfs.readFile("data/shared.txt");
        const std::string sharedStr(sharedBytes.begin(), sharedBytes.end());
        EXPECT_EQ(sharedStr, "content_from_extension_b");
    }

    TEST(VfsServiceTest, FallbackToEarlierMountWhenFileNotInLaterMount)
    {
        TempTestDirectory extensionADir("fallback_extension_a");
        TempTestDirectory extensionBDir("fallback_extension_b");

        extensionADir.writeFile("unique_to_a.txt", "alpha_data");

        VfsServiceImpl vfs;
        vfs.setExtensionLoadOrder({ "extension_a", "extension_b" });

        vfs.mountExtensionData("extension_a", extensionADir.path());
        vfs.mountExtensionData("extension_b", extensionBDir.path());

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

    // ---- Reading through the game's VFS (task 0016) -----------------------------------------------------------------

    using openeden::extension::DataOverride;
    using openeden::extension::ExtensionDataDir;
    using openeden::extension::findDataOverrides;

    // Built as OMW::Engine builds the game's: archives in priority order, then the index.
    std::unique_ptr<VFS::Manager> makeGameVfs(const std::vector<std::filesystem::path>& directories)
    {
        auto vfs = std::make_unique<VFS::Manager>();
        for (const std::filesystem::path& directory : directories)
            vfs->addArchive(std::make_unique<VFS::FileSystemArchive>(directory));
        vfs->buildIndex();
        return vfs;
    }

    std::string readText(const VfsServiceImpl& service, std::string_view path)
    {
        const std::vector<std::uint8_t> bytes = service.readFile(path);
        return std::string(bytes.begin(), bytes.end());
    }

    TEST(VfsServiceEngineTest, HasFileAndReadFileGoThroughTheGamesVfs)
    {
        TempTestDirectory dir("engine_read");
        dir.writeFile("meshes/x.txt", "game_content");
        const auto gameVfs = makeGameVfs({ dir.path() });

        VfsServiceImpl service;
        service.setEngineVfs(gameVfs.get());

        // The game's path, with no "data" prefix (header of setEngineVfs)
        EXPECT_TRUE(service.hasFile("meshes/x.txt"));
        EXPECT_FALSE(service.hasFile("meshes/y.txt"));
        EXPECT_EQ(readText(service, "meshes/x.txt"), "game_content");
    }

    TEST(VfsServiceEngineTest, BinaryFileIsReadByteForByte)
    {
        TempTestDirectory dir("engine_binary");
        const std::string content("a\0b\r\nc\xFF\0", 8);
        dir.writeFile("data.bin", content);
        const auto gameVfs = makeGameVfs({ dir.path() });

        VfsServiceImpl service;
        service.setEngineVfs(gameVfs.get());

        EXPECT_EQ(readText(service, "data.bin"), content);
    }

    TEST(VfsServiceEngineTest, ReadAssetDispatchesTheGamesFileToItsReader)
    {
        TempTestDirectory dir("engine_asset");
        dir.writeFile("anims/walk.animgraph", "VALID_ANIMGRAPH_DATA");
        const auto gameVfs = makeGameVfs({ dir.path() });

        VfsServiceImpl service;
        service.setEngineVfs(gameVfs.get());
        auto reader = std::make_shared<MockAnimGraphReader>();
        service.registerFormatReader(".animgraph", reader);

        FileError error;
        EXPECT_TRUE(service.readAsset("anims/walk.animgraph", &error));

        EXPECT_EQ(reader->mLastReadPath, "anims/walk.animgraph");
        EXPECT_EQ(std::string(reader->mLastReadData.begin(), reader->mLastReadData.end()), "VALID_ANIMGRAPH_DATA");
        EXPECT_TRUE(error.message.empty());
    }

    TEST(VfsServiceEngineTest, ReaderErrorKeepsItsLocationThroughTheGamesVfs)
    {
        TempTestDirectory dir("engine_asset_bad");
        dir.writeFile("anims/broken.animgraph", "INVALID");
        const auto gameVfs = makeGameVfs({ dir.path() });

        VfsServiceImpl service;
        service.setEngineVfs(gameVfs.get());
        service.registerFormatReader(".animgraph", std::make_shared<MockAnimGraphReader>());

        FileError error;
        EXPECT_FALSE(service.readAsset("anims/broken.animgraph", &error));
        EXPECT_EQ(error.file, "anims/broken.animgraph");
        EXPECT_EQ(error.line, 42u); // Set by MockAnimGraphReader
        EXPECT_FALSE(error.message.empty());
    }

    TEST(VfsServiceEngineTest, LaterArchiveWins)
    {
        // VFS::Manager's class comment: "the last added archive will have priority"
        TempTestDirectory earlier("engine_earlier");
        TempTestDirectory later("engine_later");
        earlier.writeFile("shared.txt", "from_earlier");
        earlier.writeFile("only_earlier.txt", "still_here");
        later.writeFile("shared.txt", "from_later");
        const auto gameVfs = makeGameVfs({ earlier.path(), later.path() });

        VfsServiceImpl service;
        service.setEngineVfs(gameVfs.get());

        EXPECT_EQ(readText(service, "shared.txt"), "from_later");
        EXPECT_EQ(readText(service, "only_earlier.txt"), "still_here");
    }

    TEST(VfsServiceEngineTest, PathsAreCaseInsensitiveAsInTheGamesVfs)
    {
        TempTestDirectory dir("engine_case");
        dir.writeFile("Meshes/Sub/MixedCase.TXT", "content");
        const auto gameVfs = makeGameVfs({ dir.path() });

        VfsServiceImpl service;
        service.setEngineVfs(gameVfs.get());

        EXPECT_TRUE(service.hasFile("meshes/sub/mixedcase.txt"));
        EXPECT_TRUE(service.hasFile("MESHES/SUB/MIXEDCASE.TXT"));
        EXPECT_TRUE(service.hasFile("Meshes\\Sub\\MixedCase.txt"));
        EXPECT_EQ(readText(service, "MESHES/sub/MixedCase.txt"), "content");
    }

    TEST(VfsServiceEngineTest, ReadAssetAcceptsAMixedCasePath)
    {
        TempTestDirectory dir("engine_asset_case");
        dir.writeFile("anims/walk.animgraph", "VALID");
        const auto gameVfs = makeGameVfs({ dir.path() });

        VfsServiceImpl service;
        service.setEngineVfs(gameVfs.get());
        service.registerFormatReader(".animgraph", std::make_shared<MockAnimGraphReader>());

        FileError error;
        EXPECT_TRUE(service.readAsset("Anims/WALK.animgraph", &error)) << error.message;
    }

    TEST(VfsServiceEngineTest, MissingFileGivesALocatedErrorWithoutThrowing)
    {
        TempTestDirectory dir("engine_missing");
        const auto gameVfs = makeGameVfs({ dir.path() });

        VfsServiceImpl service;
        service.setEngineVfs(gameVfs.get());
        service.registerFormatReader(".animgraph", std::make_shared<MockAnimGraphReader>());

        FileError error;
        bool result = true;
        EXPECT_NO_THROW(result = service.readAsset("anims/missing.animgraph", &error));
        EXPECT_FALSE(result);
        EXPECT_EQ(error.file, "anims/missing.animgraph");
        EXPECT_FALSE(error.message.empty());

        // VFS::Manager::get throws on a missing file; no exception crosses to an extension (extension doc 6.8)
        std::vector<std::uint8_t> bytes{ 1 };
        EXPECT_NO_THROW(bytes = service.readFile("anims/missing.animgraph"));
        EXPECT_TRUE(bytes.empty());
    }

    TEST(VfsServiceEngineTest, OwnMountsAreIgnoredWhileSetAndNullRestoresThem)
    {
        TempTestDirectory mounted("engine_mounted");
        TempTestDirectory game("engine_game");
        mounted.writeFile("mounted.txt", "from_mount");
        game.writeFile("game.txt", "from_game");
        const auto gameVfs = makeGameVfs({ game.path() });

        VfsServiceImpl service;
        service.mount("data", mounted.path(), 0);
        ASSERT_TRUE(service.hasFile("data/mounted.txt"));
        ASSERT_FALSE(service.hasFile("game.txt"));

        service.setEngineVfs(gameVfs.get());
        EXPECT_FALSE(service.hasFile("data/mounted.txt"));
        EXPECT_TRUE(service.hasFile("game.txt"));

        service.setEngineVfs(nullptr);
        EXPECT_TRUE(service.hasFile("data/mounted.txt"));
        EXPECT_FALSE(service.hasFile("game.txt"));
        EXPECT_EQ(readText(service, "data/mounted.txt"), "from_mount");
    }

    // ---- findDataOverrides (acceptance: "a game data file overridden by an extension is reported once")
    // -----------------

    // Flattened so that gtest prints a failure readably and the whole result, order included, can be compared.
    std::vector<std::string> flatten(const std::vector<DataOverride>& overrides)
    {
        std::vector<std::string> result;
        for (const DataOverride& entry : overrides)
            result.push_back(entry.path + "|" + entry.extensionId + "|" + entry.overriddenExtensionId);
        return result;
    }

    using Lines = std::vector<std::string>;

    TEST(FindDataOverridesTest, FileInGameDataIsReportedWithAnEmptyOverriddenExtension)
    {
        TempTestDirectory game("override_game");
        TempTestDirectory extension("override_extension");
        game.writeFile("meshes/a.nif", "game");
        extension.writeFile("meshes/a.nif", "extension");
        extension.writeFile("meshes/new.nif", "extension only");
        const auto gameVfs = makeGameVfs({ game.path() });

        const auto overrides = findDataOverrides(*gameVfs, { ExtensionDataDir{ "extension_a", extension.path() } });

        EXPECT_EQ(flatten(overrides), Lines{ "meshes/a.nif|extension_a|" });
    }

    TEST(FindDataOverridesTest, FileInTwoExtensionsIsReportedWithTheEarlierExtension)
    {
        TempTestDirectory game("override2_game");
        TempTestDirectory first("override2_first");
        TempTestDirectory second("override2_second");
        first.writeFile("textures/t.dds", "first");
        second.writeFile("textures/t.dds", "second");
        const auto gameVfs = makeGameVfs({ game.path() });

        const auto overrides = findDataOverrides(*gameVfs,
            { ExtensionDataDir{ "extension_a", first.path() }, ExtensionDataDir{ "extension_b", second.path() } });

        EXPECT_EQ(flatten(overrides), Lines{ "textures/t.dds|extension_b|extension_a" });
    }

    TEST(FindDataOverridesTest, EachExtensionReportsWhatItDirectlyHides)
    {
        // A file in the game and in three extensions: "once per extension that hides it", and each hides what was on
        // top
        TempTestDirectory game("override3_game");
        TempTestDirectory a("override3_a");
        TempTestDirectory b("override3_b");
        TempTestDirectory c("override3_c");
        game.writeFile("x.txt", "game");
        a.writeFile("x.txt", "a");
        b.writeFile("x.txt", "b");
        c.writeFile("x.txt", "c");
        const auto gameVfs = makeGameVfs({ game.path() });

        const auto overrides = findDataOverrides(*gameVfs,
            { ExtensionDataDir{ "a", a.path() }, ExtensionDataDir{ "b", b.path() },
                ExtensionDataDir{ "c", c.path() } });

        EXPECT_EQ(flatten(overrides), (Lines{ "x.txt|a|", "x.txt|b|a", "x.txt|c|b" }));
    }

    TEST(FindDataOverridesTest, NoOverlapGivesNothing)
    {
        TempTestDirectory game("override4_game");
        TempTestDirectory first("override4_first");
        TempTestDirectory second("override4_second");
        game.writeFile("game.txt", "game");
        first.writeFile("first.txt", "first");
        // The same file name in another folder is another VFS path
        second.writeFile("sub/first.txt", "second");
        const auto gameVfs = makeGameVfs({ game.path() });

        const auto overrides = findDataOverrides(
            *gameVfs, { ExtensionDataDir{ "a", first.path() }, ExtensionDataDir{ "b", second.path() } });

        EXPECT_TRUE(overrides.empty());
    }

    TEST(FindDataOverridesTest, NoExtensionsGiveNothing)
    {
        TempTestDirectory game("override5_game");
        game.writeFile("game.txt", "game");
        const auto gameVfs = makeGameVfs({ game.path() });

        EXPECT_TRUE(findDataOverrides(*gameVfs, {}).empty());
    }

    TEST(FindDataOverridesTest, MissingDirectoryHasNoFiles)
    {
        TempTestDirectory game("override6_game");
        TempTestDirectory real("override6_real");
        TempTestDirectory later("override6_later");
        game.writeFile("x.txt", "game");
        real.writeFile("x.txt", "real");
        later.writeFile("x.txt", "later");
        const auto gameVfs = makeGameVfs({ game.path() });
        const std::filesystem::path missing = game.path() / "no_such_folder";

        std::vector<DataOverride> alone;
        EXPECT_NO_THROW(alone = findDataOverrides(*gameVfs, { ExtensionDataDir{ "missing", missing } }));
        EXPECT_TRUE(alone.empty());

        // It does not stop the others, and it hides nothing between them
        const auto overrides = findDataOverrides(*gameVfs,
            { ExtensionDataDir{ "a", real.path() }, ExtensionDataDir{ "missing", missing },
                ExtensionDataDir{ "b", later.path() } });
        EXPECT_EQ(flatten(overrides), (Lines{ "x.txt|a|", "x.txt|b|a" }));
    }

    TEST(FindDataOverridesTest, ResultIsInLoadOrderAndSortedWithinAExtensionAndStable)
    {
        TempTestDirectory game("override7_game");
        TempTestDirectory first("override7_first");
        TempTestDirectory second("override7_second");
        for (const char* path : { "z.txt", "m/n.txt", "a.txt" })
        {
            game.writeFile(path, "game");
            first.writeFile(path, "first");
        }
        // Written out of order, and the extension that comes later in load order sorts first by ID
        second.writeFile("c.txt", "second");
        game.writeFile("c.txt", "game");
        second.writeFile("b.txt", "second");
        game.writeFile("b.txt", "game");
        const auto gameVfs = makeGameVfs({ game.path() });
        const std::vector<ExtensionDataDir> dirs{ ExtensionDataDir{ "zz_first", first.path() },
            ExtensionDataDir{ "aa_second", second.path() } };

        const Lines expected{ "a.txt|zz_first|", "m/n.txt|zz_first|", "z.txt|zz_first|", "b.txt|aa_second|",
            "c.txt|aa_second|" };
        EXPECT_EQ(flatten(findDataOverrides(*gameVfs, dirs)), expected);
        EXPECT_EQ(flatten(findDataOverrides(*gameVfs, dirs)), expected);
    }

    TEST(FindDataOverridesTest, NestedFoldersAndMixedCaseAreNormalized)
    {
        TempTestDirectory game("override8_game");
        TempTestDirectory first("override8_first");
        TempTestDirectory second("override8_second");
        game.writeFile("meshes/sub/foo.nif", "game");
        first.writeFile("Meshes/SUB/Foo.NIF", "first");
        first.writeFile("Textures/Deep/Er/Bar.DDS", "first");
        second.writeFile("textures/deep/ER/bar.dds", "second");
        const auto gameVfs = makeGameVfs({ game.path() });

        const auto overrides = findDataOverrides(
            *gameVfs, { ExtensionDataDir{ "a", first.path() }, ExtensionDataDir{ "b", second.path() } });

        EXPECT_EQ(flatten(overrides), (Lines{ "meshes/sub/foo.nif|a|", "textures/deep/er/bar.dds|b|a" }));
    }
} // namespace
