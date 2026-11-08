// Task 0086 stage b: the Skyrim SE game pack's core logic and its life in the host. Generated plugins only.
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/skyrimse_game_pack.hpp>

#include <components/extension/extension_loader.hpp>
#include <components/extension/host_impl.hpp>
#include <components/extension/lifecycle_manager.hpp>
#include <components/extension/manifest.hpp>
#include <components/extension/manifest_io.hpp>

#include <openeden/game/content.hpp>
#include <openeden/game/records.hpp>
#include <openeden/pack_kind.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    Content::Header headerOf(const Bytes& bytes)
    {
        Content::Error error;
        auto plugin = Content::Plugin::open(
            std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
        EXPECT_NE(plugin, nullptr) << error.describe();
        return plugin ? plugin->header() : Content::Header{};
    }

    Bytes skyrimPlugin(const Bytes& body = {})
    {
        Bytes bytes = tes4Header(1.7f, 0, "author", "", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        bytes.append(body);
        return bytes;
    }

    TEST(SkyrimSePack, aSkyrimHeaderIsRecognisedAndOthersAreNot)
    {
        EXPECT_TRUE(isSkyrimSeHeader(headerOf(skyrimPlugin())));
        EXPECT_TRUE(isSkyrimSeHeader(headerOf(tes4Header(1.71f, 0, "", "", 0, {}, 24, 43))));
        EXPECT_FALSE(isSkyrimSeHeader(headerOf(tes3Header(1.3f, 0, "", "", 0, {}))));
        EXPECT_FALSE(isSkyrimSeHeader(headerOf(tes4Header(0.95f, 0, "", "", 0, {}, 24, 131)))); // Fallout 4
        EXPECT_FALSE(isSkyrimSeHeader(headerOf(tes4Header(0.8f, 0, "", "", 0, {}, 20)))); // Oblivion
    }

    TEST(SkyrimSePack, theContentProbeAcceptsOnlySkyrimHeaders)
    {
        const SkyrimContentLoader loader;
        EXPECT_TRUE(loader.probe(toContentHeader(headerOf(skyrimPlugin()))));
        EXPECT_EQ(toContentHeader(headerOf(skyrimPlugin())).gameTag, kContentTag);
        EXPECT_TRUE(loader.probe({ "TES4", 44 }));
        EXPECT_TRUE(loader.probe({ "TES4", 43 }));
        EXPECT_FALSE(loader.probe({ "TES4", 131 }));
        EXPECT_FALSE(loader.probe({ "TES3", 44 }));
        EXPECT_FALSE(loader.probe(toContentHeader(headerOf(tes3Header(1.3f, 0, "", "", 0, {})))));
    }

    TEST(SkyrimSePack, theRecordTypeTableIsSortedAndKnowsTheCommonTypes)
    {
        const auto types = recognisedRecordTypes();
        EXPECT_GT(types.size(), 100u);
        EXPECT_TRUE(std::is_sorted(types.begin(), types.end()));
        EXPECT_TRUE(std::adjacent_find(types.begin(), types.end()) == types.end());
        for (const char* name : { "NPC_", "WEAP", "ARMO", "CELL", "QUST", "RACE", "WRLD", "REFR", "LAND", "INFO" })
        {
            std::uint32_t id = 0;
            std::memcpy(&id, name, 4);
            EXPECT_TRUE(isRecognisedRecordType(id)) << name;
        }
        EXPECT_FALSE(isRecognisedRecordType(Content::fourCC("ZZZZ")));
        EXPECT_FALSE(isRecognisedRecordType(Content::fourCC("TES3")));
        EXPECT_FALSE(isRecognisedRecordType(0));
    }

    TEST(SkyrimSePack, theCensusCountsRecognisedUnknownCompressedAndGroups)
    {
        Bytes npcs;
        npcs.append(record4("NPC_", 0, 0x100, sub4("EDID", Bytes().zstring("A"))));
        npcs.append(record4("NPC_", Content::sRecCompressed, 0x101, Bytes().u32(0).u8(0)));
        const Bytes top = group4(Content::fourCC("NPC_"), 0, npcs);
        Bytes weird;
        weird.append(record4("ZZZZ", 0, 0x102, Bytes()));
        const Bytes bytes = skyrimPlugin(Bytes().append(top).append(group4(Content::fourCC("ZZZZ"), 0, weird)));

        RecordCensus census;
        Content::Header header;
        Content::Error error;
        ASSERT_TRUE(scanPluginBytes(bytes.str(), "test.esp", census, header, error)) << error.describe();
        EXPECT_EQ(census.mRecords, 3u);
        EXPECT_EQ(census.mRecognised, 2u);
        EXPECT_EQ(census.mUnknown, 1u);
        EXPECT_EQ(census.mSkipped, 2u);
        EXPECT_EQ(census.mCompressed, 1u);
        EXPECT_EQ(census.mGroups, 2u);
        EXPECT_EQ(census.mByType.at(Content::fourCC("NPC_")), 2u);
        EXPECT_EQ(census.mUnknownByType.at(Content::fourCC("ZZZZ")), 1u);
        EXPECT_EQ(header.mMasters.size(), 1u);
    }

    TEST(SkyrimSePack, aPluginOfAnotherGameIsRefusedWithAMessageThatNamesTheFile)
    {
        RecordCensus census;
        Content::Header header;
        Content::Error error;
        EXPECT_FALSE(scanPluginBytes(tes3Header(1.3f, 0, "", "", 0, {}).str(), "morrowind.esm", census, header, error));
        EXPECT_EQ(error.mFile, "morrowind.esm");
        EXPECT_NE(error.describe().find("Skyrim"), std::string::npos) << error.describe();
    }

    TEST(SkyrimSePack, aMissingFileGivesAnError)
    {
        RecordCensus census;
        Content::Header header;
        Content::Error error;
        EXPECT_FALSE(scanPlugin("definitely/not/here.esm", census, header, error));
        EXPECT_FALSE(error.mMessage.empty());
    }

    // ---- In the host ----

    using openeden::extension::ExtensionManifest;
    using openeden::extension::HostImpl;
    using openeden::extension::LifecycleManager;

    ExtensionManifest packManifest()
    {
        ExtensionManifest m;
        m.version = 2;
        m.id = "skyrimse";
        m.name = "Skyrim SE";
        m.extensionVersion = "1.0.0";
        m.kind = "game";
        m.sdkVersion = openeden::sdk::kSdkVersion;
        m.toolchain = "test-toolchain";
        return m;
    }

    TEST(SkyrimSeGamePack, theNullRulesRefuseTheSessionWithTheMessageAndRefuseSaving)
    {
        HostImpl host;
        LifecycleManager manager(host);
        manager.addExtension(packManifest(), createSkyrimGamePack());
        EXPECT_NO_THROW(EXPECT_TRUE(manager.initialize()));
        EXPECT_TRUE(manager.hasSessionError());
        EXPECT_NE(manager.getSessionError().find(std::string(kRulesNotImplementedMessage)), std::string::npos)
            << manager.getSessionError();
        EXPECT_FALSE(manager.isSavingAllowed());
        EXPECT_EQ(manager.getGamePackId(), "skyrimse");
        EXPECT_NO_THROW(manager.runPhase(openeden::sdk::Phase::GameSimulation, 0.016f));
        EXPECT_NO_THROW(manager.shutdown());
    }

    TEST(SkyrimSeGamePack, theContentProbeIsRegisteredWithTheHostAndNoRecordTypeIsStored)
    {
        HostImpl host;
        LifecycleManager manager(host);
        manager.addExtension(packManifest(), createSkyrimGamePack());
        manager.initialize();
        EXPECT_NE(host.content().findLoader({ "TES4", 44 }), nullptr);
        EXPECT_EQ(host.content().findLoader({ "TES3", 0 }), nullptr);
        EXPECT_EQ(host.content().findLoader({ "TES4", 131 }), nullptr);
        EXPECT_EQ(host.records().findRecordType(openeden::sdk::makeRecordTypeId('N', 'P', 'C', '_')), nullptr);
    }

    TEST(SkyrimSeGamePack, theManifestIsAGamePack)
    {
        openeden::extension::LoadError error;
        const auto manifest = openeden::extension::loadManifestFile(
            std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "game_packs" / "skyrimse" / "extension.json", error);
        ASSERT_TRUE(manifest.has_value()) << error.message;
        EXPECT_EQ(manifest->id, "skyrimse");
        EXPECT_EQ(manifest->kind, "game");
        EXPECT_EQ(manifest->version, 2);
        EXPECT_TRUE(manifest->sdkVersion == openeden::sdk::kSdkVersion);
    }

    // With game=morrowind nothing of Skyrim's is loaded: discovery reads two small manifests and opens no library.
    TEST(SkyrimSeGamePack, discoveryReadsTheManifestsAndLoadsNoLibrary)
    {
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / "openeden_skyrimse_discovery";
        std::error_code ignored;
        fs::remove_all(root, ignored);
        const fs::path source{ OPENMW_PROJECT_SOURCE_DIR };
        fs::create_directories(root / "game_packs" / "skyrimse");
        fs::create_directories(root / "extensions" / "compat_skyrimse");
        fs::copy_file(
            source / "game_packs" / "skyrimse" / "extension.json", root / "game_packs" / "skyrimse" / "extension.json");
        fs::copy_file(source / "extensions" / "compat_skyrimse" / "extension.json",
            root / "extensions" / "compat_skyrimse" / "extension.json");

        std::vector<openeden::extension::ExtensionLoadError> errors;
        const auto start = std::chrono::steady_clock::now();
        const auto found
            = openeden::extension::discoverExtensionDirectories({ root / "game_packs", root / "extensions" }, &errors);
        const auto micros
            = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);
        ASSERT_EQ(found.size(), 2u);
        EXPECT_TRUE(errors.empty());
        EXPECT_EQ(found[0].manifest.kind, "game");
        EXPECT_EQ(found[1].manifest.kind, "compatibility");
        std::cout << "SKYRIMSE-CHECK discovery of both manifests: " << micros.count() << " microseconds\n";
        fs::remove_all(root, ignored);
    }

    // The built library, loaded the way the game loads it (set OPENEDEN_SKYRIMSE_PACK_DLL to the file to run it).
    TEST(SkyrimSeGamePack, theBuiltLibraryLoadsThroughTheLoaderAndRefusesTheSession)
    {
        namespace fs = std::filesystem;
        const char* const library = std::getenv("OPENEDEN_SKYRIMSE_PACK_DLL");
        if (library == nullptr || !fs::exists(library))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_PACK_DLL is not set";
        const fs::path root = fs::temp_directory_path() / "openeden_skyrimse_dll";
        std::error_code ignored;
        fs::remove_all(root, ignored);
        fs::create_directories(root / "game_packs" / "skyrimse");
        const fs::path source{ OPENMW_PROJECT_SOURCE_DIR };
        fs::copy_file(
            source / "game_packs" / "skyrimse" / "extension.json", root / "game_packs" / "skyrimse" / "extension.json");
        fs::copy_file(library, root / "game_packs" / "skyrimse" / fs::path(library).filename());
        // the manifest names skyrimse.dll / libskyrimse.so
        HostImpl host;
        const auto found = openeden::extension::discoverExtensionDirectories({ root / "game_packs" }, nullptr);
        ASSERT_EQ(found.size(), 1u);
        auto loaded = openeden::extension::loadExtensionLibrary(found[0], host.getHostInfo());
        ASSERT_TRUE(loaded.success) << loaded.error.message;
        LifecycleManager manager(host);
        manager.addExtension(std::move(loaded.extension.manifest), std::move(loaded.extension.instance),
            std::move(loaded.extension.library));
        manager.initialize();
        EXPECT_TRUE(manager.hasSessionError());
        EXPECT_NE(manager.getSessionError().find(std::string(kRulesNotImplementedMessage)), std::string::npos)
            << manager.getSessionError();
        EXPECT_NE(host.content().findLoader({ "TES4", 44 }), nullptr);
        manager.shutdown();
        fs::remove_all(root, ignored);
    }
}
