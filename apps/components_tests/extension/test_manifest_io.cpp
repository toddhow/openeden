#include <components/extension/load_error.hpp>
#include <components/extension/manifest.hpp>
#include <components/extension/manifest_io.hpp>
#include <openeden/version.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    using openeden::extension::ContractRequirement;
    using openeden::extension::ExtensionManifest;
    using openeden::extension::LoadError;
    using openeden::extension::loadManifestFile;
    using openeden::extension::parseManifest;
    using openeden::extension::writeCanonicalManifest;
    using openeden::sdk::Version;

    // Helper RAII directory for file-based tests
    class TempDirectoryGuard
    {
    public:
        TempDirectoryGuard()
        {
            mPath = std::filesystem::temp_directory_path()
                / ("openeden_test_manifest_"
                    + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
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
} // namespace

TEST(ManifestIoTest, ParseValidCanonicalManifest_AllFieldsExtractedCorrectly)
{
    // Section 5.2 manifest definition
    const std::string jsonText = R"({
  "format": "openeden.extension",
  "version": 1,
  "id": "creativemotion",
  "name": "CreativeMotion",
  "extensionVersion": "1.4.0",
  "kind": "feature",
  "sdk": {
    "major": 1,
    "minor": 2
  },
  "toolchain": "msvc-19.44-x64-release",
  "library": {
    "windows": "creativemotion.dll",
    "linux": "libcreativemotion.so"
  },
  "provides": [
    {
      "contract": "openeden.animation",
      "version": [1, 1]
    }
  ],
  "requires": [
    {
      "contract": "openeden.game",
      "version": [1, 0]
    }
  ],
  "optional": [
    {
      "contract": "openeden.body",
      "version": [1, 0]
    }
  ],
  "loadAfter": [],
  "data": "data",
  "settings": [
    {
      "key": "enabled",
      "type": "bool",
      "default": false
    }
  ],
  "licence": "GPL-3.0-or-later"
})";

    LoadError err;
    const auto manifest = parseManifest(jsonText, "extension.json", err);

    ASSERT_TRUE(manifest.has_value()) << "Failed to parse valid manifest: " << err.toString();
    EXPECT_EQ(manifest->format, "openeden.extension");
    EXPECT_EQ(manifest->version, 1);
    EXPECT_EQ(manifest->id, "creativemotion");
    EXPECT_EQ(manifest->name, "CreativeMotion");
    EXPECT_EQ(manifest->extensionVersion, "1.4.0");
    EXPECT_EQ(manifest->kind, "feature");
    EXPECT_EQ(manifest->sdkVersion, (Version{ 1, 2 }));
    EXPECT_EQ(manifest->toolchain, "msvc-19.44-x64-release");
    EXPECT_EQ(manifest->library.windows, "creativemotion.dll");
    EXPECT_EQ(manifest->library.linux, "libcreativemotion.so");

    ASSERT_EQ(manifest->providedContracts.size(), 1u);
    EXPECT_EQ(manifest->providedContracts[0].contract, "openeden.animation");
    EXPECT_EQ(manifest->providedContracts[0].version, (Version{ 1, 1 }));

    ASSERT_EQ(manifest->requiredContracts.size(), 1u);
    EXPECT_EQ(manifest->requiredContracts[0].contract, "openeden.game");
    EXPECT_EQ(manifest->requiredContracts[0].version, (Version{ 1, 0 }));

    ASSERT_EQ(manifest->optionalContracts.size(), 1u);
    EXPECT_EQ(manifest->optionalContracts[0].contract, "openeden.body");
    EXPECT_EQ(manifest->optionalContracts[0].version, (Version{ 1, 0 }));

    EXPECT_TRUE(manifest->loadAfter.empty());
    EXPECT_EQ(manifest->dataPath, "data");

    ASSERT_EQ(manifest->settings.size(), 1u);
    EXPECT_EQ(manifest->settings[0].key, "enabled");
    EXPECT_EQ(manifest->settings[0].type, "bool");

    EXPECT_EQ(manifest->licence, "GPL-3.0-or-later");
}

