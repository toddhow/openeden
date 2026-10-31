#include <components/plugin/dynamic_library.hpp>
#include <components/plugin/lifecycle_manager.hpp>
#include <components/plugin/load_error.hpp>
#include <components/plugin/manifest.hpp>
#include <components/plugin/manifest_io.hpp>
#include <components/plugin/plugin_loader.hpp>
#include <openeden/host.hpp>
#include <openeden/plugin.hpp>
#include <openeden/registrar.hpp>
#include <openeden/service_locator.hpp>
#include <openeden/version.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using openeden::plugin::DiscoveredPlugin;
    using openeden::plugin::discoverPluginDirectories;
    using openeden::plugin::DynamicLibrary;
    using openeden::plugin::filterEnabledPlugins;
    using openeden::plugin::getHostToolchain;
    using openeden::plugin::LifecycleManager;
    using openeden::plugin::LoadedPlugin;
    using openeden::plugin::loadPluginLibrary;
    using openeden::plugin::PluginLoadError;
    using openeden::plugin::PluginLoadResult;
    using openeden::plugin::PluginManifest;
    using openeden::plugin::validatePluginHandshake;
    using openeden::sdk::Host;
    using openeden::sdk::HostInfo;
    using openeden::sdk::kSdkVersion;
    using openeden::sdk::Plugin;
    using openeden::sdk::PluginDescriptor;
    using openeden::sdk::Registrar;
    using openeden::sdk::ServiceLocator;
    using openeden::sdk::Version;

    class MockHost : public Host
    {
    public:
        explicit MockHost(Version version = kSdkVersion)
            : mInfo{ version, "MockTestHost" }
        {
        }

        const HostInfo& getHostInfo() const override { return mInfo; }

    private:
        HostInfo mInfo;
    };

    class TempDirectoryGuard
    {
    public:
        TempDirectoryGuard()
        {
            const auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
            mPath = std::filesystem::temp_directory_path() / ("openeden_loader_test_" + std::to_string(timestamp));
            std::filesystem::create_directories(mPath);
        }

        ~TempDirectoryGuard()
        {
            std::error_code ec;
            std::filesystem::remove_all(mPath, ec);
        }

        const std::filesystem::path& path() const { return mPath; }

    private:
        std::filesystem::path mPath;
    };

    void writeMockManifest(const std::filesystem::path& dir, const std::string& id)
    {
        std::filesystem::create_directories(dir);
        const std::string content = "{\n"
                                    "  \"format\": \"openeden.plugin\",\n"
                                    "  \"version\": 1,\n"
                                    "  \"id\": \"" + id + "\",\n"
                                    "  \"name\": \"" + id + "\",\n"
                                    "  \"pluginVersion\": \"1.0.0\",\n"
                                    "  \"kind\": \"feature\",\n"
                                    "  \"sdk\": {\"major\": 2, \"minor\": 0},\n"
                                    "  \"toolchain\": \"" + getHostToolchain() + "\"\n"
                                    "}";

        std::ofstream file(dir / "plugin.json");
        file << content;
    }

    struct LifecycleTracker
    {
        std::vector<std::string> events;
    };

    class TrackedMockPlugin : public Plugin
    {
    public:
        explicit TrackedMockPlugin(LifecycleTracker& tracker, std::string id)
            : mTracker(tracker)
            , mId(std::move(id))
        {
        }

        ~TrackedMockPlugin() override { mTracker.events.push_back(mId + ":destructed"); }

        void registerServices(Registrar&) override { mTracker.events.push_back(mId + ":registered"); }

        bool resolve(const ServiceLocator&) override
        {
            mTracker.events.push_back(mId + ":resolved");
            return true;
        }

        void start(Host&) override { mTracker.events.push_back(mId + ":started"); }

        void stop() override { mTracker.events.push_back(mId + ":stopped"); }

    private:
        LifecycleTracker& mTracker;
        std::string mId;
    };

    std::unique_ptr<Plugin> createSamplePlugin()
    {
        static LifecycleTracker dummyTracker;
        return std::make_unique<TrackedMockPlugin>(dummyTracker, "sample");
    }
} // namespace

