#include <components/extension/dynamic_library.hpp>
#include <components/extension/extension_loader.hpp>
#include <components/extension/lifecycle_manager.hpp>
#include <components/extension/load_error.hpp>
#include <components/extension/manifest.hpp>
#include <components/extension/manifest_io.hpp>
#include <openeden/extension.hpp>
#include <openeden/host.hpp>
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
    using openeden::extension::DiscoveredExtension;
    using openeden::extension::discoverExtensionDirectories;
    using openeden::extension::DynamicLibrary;
    using openeden::extension::ExtensionLoadError;
    using openeden::extension::ExtensionLoadResult;
    using openeden::extension::ExtensionManifest;
    using openeden::extension::filterEnabledExtensions;
    using openeden::extension::getHostToolchain;
    using openeden::extension::LifecycleManager;
    using openeden::extension::LoadedExtension;
    using openeden::extension::loadExtensionLibrary;
    using openeden::extension::validateExtensionHandshake;
    using openeden::sdk::Extension;
    using openeden::sdk::ExtensionDescriptor;
    using openeden::sdk::Host;
    using openeden::sdk::HostInfo;
    using openeden::sdk::kSdkVersion;
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
                                    "  \"format\": \"openeden.extension\",\n"
                                    "  \"version\": 1,\n"
                                    "  \"id\": \"" + id + "\",\n"
                                    "  \"name\": \"" + id + "\",\n"
                                    "  \"extensionVersion\": \"1.0.0\",\n"
                                    "  \"kind\": \"feature\",\n"
                                    "  \"sdk\": {\"major\": 4, \"minor\": 0},\n"
                                    "  \"toolchain\": \"" + getHostToolchain() + "\"\n"
                                    "}";

        std::ofstream file(dir / "extension.json");
        file << content;
    }

    struct LifecycleTracker
    {
        std::vector<std::string> events;
    };

    class TrackedMockExtension : public Extension
    {
    public:
        explicit TrackedMockExtension(LifecycleTracker& tracker, std::string id)
            : mTracker(tracker)
            , mId(std::move(id))
        {
        }

        ~TrackedMockExtension() override { mTracker.events.push_back(mId + ":destructed"); }

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

    std::unique_ptr<Extension> createSampleExtension()
    {
        static LifecycleTracker dummyTracker;
        return std::make_unique<TrackedMockExtension>(dummyTracker, "sample");
    }
} // namespace

TEST(ExtensionLoaderTest, DiscoverExtensionDirectories_FindsValidExtensionsAndIgnoresInvalidOrNonExtensionFolders)
{
    TempDirectoryGuard tempDir;
    const std::filesystem::path searchDir1 = tempDir.path() / "search1";
    const std::filesystem::path searchDir2 = tempDir.path() / "search2";

    // Setup searchDir1
    writeMockManifest(searchDir1 / "extension_alpha", "extension_alpha");
    writeMockManifest(searchDir1 / "extension_beta", "extension_beta");

    // Non-extension directory (no extension.json)
    const std::filesystem::path notAExtensionDir = searchDir1 / "not_a_extension";
    std::filesystem::create_directories(notAExtensionDir);
    {
        std::ofstream textFile(notAExtensionDir / "readme.txt");
        textFile << "Not an extension manifest";
    }

    // Empty folder
    std::filesystem::create_directories(searchDir1 / "empty_dir");

    // Regular file in search directory root
    {
        std::ofstream rootFile(searchDir1 / "some_file.txt");
        rootFile << "Just a file";
    }

    // Setup searchDir2
    writeMockManifest(searchDir2 / "extension_gamma", "extension_gamma");

    // Broken manifest folder
    const std::filesystem::path brokenManifestDir = searchDir2 / "broken_extension";
    std::filesystem::create_directories(brokenManifestDir);
    {
        std::ofstream brokenFile(brokenManifestDir / "extension.json");
        brokenFile << "{ malformed json syntax";
    }

    // Discover across both directories
    const auto discovered = discoverExtensionDirectories({ searchDir1, searchDir2 });

    ASSERT_EQ(discovered.size(), 3u);

    // Verify discovered IDs
    std::vector<std::string> discoveredIds;
    for (const auto& item : discovered)
        discoveredIds.push_back(item.manifest.id);

    EXPECT_EQ(discoveredIds[0], "extension_alpha");
    EXPECT_EQ(discoveredIds[1], "extension_beta");
    EXPECT_EQ(discoveredIds[2], "extension_gamma");

    // Verify paths are correctly recorded
    EXPECT_EQ(discovered[0].directory, searchDir1 / "extension_alpha");
    EXPECT_EQ(discovered[0].manifestPath, searchDir1 / "extension_alpha" / "extension.json");
}