TEST(ManifestIoTest, ByteForByteCanonicalRoundTrip)
{
    // Canonical format: fixed key order, 2-space indentation, no trailing commas
    const std::string canonical = R"({
  "format": "openeden.extension",
  "version": 1,
  "id": "creativemotion",
  "name": "CreativeMotion",
  "extensionVersion": "1.4.0",
  "kind": "feature",
  "sdk": {
    "major": 1,
    "minor": 2
  },
  "toolchain": "msvc-19.44-x64-release",
  "library": {
    "windows": "creativemotion.dll",
    "linux": "libcreativemotion.so"
  },
  "provides": [
    {
      "contract": "openeden.animation",
      "version": [
        1,
        1
      ]
    }
  ],
  "requires": [
    {
      "contract": "openeden.game",
      "version": [
        1,
        0
      ]
    }
  ],
  "optional": [
    {
      "contract": "openeden.body",
      "version": [
        1,
        0
      ]
    }
  ],
  "loadAfter": [],
  "data": "data",
  "settings": [
    {
      "key": "enabled",
      "type": "bool",
      "default": false
    }
  ],
  "licence": "GPL-3.0-or-later"
})";

    LoadError err;
    const auto manifest = parseManifest(canonical, "extension.json", err);
    ASSERT_TRUE(manifest.has_value()) << "Parse error: " << err.toString();

    const std::string serialized = writeCanonicalManifest(*manifest);
    EXPECT_EQ(serialized, canonical);

    // Verify idempotent re-parse and re-write
    LoadError err2;
    const auto manifest2 = parseManifest(serialized, "extension.json", err2);
    ASSERT_TRUE(manifest2.has_value()) << "Second parse error: " << err2.toString();
    EXPECT_EQ(writeCanonicalManifest(*manifest2), canonical);
}

TEST(ManifestIoTest, SyntaxError_ReportsAccurateLineNumberAndFilename)
{
    // Missing comma and unclosed bracket at line 4
    const std::string malformedJson
        = "{\n  \"format\": \"openeden.extension\",\n  \"version\": 1\n  \"id\": \"bad\"\n}";

    LoadError err;
    const auto manifest = parseManifest(malformedJson, "extension.json", err);

    EXPECT_FALSE(manifest.has_value());
    EXPECT_EQ(err.file, "extension.json");
    EXPECT_EQ(err.line, 4);
    EXPECT_FALSE(err.message.empty());
    EXPECT_NE(err.toString().find("extension.json"), std::string::npos);
    EXPECT_NE(err.toString().find("4"), std::string::npos);
}

TEST(ManifestIoTest, MissingRequiredField_Id_ReportsLocatedError)
{
    const std::string missingIdJson = R"({
  "format": "openeden.extension",
  "version": 1,
  "name": "Missing ID",
  "extensionVersion": "1.0.0",
  "kind": "feature",
  "sdk": {"major": 1, "minor": 0},
  "toolchain": "msvc-19.44-x64-release"
})";

    LoadError err;
    const auto manifest = parseManifest(missingIdJson, "manifests/missing_id.json", err);

    EXPECT_FALSE(manifest.has_value());
    EXPECT_EQ(err.file, "manifests/missing_id.json");
    EXPECT_EQ(err.field, "/id");
    EXPECT_FALSE(err.message.empty());
    EXPECT_NE(err.toString().find("/id"), std::string::npos);
}

TEST(ManifestIoTest, MissingRequiredField_Sdk_ReportsLocatedError)
{
    const std::string missingSdkJson = R"({
  "format": "openeden.extension",
  "version": 1,
  "id": "myextension",
  "name": "Missing SDK",
  "extensionVersion": "1.0.0",
  "kind": "feature",
  "toolchain": "msvc-19.44-x64-release"
})";

    LoadError err;
    const auto manifest = parseManifest(missingSdkJson, "extension.json", err);

    EXPECT_FALSE(manifest.has_value());
    EXPECT_EQ(err.field, "/sdk");
    EXPECT_FALSE(err.message.empty());
}

TEST(ManifestIoTest, WrongFormatString_RejectedWithLocatedError)
{
    const std::string wrongFormatJson = R"({
  "format": "some.other.format",
  "version": 1,
  "id": "badformat",
  "name": "Bad Format",
  "extensionVersion": "1.0.0",
  "kind": "feature",
  "sdk": {"major": 1, "minor": 0},
  "toolchain": "msvc-19.44-x64-release"
})";

    LoadError err;
    const auto manifest = parseManifest(wrongFormatJson, "extension.json", err);

    EXPECT_FALSE(manifest.has_value());
    EXPECT_EQ(err.field, "/format");
    EXPECT_NE(err.message.find("openeden.extension"), std::string::npos);
}

TEST(ManifestIoTest, UnsupportedFormatVersion_RejectedWithLocatedError)
{
    const std::string wrongVersionJson = R"({
  "format": "openeden.extension",
  "version": 3,
  "id": "futureextension",
  "name": "Future Extension",
  "extensionVersion": "1.0.0",
  "kind": "feature",
  "sdk": {"major": 1, "minor": 0},
  "toolchain": "msvc-19.44-x64-release"
})";

    LoadError err;
    const auto manifest = parseManifest(wrongVersionJson, "extension.json", err);

    EXPECT_FALSE(manifest.has_value());
    EXPECT_EQ(err.field, "/version");
    EXPECT_FALSE(err.message.empty());
}