TEST(PluginLoaderTest, DiscoverPluginDirectories_FindsValidPluginsAndIgnoresInvalidOrNonPluginFolders)
{
    TempDirectoryGuard tempDir;
    const std::filesystem::path searchDir1 = tempDir.path() / "search1";
    const std::filesystem::path searchDir2 = tempDir.path() / "search2";

    // Setup searchDir1
    writeMockManifest(searchDir1 / "plugin_alpha", "plugin_alpha");
    writeMockManifest(searchDir1 / "plugin_beta", "plugin_beta");

    // Non-plugin directory (no plugin.json)
    const std::filesystem::path notAPluginDir = searchDir1 / "not_a_plugin";
    std::filesystem::create_directories(notAPluginDir);
    {
        std::ofstream textFile(notAPluginDir / "readme.txt");
        textFile << "Not a plugin manifest";
    }

    // Empty folder
    std::filesystem::create_directories(searchDir1 / "empty_dir");

    // Regular file in search directory root
    {
        std::ofstream rootFile(searchDir1 / "some_file.txt");
        rootFile << "Just a file";
    }

    // Setup searchDir2
    writeMockManifest(searchDir2 / "plugin_gamma", "plugin_gamma");

    // Broken manifest folder
    const std::filesystem::path brokenManifestDir = searchDir2 / "broken_plugin";
    std::filesystem::create_directories(brokenManifestDir);
    {
        std::ofstream brokenFile(brokenManifestDir / "plugin.json");
        brokenFile << "{ malformed json syntax";
    }

    // Discover across both directories
    const auto discovered = discoverPluginDirectories({ searchDir1, searchDir2 });

    ASSERT_EQ(discovered.size(), 3u);

    // Verify discovered IDs
    std::vector<std::string> discoveredIds;
    for (const auto& item : discovered)
        discoveredIds.push_back(item.manifest.id);

    EXPECT_EQ(discoveredIds[0], "plugin_alpha");
    EXPECT_EQ(discoveredIds[1], "plugin_beta");
    EXPECT_EQ(discoveredIds[2], "plugin_gamma");

    // Verify paths are correctly recorded
    EXPECT_EQ(discovered[0].directory, searchDir1 / "plugin_alpha");
    EXPECT_EQ(discovered[0].manifestPath, searchDir1 / "plugin_alpha" / "plugin.json");
}

TEST(PluginLoaderTest, FilterEnabledPlugins_FiltersSubsetAndPreservesDiscoveryOrder)
{
    DiscoveredPlugin pluginA;
    pluginA.manifest.id = "pluginA";

    DiscoveredPlugin pluginB;
    pluginB.manifest.id = "pluginB";

    DiscoveredPlugin pluginC;
    pluginC.manifest.id = "pluginC";

    DiscoveredPlugin pluginD;
    pluginD.manifest.id = "pluginD";

    const std::vector<DiscoveredPlugin> discovered{ pluginA, pluginB, pluginC, pluginD };
    const std::vector<std::string> enabledList{ "pluginC", "pluginA" };

    const auto filtered = filterEnabledPlugins(discovered, enabledList);

    ASSERT_EQ(filtered.size(), 2u);
    // Preserves original discovery order
    EXPECT_EQ(filtered[0].manifest.id, "pluginA");
    EXPECT_EQ(filtered[1].manifest.id, "pluginC");
}

TEST(PluginLoaderTest, FilterEnabledPlugins_EmptyEnabledListDefaultsToAllEnabled)
{
    DiscoveredPlugin pluginA;
    pluginA.manifest.id = "pluginA";

    DiscoveredPlugin pluginB;
    pluginB.manifest.id = "pluginB";

    const std::vector<DiscoveredPlugin> discovered{ pluginA, pluginB };
    const std::vector<std::string> emptyEnabledList{};

    const auto filtered = filterEnabledPlugins(discovered, emptyEnabledList);

    ASSERT_EQ(filtered.size(), 2u);
    EXPECT_EQ(filtered[0].manifest.id, "pluginA");
    EXPECT_EQ(filtered[1].manifest.id, "pluginB");
}

