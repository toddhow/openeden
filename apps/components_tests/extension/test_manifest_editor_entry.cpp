// Task 0031, stage A: the optional "editor" entry of extension.json, written from the spec alone.
//
// Assumptions: ExtensionManifest::editor is std::optional<EditorEntry> with members `library` (std::string) and
// `editorApi` (openeden::sdk::Version); the type is declared in components/extension/manifest.hpp. The canonical writer
// writes the entry after "licence", as an object with "library" then "editorApi" ({"major", "minor"}), indented with
// two spaces like the rest. Error fields are JSON pointers under "/editor".
#include <components/extension/load_error.hpp>
#include <components/extension/manifest.hpp>
#include <components/extension/manifest_io.hpp>
#include <openeden/version.hpp>

#include <gtest/gtest.h>

#include <string>

namespace
{
    using openeden::extension::LoadError;
    using openeden::extension::parseManifest;
    using openeden::extension::writeCanonicalManifest;
    using openeden::sdk::Version;

    const std::string kHead = R"({
  "format": "openeden.extension",
  "version": 1,
  "id": "studio_tool",
  "name": "Studio Tool",
  "extensionVersion": "1.0.0",
  "kind": "feature",
  "sdk": {
    "major": 2,
    "minor": 0
  },
  "toolchain": "",
  "library": {
    "windows": "studio_tool.dll",
    "linux": "libstudio_tool.so"
  },
  "provides": [],
  "requires": [],
  "optional": [],
  "loadAfter": [],
  "data": "data",
  "settings": [],
  "licence": "GPL-3.0-or-later")";

    const std::string kEditorCanonical = R"(,
  "editor": {
    "library": "studio_tool_editor.dll",
    "editorApi": {
      "major": 1,
      "minor": 0
    }
  }
})";

    // The "editor" key starts on this line of the text withEditor() builds (kHead has 23 lines).
    constexpr std::size_t kEditorLine = 24;

    std::string withEditor(const std::string& editorValueJson)
    {
        return kHead + ",\n  \"editor\": " + editorValueJson + "\n}";
    }

    void expectRejected(const std::string& editorValueJson, const std::string& expectedFieldPrefix = "/editor")
    {
        LoadError err;
        const auto manifest = parseManifest(withEditor(editorValueJson), "extension.json", err);
        EXPECT_FALSE(manifest.has_value()) << editorValueJson;
        EXPECT_EQ(err.file, "extension.json") << editorValueJson;
        EXPECT_EQ(err.field.rfind(expectedFieldPrefix, 0), 0u)
            << "field was '" << err.field << "' for " << editorValueJson;
        EXPECT_FALSE(err.message.empty()) << editorValueJson;
        EXPECT_GE(err.line, kEditorLine) << editorValueJson;
        EXPECT_NE(err.toString().find("extension.json"), std::string::npos);
    }
}

TEST(ManifestEditorEntryTest, ParsesTheEditorEntry)
{
    const std::string text = kHead + kEditorCanonical;
    LoadError err;
    const auto manifest = parseManifest(text, "extension.json", err);
    ASSERT_TRUE(manifest.has_value()) << err.toString();
    ASSERT_TRUE(manifest->editor.has_value());
    EXPECT_EQ(manifest->editor->library, "studio_tool_editor.dll");
    EXPECT_EQ(manifest->editor->editorApi, (Version{ 1, 0 }));
    // The rest of the manifest is read as before.
    EXPECT_EQ(manifest->id, "studio_tool");
    EXPECT_EQ(manifest->sdkVersion, (Version{ 2, 0 }));
}

TEST(ManifestEditorEntryTest, ParsesAnEntryWrittenCompactly)
{
    LoadError err;
    const auto manifest = parseManifest(
        withEditor(R"({ "library": "x.dll", "editorApi": { "major": 1, "minor": 3 } })"), "extension.json", err);
    ASSERT_TRUE(manifest.has_value()) << err.toString();
    ASSERT_TRUE(manifest->editor.has_value());
    EXPECT_EQ(manifest->editor->library, "x.dll");
    EXPECT_EQ(manifest->editor->editorApi, (Version{ 1, 3 }));
}

TEST(ManifestEditorEntryTest, AManifestWithoutTheEntryHasNone)
{
    LoadError err;
    const auto manifest = parseManifest(kHead + "\n}", "extension.json", err);
    ASSERT_TRUE(manifest.has_value()) << err.toString();
    EXPECT_FALSE(manifest->editor.has_value());
}