TEST(ManifestIoTest, InvalidFieldType_ProvidesAsObjectInsteadOfArray_Rejected)
{
    const std::string invalidProvidesTypeJson = R"({
  "format": "openeden.extension",
  "version": 1,
  "id": "invalidtype",
  "name": "Invalid Type",
  "extensionVersion": "1.0.0",
  "kind": "feature",
  "sdk": {"major": 1, "minor": 0},
  "toolchain": "msvc-19.44-x64-release",
  "provides": {
    "contract": "openeden.test",
    "version": [1, 0]
  }
})";

    LoadError err;
    const auto manifest = parseManifest(invalidProvidesTypeJson, "extension.json", err);

    EXPECT_FALSE(manifest.has_value());
    EXPECT_EQ(err.field, "/provides");
    EXPECT_FALSE(err.message.empty());
}

TEST(ManifestIoTest, InvalidContractRequirementFormat_VersionNotArray_Rejected)
{
    const std::string stringVersionJson = R"({
  "format": "openeden.extension",
  "version": 1,
  "id": "badversionformat",
  "name": "Bad Version Format",
  "extensionVersion": "1.0.0",
  "kind": "feature",
  "sdk": {"major": 1, "minor": 0},
  "toolchain": "msvc-19.44-x64-release",
  "provides": [
    {
      "contract": "openeden.test",
      "version": "1.0"
    }
  ]
})";

    LoadError err;
    const auto manifest = parseManifest(stringVersionJson, "extension.json", err);

    EXPECT_FALSE(manifest.has_value());
    EXPECT_EQ(err.field, "/provides/0/version");
    EXPECT_FALSE(err.message.empty());
}

TEST(ManifestIoTest, InvalidContractRequirementFormat_VersionArrayWrongLength_Rejected)
{
    const std::string shortVersionArrayJson = R"({
  "format": "openeden.extension",
  "version": 1,
  "id": "shortversionarray",
  "name": "Short Version Array",
  "extensionVersion": "1.0.0",
  "kind": "feature",
  "sdk": {"major": 1, "minor": 0},
  "toolchain": "msvc-19.44-x64-release",
  "requires": [
    {
      "contract": "openeden.test",
      "version": [1]
    }
  ]
})";

    LoadError err;
    const auto manifest = parseManifest(shortVersionArrayJson, "extension.json", err);

    EXPECT_FALSE(manifest.has_value());
    EXPECT_EQ(err.field, "/requires/0/version");
    EXPECT_FALSE(err.message.empty());
}

TEST(ManifestIoTest, UnknownKey_CatchesTypo_RejectedWithJsonPointer)
{
    const std::string typoKeyJson = R"({
  "format": "openeden.extension",
  "version": 1,
  "id": "typokey",
  "name": "Typo Key",
  "extensionVersion": "1.0.0",
  "kind": "feature",
  "sdk": {"major": 1, "minor": 0},
  "toolchain": "msvc-19.44-x64-release",
  "unknownelement": true
})";

    LoadError err;
    const auto manifest = parseManifest(typoKeyJson, "extension.json", err);

    EXPECT_FALSE(manifest.has_value());
    EXPECT_EQ(err.field, "/unknownelement");
    EXPECT_FALSE(err.message.empty());
    EXPECT_NE(err.message.find("unknown"), std::string::npos);
}

TEST(ManifestIoTest, LoadManifestFile_NonExistentFile_ReturnsError)
{
    LoadError err;
    const auto manifest = loadManifestFile("non_existent_directory_98765/extension.json", err);

    EXPECT_FALSE(manifest.has_value());
    EXPECT_FALSE(err.message.empty());
}

TEST(ManifestIoTest, LoadManifestFile_ValidFile_ReturnsManifest)
{
    TempDirectoryGuard tempDir;
    const std::filesystem::path manifestPath = tempDir.path() / "extension.json";

    const std::string content = R"({
  "format": "openeden.extension",
  "version": 1,
  "id": "fileextension",
  "name": "File Extension",
  "extensionVersion": "1.0.0",
  "kind": "feature",
  "sdk": {"major": 1, "minor": 0},
  "toolchain": "msvc-19.44-x64-release"
})";

    {
        std::ofstream out(manifestPath);
        ASSERT_TRUE(out.is_open());
        out << content;
    }

    LoadError err;
    const auto manifest = loadManifestFile(manifestPath, err);

    ASSERT_TRUE(manifest.has_value()) << "Failed to load manifest file: " << err.toString();
    EXPECT_EQ(manifest->id, "fileextension");
    EXPECT_EQ(manifest->name, "File Extension");
}