TEST(PluginLoaderTest, FilterEnabledPlugins_IgnoresUnknownPluginsInEnableList)
{
    DiscoveredPlugin pluginA;
    pluginA.manifest.id = "pluginA";

    const std::vector<DiscoveredPlugin> discovered{ pluginA };
    const std::vector<std::string> enabledList{ "pluginA", "non_existent_plugin_xyz" };

    const auto filtered = filterEnabledPlugins(discovered, enabledList);

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].manifest.id, "pluginA");
}

TEST(PluginLoaderTest, LoadPluginLibrary_SdkVersionMismatch_RejectedBeforeLibraryLoad)
{
    MockHost host;

    DiscoveredPlugin discovered;
    discovered.manifest.id = "old_plugin";
    discovered.manifest.sdkVersion = Version{ 1, 0 }; // Built before SDK 2.0 removed API
    discovered.manifest.toolchain = getHostToolchain();

    const PluginLoadResult result = loadPluginLibrary(discovered, host.getHostInfo());

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.pluginId, "old_plugin");
    EXPECT_EQ(result.error.stage, "sdk");
    EXPECT_NE(result.error.message.find("built for SDK 1.0; this host provides 2.0"), std::string::npos)
        << result.error.message;
}

TEST(PluginLoaderTest, LoadPluginLibrary_Sdk1xMessageNamesTheRealMinor)
{
    MockHost host;

    DiscoveredPlugin discovered;
    discovered.manifest.id = "old_plugin";
    discovered.manifest.sdkVersion = Version{ 1, 7 };
    discovered.manifest.toolchain = getHostToolchain();

    const PluginLoadResult result = loadPluginLibrary(discovered, host.getHostInfo());

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.stage, "sdk");
    EXPECT_NE(result.error.message.find("built for SDK 1.7; this host provides 2.0"), std::string::npos)
        << result.error.message;
}

TEST(PluginLoaderTest, LoadPluginLibrary_NewerSdkMajor_RejectedBeforeLibraryLoad)
{
    MockHost host;

    DiscoveredPlugin discovered;
    discovered.manifest.id = "future_plugin";
    discovered.manifest.sdkVersion = Version{ 3, 0 };
    discovered.manifest.toolchain = getHostToolchain();

    const PluginLoadResult result = loadPluginLibrary(discovered, host.getHostInfo());

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.pluginId, "future_plugin");
    EXPECT_EQ(result.error.stage, "sdk");
    EXPECT_NE(result.error.message.find("SDK"), std::string::npos);
}

TEST(PluginLoaderTest, LoadPluginLibrary_ToolchainMismatch_RejectedBeforeLibraryLoad)
{
    MockHost host;

    DiscoveredPlugin discovered;
    discovered.manifest.id = "foreign_toolchain_plugin";
    discovered.manifest.sdkVersion = kSdkVersion;
    discovered.manifest.toolchain = "gcc-unknown-toolchain-xyz"; // Incompatible toolchain

    const PluginLoadResult result = loadPluginLibrary(discovered, host.getHostInfo());

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.pluginId, "foreign_toolchain_plugin");
    EXPECT_FALSE(result.error.message.empty());
    EXPECT_NE(result.error.message.find("toolchain"), std::string::npos);
}

TEST(PluginLoaderTest, LoadPluginLibrary_MissingLibraryFile_RejectedWithError)
{
    MockHost host;
    TempDirectoryGuard tempDir;

    DiscoveredPlugin discovered;
    discovered.directory = tempDir.path();
    discovered.manifest.id = "missing_lib_plugin";
    discovered.manifest.sdkVersion = kSdkVersion;
    discovered.manifest.toolchain = getHostToolchain();
    discovered.manifest.library.windows = "missing_plugin.dll";
    discovered.manifest.library.linux = "libmissing_plugin.so";

    const PluginLoadResult result = loadPluginLibrary(discovered, host.getHostInfo());

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.pluginId, "missing_lib_plugin");
    EXPECT_FALSE(result.error.message.empty());
}