TEST(ExtensionLoaderTest, FilterEnabledExtensions_FiltersSubsetAndPreservesDiscoveryOrder)
{
    DiscoveredExtension extensionA;
    extensionA.manifest.id = "extensionA";

    DiscoveredExtension extensionB;
    extensionB.manifest.id = "extensionB";

    DiscoveredExtension extensionC;
    extensionC.manifest.id = "extensionC";

    DiscoveredExtension extensionD;
    extensionD.manifest.id = "extensionD";

    const std::vector<DiscoveredExtension> discovered{ extensionA, extensionB, extensionC, extensionD };
    const std::vector<std::string> enabledList{ "extensionC", "extensionA" };

    const auto filtered = filterEnabledExtensions(discovered, enabledList);

    ASSERT_EQ(filtered.size(), 2u);
    // Preserves original discovery order
    EXPECT_EQ(filtered[0].manifest.id, "extensionA");
    EXPECT_EQ(filtered[1].manifest.id, "extensionC");
}

TEST(ExtensionLoaderTest, FilterEnabledExtensions_EmptyEnabledListDefaultsToAllEnabled)
{
    DiscoveredExtension extensionA;
    extensionA.manifest.id = "extensionA";

    DiscoveredExtension extensionB;
    extensionB.manifest.id = "extensionB";

    const std::vector<DiscoveredExtension> discovered{ extensionA, extensionB };
    const std::vector<std::string> emptyEnabledList{};

    const auto filtered = filterEnabledExtensions(discovered, emptyEnabledList);

    ASSERT_EQ(filtered.size(), 2u);
    EXPECT_EQ(filtered[0].manifest.id, "extensionA");
    EXPECT_EQ(filtered[1].manifest.id, "extensionB");
}

TEST(ExtensionLoaderTest, FilterEnabledExtensions_IgnoresUnknownExtensionsInEnableList)
{
    DiscoveredExtension extensionA;
    extensionA.manifest.id = "extensionA";

    const std::vector<DiscoveredExtension> discovered{ extensionA };
    const std::vector<std::string> enabledList{ "extensionA", "non_existent_extension_xyz" };

    const auto filtered = filterEnabledExtensions(discovered, enabledList);

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].manifest.id, "extensionA");
}

TEST(ExtensionLoaderTest, LoadExtensionLibrary_SdkVersionMismatch_RejectedBeforeLibraryLoad)
{
    MockHost host;

    DiscoveredExtension discovered;
    discovered.manifest.id = "old_extension";
    discovered.manifest.sdkVersion = Version{ 1, 0 }; // Built before SDK 2.0 removed API (and long before 3.0)
    discovered.manifest.toolchain = getHostToolchain();

    const ExtensionLoadResult result = loadExtensionLibrary(discovered, host.getHostInfo());

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.extensionId, "old_extension");
    EXPECT_EQ(result.error.stage, "sdk");
    EXPECT_NE(result.error.message.find("built for SDK 1.0; this host provides 4.0"), std::string::npos)
        << result.error.message;
}