TEST(ManifestEditorEntryTest, CanonicalRoundTripIsByteForByteWithTheEntry)
{
    const std::string canonical = kHead + kEditorCanonical;
    LoadError err;
    const auto manifest = parseManifest(canonical, "extension.json", err);
    ASSERT_TRUE(manifest.has_value()) << err.toString();

    const std::string written = writeCanonicalManifest(*manifest);
    EXPECT_EQ(written, canonical);

    LoadError err2;
    const auto again = parseManifest(written, "extension.json", err2);
    ASSERT_TRUE(again.has_value()) << err2.toString();
    EXPECT_EQ(writeCanonicalManifest(*again), canonical);
}

TEST(ManifestEditorEntryTest, CanonicalRoundTripIsByteForByteWithoutTheEntryAndWritesNoEditorKey)
{
    const std::string canonical = kHead + "\n}";
    LoadError err;
    const auto manifest = parseManifest(canonical, "extension.json", err);
    ASSERT_TRUE(manifest.has_value()) << err.toString();

    const std::string written = writeCanonicalManifest(*manifest);
    EXPECT_EQ(written, canonical);
    EXPECT_EQ(written.find("editor"), std::string::npos);
}

TEST(ManifestEditorEntryTest, ANonCanonicalEntryIsWrittenCanonically)
{
    LoadError err;
    const auto manifest = parseManifest(
        withEditor(R"({"editorApi":{"minor":0,"major":1},"library":"studio_tool_editor.dll"})"), "extension.json", err);
    ASSERT_TRUE(manifest.has_value()) << err.toString();
    EXPECT_EQ(writeCanonicalManifest(*manifest), kHead + kEditorCanonical);
}

TEST(ManifestEditorEntryTest, EditorMustBeAnObject)
{
    expectRejected("\"studio_tool_editor.dll\"");
    expectRejected("[]");
    expectRejected("42");
    expectRejected("true");
    expectRejected("null");
}

TEST(ManifestEditorEntryTest, LibraryIsRequiredAndMustBeAString)
{
    expectRejected(R"({ "editorApi": { "major": 1, "minor": 0 } })");
    expectRejected(R"({ "library": 5, "editorApi": { "major": 1, "minor": 0 } })");
    expectRejected(R"({ "library": ["a.dll"], "editorApi": { "major": 1, "minor": 0 } })");
    expectRejected(R"({ "library": null, "editorApi": { "major": 1, "minor": 0 } })");
}

TEST(ManifestEditorEntryTest, MissingLibraryNamesTheLibraryField)
{
    LoadError err;
    EXPECT_FALSE(parseManifest(withEditor(R"({ "editorApi": { "major": 1, "minor": 0 } })"), "extension.json", err));
    const std::string where = err.field + " " + err.message;
    EXPECT_NE(where.find("library"), std::string::npos) << where;
}

TEST(ManifestEditorEntryTest, EditorApiIsRequiredAndMustBeWellFormed)
{
    expectRejected(R"({ "library": "x.dll" })");
    expectRejected(R"({ "library": "x.dll", "editorApi": "1.0" })");
    expectRejected(R"({ "library": "x.dll", "editorApi": [1, 0] })");
    expectRejected(R"({ "library": "x.dll", "editorApi": null })");
    expectRejected(R"({ "library": "x.dll", "editorApi": {} })");
    expectRejected(R"({ "library": "x.dll", "editorApi": { "major": 1 } })");
    expectRejected(R"({ "library": "x.dll", "editorApi": { "minor": 0 } })");
    expectRejected(R"({ "library": "x.dll", "editorApi": { "major": "1", "minor": 0 } })");
    expectRejected(R"({ "library": "x.dll", "editorApi": { "major": 1, "minor": -1 } })");
    expectRejected(R"({ "library": "x.dll", "editorApi": { "major": 1.5, "minor": 0 } })");
}

TEST(ManifestEditorEntryTest, MissingEditorApiNamesTheEditorApiField)
{
    LoadError err;
    EXPECT_FALSE(parseManifest(withEditor(R"({ "library": "x.dll" })"), "extension.json", err));
    const std::string where = err.field + " " + err.message;
    EXPECT_NE(where.find("editorApi"), std::string::npos) << where;
}

TEST(ManifestEditorEntryTest, UnknownKeysInsideEditorAreRejected)
{
    const std::string entry = R"({ "library": "x.dll", "editorApi": { "major": 1, "minor": 0 }, "extra": true })";
    expectRejected(entry);
    LoadError err;
    EXPECT_FALSE(parseManifest(withEditor(entry), "extension.json", err));
    const std::string where = err.field + " " + err.message;
    EXPECT_NE(where.find("extra"), std::string::npos) << where;
}

TEST(ManifestEditorEntryTest, ErrorLineIsInsideTheEditorEntry)
{
    LoadError err;
    EXPECT_FALSE(parseManifest(
        withEditor("{\n    \"library\": 5,\n    \"editorApi\": { \"major\": 1, \"minor\": 0 }\n  }"), "extension.json",
        err));
    EXPECT_GE(err.line, kEditorLine);
}