TEST(PluginLoaderTest, HandshakeValidation_NullDescriptor_Rejected)
{
    MockHost host;

    PluginManifest manifest;
    manifest.id = "pluginA";
    manifest.sdkVersion = Version{ 1, 0 };

    PluginLoadError error;
    const bool valid = validatePluginHandshake(nullptr, manifest, host.getHostInfo(), error);

    EXPECT_FALSE(valid);
    EXPECT_EQ(error.pluginId, "pluginA");
    EXPECT_EQ(error.stage, "handshake");
    EXPECT_FALSE(error.message.empty());
    EXPECT_NE(error.message.find("null"), std::string::npos);
}

TEST(PluginLoaderTest, HandshakeValidation_IdMismatch_Rejected)
{
    MockHost host;

    PluginManifest manifest;
    manifest.id = "pluginA";
    manifest.sdkVersion = Version{ 1, 0 };

    const PluginDescriptor descriptor{ kSdkVersion, "wrong_plugin_id", &createSamplePlugin };

    PluginLoadError error;
    const bool valid = validatePluginHandshake(&descriptor, manifest, host.getHostInfo(), error);

    EXPECT_FALSE(valid);
    EXPECT_EQ(error.pluginId, "pluginA");
    EXPECT_EQ(error.stage, "handshake");
    EXPECT_FALSE(error.message.empty());
    EXPECT_NE(error.message.find("id"), std::string::npos);
}

TEST(PluginLoaderTest, HandshakeValidation_SdkVersionMismatch_Rejected)
{
    MockHost host;

    PluginManifest manifest;
    manifest.id = "pluginA";
    manifest.sdkVersion = Version{ 1, 0 };

    const PluginDescriptor descriptor{ Version{ 2, 2 }, // Mismatch with manifest's 2.0
        "pluginA", &createSamplePlugin };

    PluginLoadError error;
    const bool valid = validatePluginHandshake(&descriptor, manifest, host.getHostInfo(), error);

    EXPECT_FALSE(valid);
    EXPECT_EQ(error.pluginId, "pluginA");
    EXPECT_EQ(error.stage, "handshake");
    EXPECT_FALSE(error.message.empty());
    EXPECT_NE(error.message.find("SDK"), std::string::npos);
}

TEST(PluginLoaderTest, HandshakeValidation_NullCreateFunction_Rejected)
{
    MockHost host;

    PluginManifest manifest;
    manifest.id = "pluginA";
    manifest.sdkVersion = Version{ 1, 0 };

    const PluginDescriptor descriptor{
        kSdkVersion, "pluginA",
        nullptr // Null create pointer
    };

    PluginLoadError error;
    const bool valid = validatePluginHandshake(&descriptor, manifest, host.getHostInfo(), error);

    EXPECT_FALSE(valid);
    EXPECT_EQ(error.pluginId, "pluginA");
    EXPECT_EQ(error.stage, "handshake");
    EXPECT_FALSE(error.message.empty());
}

TEST(PluginLoaderTest, HandshakeValidation_MatchingDescriptor_Succeeds)
{
    MockHost host;

    PluginManifest manifest;
    manifest.id = "pluginA";
    manifest.sdkVersion = kSdkVersion;

    const PluginDescriptor descriptor{ kSdkVersion, "pluginA", &createSamplePlugin };

    PluginLoadError error;
    const bool valid = validatePluginHandshake(&descriptor, manifest, host.getHostInfo(), error);

    EXPECT_TRUE(valid);
    EXPECT_TRUE(error.message.empty());
}

TEST(PluginLoaderTest, HandshakeValidation_LibraryBuiltForSdk1_Rejected)
{
    MockHost host;

    // The manifest claims 2.0, but the library itself says it was built for 1.0
    PluginManifest manifest;
    manifest.id = "pluginA";
    manifest.sdkVersion = kSdkVersion;

    const PluginDescriptor descriptor{ Version{ 1, 0 }, "pluginA", &createSamplePlugin };

    PluginLoadError error;
    const bool valid = validatePluginHandshake(&descriptor, manifest, host.getHostInfo(), error);

    EXPECT_FALSE(valid);
    EXPECT_EQ(error.pluginId, "pluginA");
    EXPECT_EQ(error.stage, "handshake");
    EXPECT_NE(error.message.find("SDK"), std::string::npos);
}