TEST(ExtensionLoaderTest, LoadExtensionLibrary_Sdk1xMessageNamesTheRealMinor)
{
    MockHost host;

    DiscoveredExtension discovered;
    discovered.manifest.id = "old_extension";
    discovered.manifest.sdkVersion = Version{ 1, 7 };
    discovered.manifest.toolchain = getHostToolchain();

    const ExtensionLoadResult result = loadExtensionLibrary(discovered, host.getHostInfo());

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.stage, "sdk");
    EXPECT_NE(result.error.message.find("built for SDK 1.7; this host provides 4.0"), std::string::npos)
        << result.error.message;
}

// Task 0031 stage C: SDK 3.0 removed the Host editor accessor, so a library built for 2.x must not be loaded.
TEST(ExtensionLoaderTest, LoadExtensionLibrary_Sdk2x_RejectedBeforeLibraryLoadWithAClearReason)
{
    MockHost host;

    for (const std::uint16_t minor : { 0, 7 })
    {
        DiscoveredExtension discovered;
        discovered.manifest.id = "sdk2_extension";
        discovered.manifest.sdkVersion = Version{ 2, minor };
        discovered.manifest.toolchain = getHostToolchain();

        const ExtensionLoadResult result = loadExtensionLibrary(discovered, host.getHostInfo());

        EXPECT_FALSE(result.success);
        EXPECT_EQ(result.error.extensionId, "sdk2_extension");
        EXPECT_EQ(result.error.stage, "sdk");
        const std::string expected = "built for SDK 2." + std::to_string(minor) + "; this host provides 4.0";
        EXPECT_NE(result.error.message.find(expected), std::string::npos) << result.error.message;
    }
}

TEST(ExtensionLoaderTest, LoadExtensionLibrary_NewerSdkMajor_RejectedBeforeLibraryLoad)
{
    MockHost host;

    DiscoveredExtension discovered;
    discovered.manifest.id = "future_extension";
    discovered.manifest.sdkVersion = Version{ 5, 0 };
    discovered.manifest.toolchain = getHostToolchain();

    const ExtensionLoadResult result = loadExtensionLibrary(discovered, host.getHostInfo());

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.extensionId, "future_extension");
    EXPECT_EQ(result.error.stage, "sdk");
    EXPECT_NE(result.error.message.find("SDK"), std::string::npos);
}

TEST(ExtensionLoaderTest, LoadExtensionLibrary_ToolchainMismatch_RejectedBeforeLibraryLoad)
{
    MockHost host;

    DiscoveredExtension discovered;
    discovered.manifest.id = "foreign_toolchain_extension";
    discovered.manifest.sdkVersion = kSdkVersion;
    discovered.manifest.toolchain = "gcc-unknown-toolchain-xyz"; // Incompatible toolchain

    const ExtensionLoadResult result = loadExtensionLibrary(discovered, host.getHostInfo());

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.extensionId, "foreign_toolchain_extension");
    EXPECT_FALSE(result.error.message.empty());
    EXPECT_NE(result.error.message.find("toolchain"), std::string::npos);
}

TEST(ExtensionLoaderTest, LoadExtensionLibrary_MissingLibraryFile_RejectedWithError)
{
    MockHost host;
    TempDirectoryGuard tempDir;

    DiscoveredExtension discovered;
    discovered.directory = tempDir.path();
    discovered.manifest.id = "missing_lib_extension";
    discovered.manifest.sdkVersion = kSdkVersion;
    discovered.manifest.toolchain = getHostToolchain();
    discovered.manifest.library.windows = "missing_extension.dll";
    discovered.manifest.library.linux = "libmissing_extension.so";

    const ExtensionLoadResult result = loadExtensionLibrary(discovered, host.getHostInfo());

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.extensionId, "missing_lib_extension");
    EXPECT_FALSE(result.error.message.empty());
}

