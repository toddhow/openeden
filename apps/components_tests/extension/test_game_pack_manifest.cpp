// Task 0084: manifest format v2 (kind "game"), the pack kinds and SDK 4.0. Written from the spec before the code.
#include <components/extension/dependency_resolver.hpp>
#include <components/extension/extension_loader.hpp>
#include <components/extension/load_error.hpp>
#include <components/extension/manifest.hpp>
#include <components/extension/manifest_io.hpp>
#include <openeden/pack_kind.hpp>
#include <openeden/phases.hpp>
#include <openeden/version.hpp>

#include <gtest/gtest.h>

#include <string>

namespace
{
    using openeden::extension::LoadError;
    using openeden::extension::parseManifest;
    using openeden::extension::writeCanonicalManifest;
    using openeden::sdk::PackKind;

    std::string manifestText(int formatVersion, const std::string& kind, const std::string& id = "game_test")
    {
        return std::string("{\n  \"format\": \"openeden.extension\",\n  \"version\": ") + std::to_string(formatVersion)
            + ",\n  \"id\": \"" + id + "\",\n  \"name\": \"Test game\",\n  \"extensionVersion\": \"1.0.0\",\n  \"kind\": \""
            + kind
            + "\",\n  \"sdk\": {\n    \"major\": 4,\n    \"minor\": 0\n  },\n  \"toolchain\": \"\",\n  \"library\": {\n    "
              "\"windows\": \"game_test.dll\",\n    \"linux\": \"libgame_test.so\"\n  },\n  \"provides\": [],\n  "
              "\"requires\": [],\n  \"optional\": [],\n  \"loadAfter\": [],\n  \"data\": \"data\",\n  \"settings\": [],\n  "
              "\"licence\": \"GPL-3.0-or-later\"\n}";
    }
}

TEST(GamePackSdk, VersionIsFourPointZero)
{
    EXPECT_EQ(openeden::sdk::kSdkVersion, (openeden::sdk::Version{ 4, 0 }));
}

TEST(GamePackSdk, PackKindNamesRoundTrip)
{
    EXPECT_EQ(openeden::sdk::parsePackKind("feature"), PackKind::Feature);
    EXPECT_EQ(openeden::sdk::parsePackKind("compatibility"), PackKind::Compatibility);
    EXPECT_EQ(openeden::sdk::parsePackKind("game"), PackKind::Game);
    EXPECT_FALSE(openeden::sdk::parsePackKind("extension").has_value());
    EXPECT_FALSE(openeden::sdk::parsePackKind("compat").has_value());
    EXPECT_FALSE(openeden::sdk::parsePackKind("").has_value());
    for (const PackKind kind : { PackKind::Feature, PackKind::Compatibility, PackKind::Game })
    {
        EXPECT_EQ(openeden::sdk::parsePackKind(openeden::sdk::toString(kind)), kind);
    }
}

TEST(GamePackSdk, OnlyGameHasGameCapabilities)
{
    EXPECT_FALSE(openeden::sdk::hasGameCapabilities(PackKind::Feature));
    EXPECT_FALSE(openeden::sdk::hasGameCapabilities(PackKind::Compatibility));
    EXPECT_TRUE(openeden::sdk::hasGameCapabilities(PackKind::Game));
}

TEST(GamePackSdk, GamePhasesComeAfterTheSixFramePhases)
{
    using openeden::sdk::Phase;
    static_assert(Phase::Ui < Phase::GameSimulation);
    static_assert(Phase::GameSimulation < Phase::GameUpdateUi);
    EXPECT_NE(Phase::GameSimulation, Phase::GameUpdateUi);
}

TEST(GamePackManifest, V2GameKindParsesAndRoundTripsByteForByte)
{
    const std::string text = manifestText(2, "game");
    LoadError err;
    const auto manifest = parseManifest(text, "extension.json", err);
    ASSERT_TRUE(manifest.has_value()) << err.toString();
    EXPECT_EQ(manifest->version, 2);
    EXPECT_EQ(manifest->kind, "game");
    EXPECT_EQ(manifest->sdkVersion, (openeden::sdk::Version{ 4, 0 }));
    EXPECT_EQ(writeCanonicalManifest(*manifest), text);
}