TEST(PluginLoaderTest, HandshakeValidation_ManifestAndLibraryBothSdk1_Rejected)
{
    MockHost host;

    // They agree with each other but not with the host, which validatePluginHandshake is also given
    PluginManifest manifest;
    manifest.id = "pluginA";
    manifest.sdkVersion = Version{ 1, 0 };

    const PluginDescriptor descriptor{ Version{ 1, 0 }, "pluginA", &createSamplePlugin };

    PluginLoadError error;
    const bool valid = validatePluginHandshake(&descriptor, manifest, host.getHostInfo(), error);

    EXPECT_FALSE(valid);
    EXPECT_EQ(error.pluginId, "pluginA");
    EXPECT_NE(error.message.find("SDK"), std::string::npos);
}

TEST(PluginLoaderTest, LifecycleManager_DynamicPluginDestructedBeforeLibraryUnload)
{
    MockHost host;
    LifecycleTracker tracker;

    PluginManifest manifest;
    manifest.id = "dynamic_plugin";
    manifest.sdkVersion = kSdkVersion;
    manifest.toolchain = getHostToolchain();

    auto pluginInstance = std::make_unique<TrackedMockPlugin>(tracker, "dynamic_plugin");

    // Dynamic library handle simulation with tracking
    class TrackedDynamicLibrary : public DynamicLibrary
    {
    public:
        explicit TrackedDynamicLibrary(LifecycleTracker& tracker)
            : mTracker(tracker)
        {
        }

        ~TrackedDynamicLibrary() { mTracker.events.push_back("library:unloaded"); }

    private:
        LifecycleTracker& mTracker;
    };

    auto trackedLib = std::make_unique<TrackedDynamicLibrary>(tracker);

    {
        LifecycleManager manager(host);
        manager.addPlugin(manifest, std::move(pluginInstance), std::move(trackedLib));

        ASSERT_TRUE(manager.initialize());
        EXPECT_TRUE(manager.isPluginActive("dynamic_plugin"));

        manager.shutdown();
    }

    // Invariant: stop() -> ~Plugin() -> library unload
    const std::vector<std::string> expectedOrder{ "dynamic_plugin:registered", "dynamic_plugin:resolved",
        "dynamic_plugin:started", "dynamic_plugin:stopped", "dynamic_plugin:destructed", "library:unloaded" };

    EXPECT_EQ(tracker.events, expectedOrder);
}

// ---- listInstalledPlugins (task 0018): what a launcher shows -------------------------------------------------------

namespace
{
    using openeden::plugin::InstalledPlugin;
    using openeden::plugin::listInstalledPlugins;

    struct InstalledManifest
    {
        std::string mId;
        int mSdkMajor = 2;
        std::string mName = "A name";
        std::string mLicence = "GPL-3.0-or-later";
    };

    // The library named here never exists: a listing must not care.
    void writeInstalledManifest(const std::filesystem::path& dir, const InstalledManifest& options)
    {
        std::filesystem::create_directories(dir);
        std::ofstream file(dir / "plugin.json");
        file << "{\n"
             << "  \"format\": \"openeden.plugin\",\n"
             << "  \"version\": 1,\n"
             << "  \"id\": \"" << options.mId << "\",\n"
             << "  \"name\": \"" << options.mName << "\",\n"
             << "  \"pluginVersion\": \"1.2.3\",\n"
             << "  \"kind\": \"feature\",\n"
             << "  \"sdk\": { \"major\": " << options.mSdkMajor << ", \"minor\": 0 },\n"
             << "  \"toolchain\": \"\",\n"
             << "  \"library\": { \"windows\": \"does_not_exist.dll\", \"linux\": \"libdoes_not_exist.so\" },\n"
             << "  \"licence\": \"" << options.mLicence << "\"\n"
             << "}\n";
    }

    void writeText(const std::filesystem::path& path, const std::string& text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << text;
    }
}