TEST(ExtensionLoaderTest, HandshakeValidation_NullDescriptor_Rejected)
{
    MockHost host;

    ExtensionManifest manifest;
    manifest.id = "extensionA";
    manifest.sdkVersion = Version{ 1, 0 };

    ExtensionLoadError error;
    const bool valid = validateExtensionHandshake(nullptr, manifest, host.getHostInfo(), error);

    EXPECT_FALSE(valid);
    EXPECT_EQ(error.extensionId, "extensionA");
    EXPECT_EQ(error.stage, "handshake");
    EXPECT_FALSE(error.message.empty());
    EXPECT_NE(error.message.find("null"), std::string::npos);
}

TEST(ExtensionLoaderTest, HandshakeValidation_IdMismatch_Rejected)
{
    MockHost host;

    ExtensionManifest manifest;
    manifest.id = "extensionA";
    manifest.sdkVersion = Version{ 1, 0 };

    const ExtensionDescriptor descriptor{ kSdkVersion, "wrong_extension_id", &createSampleExtension };

    ExtensionLoadError error;
    const bool valid = validateExtensionHandshake(&descriptor, manifest, host.getHostInfo(), error);

    EXPECT_FALSE(valid);
    EXPECT_EQ(error.extensionId, "extensionA");
    EXPECT_EQ(error.stage, "handshake");
    EXPECT_FALSE(error.message.empty());
    EXPECT_NE(error.message.find("id"), std::string::npos);
}

TEST(ExtensionLoaderTest, HandshakeValidation_SdkVersionMismatch_Rejected)
{
    MockHost host;

    ExtensionManifest manifest;
    manifest.id = "extensionA";
    manifest.sdkVersion = Version{ 1, 0 };

    const ExtensionDescriptor descriptor{ Version{ 2, 2 }, // Mismatch with manifest's current SDK
        "extensionA", &createSampleExtension };

    ExtensionLoadError error;
    const bool valid = validateExtensionHandshake(&descriptor, manifest, host.getHostInfo(), error);

    EXPECT_FALSE(valid);
    EXPECT_EQ(error.extensionId, "extensionA");
    EXPECT_EQ(error.stage, "handshake");
    EXPECT_FALSE(error.message.empty());
    EXPECT_NE(error.message.find("SDK"), std::string::npos);
}

TEST(ExtensionLoaderTest, HandshakeValidation_NullCreateFunction_Rejected)
{
    MockHost host;

    ExtensionManifest manifest;
    manifest.id = "extensionA";
    manifest.sdkVersion = Version{ 1, 0 };

    const ExtensionDescriptor descriptor{
        kSdkVersion, "extensionA",
        nullptr // Null create pointer
    };

    ExtensionLoadError error;
    const bool valid = validateExtensionHandshake(&descriptor, manifest, host.getHostInfo(), error);

    EXPECT_FALSE(valid);
    EXPECT_EQ(error.extensionId, "extensionA");
    EXPECT_EQ(error.stage, "handshake");
    EXPECT_FALSE(error.message.empty());
}

TEST(ExtensionLoaderTest, HandshakeValidation_MatchingDescriptor_Succeeds)
{
    MockHost host;

    ExtensionManifest manifest;
    manifest.id = "extensionA";
    manifest.sdkVersion = kSdkVersion;

    const ExtensionDescriptor descriptor{ kSdkVersion, "extensionA", &createSampleExtension };

    ExtensionLoadError error;
    const bool valid = validateExtensionHandshake(&descriptor, manifest, host.getHostInfo(), error);

    EXPECT_TRUE(valid);
    EXPECT_TRUE(error.message.empty());
}

TEST(ExtensionLoaderTest, HandshakeValidation_LibraryBuiltForSdk1_Rejected)
{
    MockHost host;

    // The manifest claims the current SDK, but the library itself says it was built for 1.0
    ExtensionManifest manifest;
    manifest.id = "extensionA";
    manifest.sdkVersion = kSdkVersion;

    const ExtensionDescriptor descriptor{ Version{ 1, 0 }, "extensionA", &createSampleExtension };

    ExtensionLoadError error;
    const bool valid = validateExtensionHandshake(&descriptor, manifest, host.getHostInfo(), error);

    EXPECT_FALSE(valid);
    EXPECT_EQ(error.extensionId, "extensionA");
    EXPECT_EQ(error.stage, "handshake");
    EXPECT_NE(error.message.find("SDK"), std::string::npos);
}