TEST(GamePackManifest, V2AcceptsEveryKnownKind)
{
    for (const char* kind : { "feature", "compatibility", "game" })
    {
        LoadError err;
        const auto manifest = parseManifest(manifestText(2, kind), "extension.json", err);
        ASSERT_TRUE(manifest.has_value()) << kind << ": " << err.toString();
        EXPECT_EQ(manifest->kind, kind);
    }
}

TEST(GamePackManifest, V2RefusesUnknownKindWithLocatedError)
{
    LoadError err;
    const auto manifest = parseManifest(manifestText(2, "gamepack"), "packs/x/extension.json", err);
    EXPECT_FALSE(manifest.has_value());
    EXPECT_EQ(err.file, "packs/x/extension.json");
    EXPECT_EQ(err.field, "/kind");
    EXPECT_GT(err.line, 0u);
    EXPECT_NE(err.message.find("gamepack"), std::string::npos) << err.message;
    EXPECT_NE(err.message.find("game"), std::string::npos) << err.message;
}

TEST(GamePackManifest, V1CannotDeclareKindGame)
{
    LoadError err;
    const auto manifest = parseManifest(manifestText(1, "game"), "extension.json", err);
    EXPECT_FALSE(manifest.has_value());
    EXPECT_EQ(err.field, "/kind");
    EXPECT_NE(err.message.find("version 2"), std::string::npos) << err.message;
}

TEST(GamePackManifest, V1ManifestsAreReadAsBefore)
{
    // v1 keeps its kind unchanged, whatever the string was (the in-tree extensions are "feature" and "compatibility")
    for (const char* kind : { "feature", "compatibility", "whatever" })
    {
        LoadError err;
        const auto manifest = parseManifest(manifestText(1, kind), "extension.json", err);
        ASSERT_TRUE(manifest.has_value()) << kind << ": " << err.toString();
        EXPECT_EQ(manifest->version, 1);
        EXPECT_EQ(manifest->kind, kind);
        EXPECT_EQ(writeCanonicalManifest(*manifest), manifestText(1, kind));
    }
}

TEST(GamePackManifest, FormatVersionThreeIsStillRefused)
{
    LoadError err;
    const auto manifest = parseManifest(manifestText(3, "feature"), "extension.json", err);
    EXPECT_FALSE(manifest.has_value());
    EXPECT_EQ(err.field, "/version");
}

TEST(GamePackManifest, SdkThreeManifestStillParsesSoTheLoaderCanSayWhy)
{
    // The refusal "built for SDK 3.0; this host provides 4.0" is the loader's and resolver's (stage "sdk"), which need
    // the parsed manifest to name the extension.
    std::string text = manifestText(1, "feature");
    const auto pos = text.find("\"major\": 4");
    ASSERT_NE(pos, std::string::npos);
    text.replace(pos, std::string("\"major\": 4").size(), "\"major\": 3");
    LoadError err;
    const auto manifest = parseManifest(text, "extension.json", err);
    ASSERT_TRUE(manifest.has_value()) << err.toString();
    EXPECT_EQ(manifest->sdkVersion, (openeden::sdk::Version{ 3, 0 }));
}

TEST(GamePackManifest, ExtensionBuiltForSdkThreeIsRefusedByTheLoaderNamingBothVersions)
{
    openeden::extension::DiscoveredExtension discovered;
    discovered.manifest.id = "old_extension";
    discovered.manifest.sdkVersion = openeden::sdk::Version{ 3, 0 };
    openeden::sdk::HostInfo hostInfo;
    const auto result = openeden::extension::loadExtensionLibrary(discovered, hostInfo);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.extensionId, "old_extension");
    EXPECT_EQ(result.error.stage, "sdk");
    EXPECT_NE(result.error.message.find("built for SDK 3.0; this host provides 4.0"), std::string::npos)
        << result.error.message;
}

TEST(GamePackManifest, ExtensionBuiltForSdkThreeIsRefusedByTheResolverNamingBothVersions)
{
    openeden::extension::ExtensionManifest old;
    old.id = "old_extension";
    old.sdkVersion = openeden::sdk::Version{ 3, 0 };
    const auto result = openeden::extension::resolveDependencies({ old });
    ASSERT_FALSE(result.errors.empty());
    EXPECT_EQ(result.errors[0].extensionId, "old_extension");
    EXPECT_NE(result.errors[0].message.find("built for SDK 3.0; this host provides 4.0"), std::string::npos)
        << result.errors[0].message;
}