TEST(ListInstalledPluginsTest, ValidPluginIsShownWithItsManifestAndNoError)
{
    TempDirectoryGuard tempDir;
    InstalledManifest options;
    options.mId = "alpha";
    options.mName = "The alpha plugin";
    writeInstalledManifest(tempDir.path() / "alpha", options);

    const std::vector<InstalledPlugin> installed = listInstalledPlugins({ tempDir.path() });

    ASSERT_EQ(installed.size(), 1u);
    EXPECT_EQ(installed[0].manifest.id, "alpha");
    EXPECT_EQ(installed[0].manifest.name, "The alpha plugin");
    EXPECT_EQ(installed[0].manifest.pluginVersion, "1.2.3");
    EXPECT_EQ(installed[0].directory, tempDir.path() / "alpha");
    EXPECT_TRUE(installed[0].error.empty()) << installed[0].error;
}

TEST(ListInstalledPluginsTest, UnparsableManifestIsListedUnderItsFolderNameWithAnError)
{
    TempDirectoryGuard tempDir;
    writeText(tempDir.path() / "broken_folder" / "plugin.json", "{ malformed json syntax");

    const std::vector<InstalledPlugin> installed = listInstalledPlugins({ tempDir.path() });

    ASSERT_EQ(installed.size(), 1u);
    EXPECT_EQ(installed[0].manifest.id, "broken_folder");
    EXPECT_EQ(installed[0].directory, tempDir.path() / "broken_folder");
    EXPECT_FALSE(installed[0].error.empty());
}

TEST(ListInstalledPluginsTest, ErrorOfAnIncompleteManifestNamesTheMissingField)
{
    TempDirectoryGuard tempDir;
    // Valid JSON without "kind", which the manifest parser requires (components/plugin/manifest_io.cpp).
    writeText(tempDir.path() / "no_kind" / "plugin.json",
        "{ \"format\": \"openeden.plugin\", \"version\": 1, \"id\": \"inner_id\", \"name\": \"n\", "
        "\"pluginVersion\": \"1.0.0\", \"sdk\": { \"major\": 2, \"minor\": 0 }, \"toolchain\": \"\" }");

    const std::vector<InstalledPlugin> installed = listInstalledPlugins({ tempDir.path() });

    ASSERT_EQ(installed.size(), 1u);
    EXPECT_EQ(installed[0].manifest.id, "no_kind");
    EXPECT_NE(installed[0].error.find("kind"), std::string::npos) << installed[0].error;
}

TEST(ListInstalledPluginsTest, Sdk1ManifestIsListedWithItsDetailsAndAnError)
{
    TempDirectoryGuard tempDir;
    InstalledManifest options;
    options.mId = "old_plugin";
    options.mSdkMajor = 1;
    writeInstalledManifest(tempDir.path() / "old_plugin", options);

    const std::vector<InstalledPlugin> installed = listInstalledPlugins({ tempDir.path() });

    ASSERT_EQ(installed.size(), 1u);
    EXPECT_EQ(installed[0].manifest.id, "old_plugin");
    EXPECT_NE(installed[0].error.find("SDK"), std::string::npos) << installed[0].error;
    EXPECT_NE(installed[0].error.find("1.0"), std::string::npos) << installed[0].error;
}

TEST(ListInstalledPluginsTest, NewerSdkMajorIsListedWithAnError)
{
    TempDirectoryGuard tempDir;
    InstalledManifest options;
    options.mId = "future_plugin";
    options.mSdkMajor = 3;
    writeInstalledManifest(tempDir.path() / "future_plugin", options);

    const std::vector<InstalledPlugin> installed = listInstalledPlugins({ tempDir.path() });

    ASSERT_EQ(installed.size(), 1u);
    EXPECT_NE(installed[0].error.find("SDK"), std::string::npos) << installed[0].error;
}