TEST(ExtensionLoaderTest, HandshakeValidation_ManifestAndLibraryBothSdk1_Rejected)
{
    MockHost host;

    // They agree with each other but not with the host, which validateExtensionHandshake is also given
    ExtensionManifest manifest;
    manifest.id = "extensionA";
    manifest.sdkVersion = Version{ 1, 0 };

    const ExtensionDescriptor descriptor{ Version{ 1, 0 }, "extensionA", &createSampleExtension };

    ExtensionLoadError error;
    const bool valid = validateExtensionHandshake(&descriptor, manifest, host.getHostInfo(), error);

    EXPECT_FALSE(valid);
    EXPECT_EQ(error.extensionId, "extensionA");
    EXPECT_NE(error.message.find("SDK"), std::string::npos);
}

TEST(ExtensionLoaderTest, LifecycleManager_DynamicExtensionDestructedBeforeLibraryUnload)
{
    MockHost host;
    LifecycleTracker tracker;

    ExtensionManifest manifest;
    manifest.id = "dynamic_extension";
    manifest.sdkVersion = kSdkVersion;
    manifest.toolchain = getHostToolchain();

    auto extensionInstance = std::make_unique<TrackedMockExtension>(tracker, "dynamic_extension");

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
        manager.addExtension(manifest, std::move(extensionInstance), std::move(trackedLib));

        ASSERT_TRUE(manager.initialize());
        EXPECT_TRUE(manager.isExtensionActive("dynamic_extension"));

        manager.shutdown();
    }

    // Invariant: stop() -> ~Extension() -> library unload
    const std::vector<std::string> expectedOrder{ "dynamic_extension:registered", "dynamic_extension:resolved",
        "dynamic_extension:started", "dynamic_extension:stopped", "dynamic_extension:destructed", "library:unloaded" };

    EXPECT_EQ(tracker.events, expectedOrder);
}

// ---- listInstalledExtensions (task 0018): what a launcher shows
// -------------------------------------------------------

namespace
{
    using openeden::extension::InstalledExtension;
    using openeden::extension::listInstalledExtensions;

    struct InstalledManifest
    {
        std::string mId;
        int mSdkMajor = 4;
        std::string mName = "A name";
        std::string mLicence = "GPL-3.0-or-later";
    };

