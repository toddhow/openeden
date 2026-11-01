// Task 0031, a reviewer's finding: the editor library is a file name in the extension's folder, and an error is
// reported at the "editor" key, not at a value that spells the same.
#include <components/extension/manifest_io.hpp>

#include <gtest/gtest.h>

#include <string>

namespace
{
    using openeden::extension::LoadError;
    using openeden::extension::parseManifest;

    std::string manifest(const std::string& id, const std::string& library)
    {
        return std::string("{\n  \"format\": \"openeden.extension\",\n  \"version\": 1,\n  \"id\": \"") + id
            + "\",\n  \"name\": \"Test\",\n  \"extensionVersion\": \"1.0.0\",\n  \"kind\": \"feature\",\n"
              "  \"sdk\": { \"major\": 2, \"minor\": 0 },\n  \"toolchain\": \"\",\n"
              "  \"library\": { \"windows\": \"a.dll\", \"linux\": \"liba.so\" },\n"
              "  \"provides\": [], \"requires\": [], \"optional\": [], \"loadAfter\": [], \"data\": \"data\",\n"
              "  \"settings\": [],\n  \"editor\": { \"library\": \""
            + library + "\", \"editorApi\": { \"major\": 1, \"minor\": 0 } }\n}";
    }

    TEST(ManifestEditorLibraryTest, aLibraryThatLeavesTheExtensionFolderIsRefused)
    {
        for (const char* library : { "../x.dll", "sub/x.dll", "..", "" })
        {
            LoadError error;
            EXPECT_FALSE(parseManifest(manifest("t", library), "extension.json", error)) << library;
            EXPECT_EQ(error.field, "/editor/library") << library;
        }
        LoadError error;
        EXPECT_TRUE(parseManifest(manifest("t", "t_editor.dll"), "extension.json", error));
    }

    TEST(ManifestEditorLibraryTest, theErrorLineIsTheEditorKeyNotAValueThatSpellsTheSame)
    {
        LoadError error;
        // The id is "editor": a search for the text finds it on line 4, the key is on line 13.
        EXPECT_FALSE(parseManifest(manifest("editor", "../x.dll"), "extension.json", error));
        EXPECT_EQ(error.line, 13u);
    }
}