TEST(ListInstalledPluginsTest, FolderWithoutManifestAndLooseFilesAreNotListed)
{
    TempDirectoryGuard tempDir;
    std::filesystem::create_directories(tempDir.path() / "empty_folder");
    writeText(tempDir.path() / "not_a_plugin" / "readme.txt", "not a manifest");
    writeText(tempDir.path() / "loose_file.txt", "just a file");
    writeText(tempDir.path() / "plugin.json", "{ a manifest outside any plugin folder");

    EXPECT_TRUE(listInstalledPlugins({ tempDir.path() }).empty());
}

TEST(ListInstalledPluginsTest, MissingSearchDirectoryIsFine)
{
    TempDirectoryGuard tempDir;
    InstalledManifest options;
    options.mId = "alpha";
    writeInstalledManifest(tempDir.path() / "real" / "alpha", options);

    EXPECT_TRUE(listInstalledPlugins({}).empty());
    EXPECT_TRUE(listInstalledPlugins({ tempDir.path() / "missing" }).empty());

    const std::vector<InstalledPlugin> installed
        = listInstalledPlugins({ tempDir.path() / "missing", tempDir.path() / "real" });
    ASSERT_EQ(installed.size(), 1u);
    EXPECT_EQ(installed[0].manifest.id, "alpha");
}

TEST(ListInstalledPluginsTest, SameIdInTwoDirectoriesEarlierWins)
{
    TempDirectoryGuard tempDir;
    InstalledManifest first;
    first.mId = "shared";
    first.mName = "From the first directory";
    writeInstalledManifest(tempDir.path() / "first" / "shared", first);
    InstalledManifest second;
    second.mId = "shared";
    second.mName = "From the second directory";
    writeInstalledManifest(tempDir.path() / "second" / "shared", second);

    const std::vector<InstalledPlugin> forward
        = listInstalledPlugins({ tempDir.path() / "first", tempDir.path() / "second" });
    ASSERT_EQ(forward.size(), 1u);
    EXPECT_EQ(forward[0].manifest.name, "From the first directory");
    EXPECT_EQ(forward[0].directory, tempDir.path() / "first" / "shared");

    const std::vector<InstalledPlugin> backward
        = listInstalledPlugins({ tempDir.path() / "second", tempDir.path() / "first" });
    ASSERT_EQ(backward.size(), 1u);
    EXPECT_EQ(backward[0].manifest.name, "From the second directory");
}

TEST(ListInstalledPluginsTest, SortedByIdNotByFolderOrDirectory)
{
    TempDirectoryGuard tempDir;
    const auto write = [&](const char* dir, const char* folder, const char* id) {
        InstalledManifest options;
        options.mId = id;
        writeInstalledManifest(tempDir.path() / dir / folder, options);
    };
    write("one", "a_folder", "zulu");
    write("one", "z_folder", "alpha");
    write("two", "m_folder", "mike");
    writeText(tempDir.path() / "two" / "kilo" / "plugin.json", "{ broken, sorted by its folder's name");

    const std::vector<InstalledPlugin> installed
        = listInstalledPlugins({ tempDir.path() / "one", tempDir.path() / "two" });

    std::vector<std::string> ids;
    for (const InstalledPlugin& plugin : installed)
        ids.push_back(plugin.manifest.id);
    EXPECT_EQ(ids, (std::vector<std::string>{ "alpha", "kilo", "mike", "zulu" }));
}

TEST(ListInstalledPluginsTest, NoLibraryIsLoadedSoAMissingOrFakeLibraryIsNotAnError)
{
    TempDirectoryGuard tempDir;
    InstalledManifest options;
    options.mId = "no_library";
    writeInstalledManifest(tempDir.path() / "no_library", options);
    options.mId = "fake_library";
    writeInstalledManifest(tempDir.path() / "fake_library", options);
    writeText(tempDir.path() / "fake_library" / "does_not_exist.dll", "this is not a shared library");
    writeText(tempDir.path() / "fake_library" / "libdoes_not_exist.so", "this is not a shared library");

    const std::vector<InstalledPlugin> installed = listInstalledPlugins({ tempDir.path() });

    ASSERT_EQ(installed.size(), 2u);
    EXPECT_TRUE(installed[0].error.empty()) << installed[0].error;
    EXPECT_TRUE(installed[1].error.empty()) << installed[1].error;
}