    // The library named here never exists: a listing must not care.
    void writeInstalledManifest(const std::filesystem::path& dir, const InstalledManifest& options)
    {
        std::filesystem::create_directories(dir);
        std::ofstream file(dir / "extension.json");
        file << "{\n"
             << "  \"format\": \"openeden.extension\",\n"
             << "  \"version\": 1,\n"
             << "  \"id\": \"" << options.mId << "\",\n"
             << "  \"name\": \"" << options.mName << "\",\n"
             << "  \"extensionVersion\": \"1.2.3\",\n"
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

TEST(ListInstalledExtensionsTest, ValidExtensionIsShownWithItsManifestAndNoError)
{
    TempDirectoryGuard tempDir;
    InstalledManifest options;
    options.mId = "alpha";
    options.mName = "The alpha extension";
    writeInstalledManifest(tempDir.path() / "alpha", options);

    const std::vector<InstalledExtension> installed = listInstalledExtensions({ tempDir.path() });

    ASSERT_EQ(installed.size(), 1u);
    EXPECT_EQ(installed[0].manifest.id, "alpha");
    EXPECT_EQ(installed[0].manifest.name, "The alpha extension");
    EXPECT_EQ(installed[0].manifest.extensionVersion, "1.2.3");
    EXPECT_EQ(installed[0].directory, tempDir.path() / "alpha");
    EXPECT_TRUE(installed[0].error.empty()) << installed[0].error;
}

TEST(ListInstalledExtensionsTest, UnparsableManifestIsListedUnderItsFolderNameWithAnError)
{
    TempDirectoryGuard tempDir;
    writeText(tempDir.path() / "broken_folder" / "extension.json", "{ malformed json syntax");

    const std::vector<InstalledExtension> installed = listInstalledExtensions({ tempDir.path() });

    ASSERT_EQ(installed.size(), 1u);
    EXPECT_EQ(installed[0].manifest.id, "broken_folder");
    EXPECT_EQ(installed[0].directory, tempDir.path() / "broken_folder");
    EXPECT_FALSE(installed[0].error.empty());
}

TEST(ListInstalledExtensionsTest, ErrorOfAnIncompleteManifestNamesTheMissingField)
{
    TempDirectoryGuard tempDir;
    // Valid JSON without "kind", which the manifest parser requires (components/extension/manifest_io.cpp).
    writeText(tempDir.path() / "no_kind" / "extension.json",
        "{ \"format\": \"openeden.extension\", \"version\": 1, \"id\": \"inner_id\", \"name\": \"n\", "
        "\"extensionVersion\": \"1.0.0\", \"sdk\": { \"major\": 4, \"minor\": 0 }, \"toolchain\": \"\" }");

    const std::vector<InstalledExtension> installed = listInstalledExtensions({ tempDir.path() });

    ASSERT_EQ(installed.size(), 1u);
    EXPECT_EQ(installed[0].manifest.id, "no_kind");
    EXPECT_NE(installed[0].error.find("kind"), std::string::npos) << installed[0].error;
}

TEST(ListInstalledExtensionsTest, Sdk1ManifestIsListedWithItsDetailsAndAnError)
{
    TempDirectoryGuard tempDir;
    InstalledManifest options;
    options.mId = "old_extension";
    options.mSdkMajor = 1;
    writeInstalledManifest(tempDir.path() / "old_extension", options);

    const std::vector<InstalledExtension> installed = listInstalledExtensions({ tempDir.path() });

    ASSERT_EQ(installed.size(), 1u);
    EXPECT_EQ(installed[0].manifest.id, "old_extension");
    EXPECT_NE(installed[0].error.find("SDK"), std::string::npos) << installed[0].error;
    EXPECT_NE(installed[0].error.find("1.0"), std::string::npos) << installed[0].error;
}

TEST(ListInstalledExtensionsTest, Sdk2ManifestIsListedWithItsDetailsAndAnErrorNamingBothVersions)
{
    TempDirectoryGuard tempDir;
    InstalledManifest options;
    options.mId = "sdk2_extension";
    options.mSdkMajor = 2;
    writeInstalledManifest(tempDir.path() / "sdk2_extension", options);

    const std::vector<InstalledExtension> installed = listInstalledExtensions({ tempDir.path() });

    ASSERT_EQ(installed.size(), 1u);
    EXPECT_EQ(installed[0].manifest.id, "sdk2_extension");
    EXPECT_NE(installed[0].error.find("Built for SDK 2.0; this build provides 4.0"), std::string::npos)
        << installed[0].error;
}

TEST(ListInstalledExtensionsTest, CurrentSdkManifestIsListedWithoutAnError)
{
    TempDirectoryGuard tempDir;
    InstalledManifest options;
    options.mId = "current_extension";
    options.mSdkMajor = 4;
    writeInstalledManifest(tempDir.path() / "current_extension", options);

    const std::vector<InstalledExtension> installed = listInstalledExtensions({ tempDir.path() });

    ASSERT_EQ(installed.size(), 1u);
    EXPECT_TRUE(installed[0].error.empty()) << installed[0].error;
}

TEST(ListInstalledExtensionsTest, NewerSdkMajorIsListedWithAnError)
{
    TempDirectoryGuard tempDir;
    InstalledManifest options;
    options.mId = "future_extension";
    options.mSdkMajor = 5;
    writeInstalledManifest(tempDir.path() / "future_extension", options);

    const std::vector<InstalledExtension> installed = listInstalledExtensions({ tempDir.path() });

    ASSERT_EQ(installed.size(), 1u);
    EXPECT_NE(installed[0].error.find("SDK"), std::string::npos) << installed[0].error;
}

TEST(ListInstalledExtensionsTest, FolderWithoutManifestAndLooseFilesAreNotListed)
{
    TempDirectoryGuard tempDir;
    std::filesystem::create_directories(tempDir.path() / "empty_folder");
    writeText(tempDir.path() / "not_a_extension" / "readme.txt", "not a manifest");
    writeText(tempDir.path() / "loose_file.txt", "just a file");
    writeText(tempDir.path() / "extension.json", "{ a manifest outside any extension folder");

    EXPECT_TRUE(listInstalledExtensions({ tempDir.path() }).empty());
}

TEST(ListInstalledExtensionsTest, MissingSearchDirectoryIsFine)
{
    TempDirectoryGuard tempDir;
    InstalledManifest options;
    options.mId = "alpha";
    writeInstalledManifest(tempDir.path() / "real" / "alpha", options);

    EXPECT_TRUE(listInstalledExtensions({}).empty());
    EXPECT_TRUE(listInstalledExtensions({ tempDir.path() / "missing" }).empty());

    const std::vector<InstalledExtension> installed
        = listInstalledExtensions({ tempDir.path() / "missing", tempDir.path() / "real" });
    ASSERT_EQ(installed.size(), 1u);
    EXPECT_EQ(installed[0].manifest.id, "alpha");
}

TEST(ListInstalledExtensionsTest, SameIdInTwoDirectoriesEarlierWins)
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

    const std::vector<InstalledExtension> forward
        = listInstalledExtensions({ tempDir.path() / "first", tempDir.path() / "second" });
    ASSERT_EQ(forward.size(), 1u);
    EXPECT_EQ(forward[0].manifest.name, "From the first directory");
    EXPECT_EQ(forward[0].directory, tempDir.path() / "first" / "shared");

    const std::vector<InstalledExtension> backward
        = listInstalledExtensions({ tempDir.path() / "second", tempDir.path() / "first" });
    ASSERT_EQ(backward.size(), 1u);
    EXPECT_EQ(backward[0].manifest.name, "From the second directory");
}

TEST(ListInstalledExtensionsTest, SortedByIdNotByFolderOrDirectory)
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
    writeText(tempDir.path() / "two" / "kilo" / "extension.json", "{ broken, sorted by its folder's name");

