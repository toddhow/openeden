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
                                    "  \"sdk\": {\"major\": 1, \"minor\": 0},\n"
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
    MockHost host(Version{ 1, 0 });

    DiscoveredPlugin discovered;
    discovered.manifest.id = "future_plugin";
    discovered.manifest.sdkVersion = Version{ 2, 0 }; // Incompatible major version
    discovered.manifest.toolchain = getHostToolchain();

    const PluginLoadResult result = loadPluginLibrary(discovered, host.getHostInfo());

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.pluginId, "future_plugin");
    EXPECT_FALSE(result.error.message.empty());
    EXPECT_NE(result.error.message.find("SDK"), std::string::npos);
}

TEST(PluginLoaderTest, LoadPluginLibrary_ToolchainMismatch_RejectedBeforeLibraryLoad)
{
    MockHost host(Version{ 1, 0 });

    DiscoveredPlugin discovered;
    discovered.manifest.id = "foreign_toolchain_plugin";
    discovered.manifest.sdkVersion = Version{ 1, 0 };
    discovered.manifest.toolchain = "gcc-unknown-toolchain-xyz"; // Incompatible toolchain

    const PluginLoadResult result = loadPluginLibrary(discovered, host.getHostInfo());

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.pluginId, "foreign_toolchain_plugin");
    EXPECT_FALSE(result.error.message.empty());
    EXPECT_NE(result.error.message.find("toolchain"), std::string::npos);
}

TEST(PluginLoaderTest, LoadPluginLibrary_MissingLibraryFile_RejectedWithError)
{
    MockHost host(Version{ 1, 0 });
    TempDirectoryGuard tempDir;

    DiscoveredPlugin discovered;
    discovered.directory = tempDir.path();
    discovered.manifest.id = "missing_lib_plugin";
    discovered.manifest.sdkVersion = Version{ 1, 0 };
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
    MockHost host(Version{ 1, 0 });

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
    MockHost host(Version{ 1, 0 });

    PluginManifest manifest;
    manifest.id = "pluginA";
    manifest.sdkVersion = Version{ 1, 0 };

    const PluginDescriptor descriptor{ Version{ 1, 0 }, "wrong_plugin_id", &createSamplePlugin };

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
    MockHost host(Version{ 1, 0 });

    PluginManifest manifest;
    manifest.id = "pluginA";
    manifest.sdkVersion = Version{ 1, 0 };

    const PluginDescriptor descriptor{ Version{ 1, 2 }, // Mismatch with manifest's 1.0
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
    MockHost host(Version{ 1, 0 });

    PluginManifest manifest;
    manifest.id = "pluginA";
    manifest.sdkVersion = Version{ 1, 0 };

    const PluginDescriptor descriptor{
        Version{ 1, 0 }, "pluginA",
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
    MockHost host(Version{ 1, 0 });

    PluginManifest manifest;
    manifest.id = "pluginA";
    manifest.sdkVersion = Version{ 1, 0 };

    const PluginDescriptor descriptor{ Version{ 1, 0 }, "pluginA", &createSamplePlugin };

    PluginLoadError error;
    const bool valid = validatePluginHandshake(&descriptor, manifest, host.getHostInfo(), error);

    EXPECT_TRUE(valid);
    EXPECT_TRUE(error.message.empty());
}

TEST(PluginLoaderTest, LifecycleManager_DynamicPluginDestructedBeforeLibraryUnload)
{
    MockHost host;
    LifecycleTracker tracker;

    PluginManifest manifest;
    manifest.id = "dynamic_plugin";
    manifest.sdkVersion = Version{ 1, 0 };
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