    const std::vector<InstalledExtension> installed
        = listInstalledExtensions({ tempDir.path() / "one", tempDir.path() / "two" });

    std::vector<std::string> ids;
    for (const InstalledExtension& extension : installed)
        ids.push_back(extension.manifest.id);
    EXPECT_EQ(ids, (std::vector<std::string>{ "alpha", "kilo", "mike", "zulu" }));
}

TEST(ListInstalledExtensionsTest, NoLibraryIsLoadedSoAMissingOrFakeLibraryIsNotAnError)
{
    TempDirectoryGuard tempDir;
    InstalledManifest options;
    options.mId = "no_library";
    writeInstalledManifest(tempDir.path() / "no_library", options);
    options.mId = "fake_library";
    writeInstalledManifest(tempDir.path() / "fake_library", options);
    writeText(tempDir.path() / "fake_library" / "does_not_exist.dll", "this is not a shared library");
    writeText(tempDir.path() / "fake_library" / "libdoes_not_exist.so", "this is not a shared library");

    const std::vector<InstalledExtension> installed = listInstalledExtensions({ tempDir.path() });

    ASSERT_EQ(installed.size(), 2u);
    EXPECT_TRUE(installed[0].error.empty()) << installed[0].error;
    EXPECT_TRUE(installed[1].error.empty()) << installed[1].error;
}
