// Task 0050, stage b: what name normalization does today, stated as tests. The rule (Path::normalize in
// components/vfs/pathutil.hpp): '\' becomes '/', ASCII 'A' to 'Z' becomes lower case, repeated '/' collapse to one,
// one leading '/' is removed. Nothing else happens: '.' and '..' are not resolved, non-ASCII bytes are untouched,
// there is no Unicode normalization, trailing dots and spaces stay, reserved Windows names stay.
// Only the existing Path API and Manager lookups are used, so these tests pass on unchanged code.

#include "vfstestutil.hpp"

#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VFS
{
    namespace
    {
        using namespace testing;
        using namespace VFSTest;

        // Non-ASCII text is written as explicit UTF-8 bytes, split into separate literals so that a hexadecimal
        // escape never swallows the letter after it.
        const std::string eAcuteComposed = "caf\xC3\xA9";
        const std::string eAcuteDecomposed = "cafe\xCC\x81";
        const std::string upperUmlaut
            = "\xC3\x9C"
              "ber"; // "Uber" with U-umlaut
        const std::string lowerUmlaut
            = "\xC3\xBC"
              "ber";

        // ---- normalizeFilename ----

        struct NormalizeCase
        {
            std::string mInput;
            std::string mExpected;
        };

        std::ostream& operator<<(std::ostream& stream, const NormalizeCase& value)
        {
            return stream << "'" << value.mInput << "' -> '" << value.mExpected << "'";
        }

        struct VFSNormalizeFilenameTest : TestWithParam<NormalizeCase>
        {
        };

        TEST_P(VFSNormalizeFilenameTest, shouldGiveExpectedResultAndBeIdempotent)
        {
            const NormalizeCase& param = GetParam();
            const std::string once = Path::normalizeFilename(param.mInput);
            EXPECT_EQ(once, param.mExpected);
            EXPECT_TRUE(Path::isNormalized(once));
            EXPECT_EQ(Path::normalizeFilename(once), once);
            EXPECT_EQ(Path::Normalized(param.mInput).value(), once);
        }

        const NormalizeCase normalizeCases[] = {
            { "", "" },
            { "foo", "foo" },
            { "Foo/Bar.NIF", "foo/bar.nif" },
            { "Foo\\Bar\\Baz.nif", "foo/bar/baz.nif" },
            { "foo//bar", "foo/bar" },
            { "foo///bar////baz", "foo/bar/baz" },
            { "foo\\\\bar", "foo/bar" },
            { "foo\\/bar", "foo/bar" },
            { "/foo", "foo" },
            { "\\foo", "foo" },
            { "//foo", "foo" },
            { "/", "" },
            { "//", "" },
            { "foo/", "foo/" },
            { "foo//", "foo/" },
            // "." and ".." are plain names: nothing is resolved.
            { ".", "." },
            { "..", ".." },
            { "./foo", "./foo" },
            { "foo/./bar", "foo/./bar" },
            { "foo/../bar", "foo/../bar" },
            { "../foo", "../foo" },
            { "foo/..", "foo/.." },
            { "FOO/../BAR", "foo/../bar" },
            // Absolute looking names are plain names with a colon or a UNC prefix that loses one slash.
            { "C:\\Windows\\System32", "c:/windows/system32" },
            { "c:/x", "c:/x" },
            { "\\\\server\\share\\f.txt", "server/share/f.txt" },
            // Trailing dots and spaces, reserved Windows names: untouched by normalization.
            { "trailing.", "trailing." },
            { "trailing ", "trailing " },
            { "dir./file", "dir./file" },
            { "CON", "con" },
            { "Con.TXT", "con.txt" },
            { "NUL.", "nul." },
            { "LPT1/aux", "lpt1/aux" },
            { " leading space", " leading space" },
        };

        INSTANTIATE_TEST_SUITE_P(NormalizeCases, VFSNormalizeFilenameTest, ValuesIn(normalizeCases));

        TEST(VFSNormalizationTest, nonAsciiBytesShouldBeUnchangedButAsciiLettersAreLowered)
        {
            EXPECT_EQ(Path::normalizeFilename(eAcuteComposed), eAcuteComposed);
            EXPECT_EQ(Path::normalizeFilename(eAcuteDecomposed), eAcuteDecomposed);
            // Upper case outside ASCII is not lowered: U-umlaut and u-umlaut stay different names.
            EXPECT_EQ(Path::normalizeFilename(upperUmlaut), upperUmlaut);
            EXPECT_NE(Path::normalizeFilename(upperUmlaut), Path::normalizeFilename(lowerUmlaut));
            EXPECT_EQ(Path::normalizeFilename("Dir\\" + upperUmlaut + "\\X.TXT"), "dir/" + upperUmlaut + "/x.txt");
        }

        TEST(VFSNormalizationTest, composedAndDecomposedAccentsShouldBeDifferentKeys)
        {
            // No Unicode normalization (decision D2 of task 0050): "e" + combining accent is not the precomposed
            // character.
            ASSERT_NE(eAcuteComposed, eAcuteDecomposed);
            EXPECT_NE(Path::Normalized(eAcuteComposed), Path::Normalized(eAcuteDecomposed));
            EXPECT_NE(Path::Normalized(eAcuteComposed).value(), Path::Normalized(eAcuteDecomposed).value());
        }

        TEST(VFSNormalizationTest, everyByteValueShouldKeepItsLengthOrBeAFoldedSeparator)
        {
            // A name made of all byte values 1 to 255 (no NUL, which std::string would allow but file names cannot
            // contain): normalization changes only 'A'-'Z' (lowered) and '\' (to '/') and then may collapse '/' runs
            // and drop one leading '/'.
            std::string all;
            for (int c = 1; c < 256; ++c)
                all.push_back(static_cast<char>(c));
            const std::string normalized = Path::normalizeFilename(all);
            EXPECT_TRUE(Path::isNormalized(normalized));
            EXPECT_EQ(Path::normalizeFilename(normalized), normalized);
            for (char c : normalized)
            {
                EXPECT_FALSE(c >= 'A' && c <= 'Z');
                EXPECT_NE(c, '\\');
            }
            // The high bytes (0x80..0xFF) are all still there, in order.
            std::string high;
            for (char c : normalized)
                if (static_cast<unsigned char>(c) >= 0x80)
                    high.push_back(c);
            std::string expectedHigh;
            for (int c = 0x80; c < 256; ++c)
                expectedHigh.push_back(static_cast<char>(c));
            EXPECT_EQ(high, expectedHigh);
        }

        TEST(VFSNormalizationTest, veryLongNamesShouldBeNormalizedWithoutLimit)
        {
            const std::string longName(5000, 'A');
            const std::string expected(5000, 'a');
            EXPECT_EQ(Path::normalizeFilename(longName), expected);
            EXPECT_EQ(Path::Normalized(longName).value(), expected);

            std::string deep;
            for (int i = 0; i < 1500; ++i)
                deep += "D\\";
            deep += "File.TXT";
            const std::string normalized = Path::normalizeFilename(deep);
            EXPECT_EQ(normalized.size(), deep.size());
            EXPECT_EQ(normalized.substr(normalized.size() - 8), "file.txt");
            EXPECT_TRUE(Path::isNormalized(normalized));
        }

        // ---- isNormalized, Normalized, NormalizedView ----

        TEST(VFSNormalizationTest, isNormalizedShouldRejectNamesNormalizationWouldChange)
        {
            EXPECT_TRUE(Path::isNormalized(""));
            EXPECT_TRUE(Path::isNormalized("a/b.c"));
            EXPECT_TRUE(Path::isNormalized("a/"));
            EXPECT_TRUE(Path::isNormalized("../a"));
            EXPECT_TRUE(Path::isNormalized("c:/x"));
            EXPECT_TRUE(Path::isNormalized(eAcuteComposed));
            EXPECT_TRUE(Path::isNormalized(upperUmlaut));
            EXPECT_FALSE(Path::isNormalized("A"));
            EXPECT_FALSE(Path::isNormalized("a\\b"));
            EXPECT_FALSE(Path::isNormalized("a//b"));
            EXPECT_FALSE(Path::isNormalized("/a"));
            EXPECT_FALSE(Path::isNormalized("/"));
        }

        TEST(VFSNormalizationTest, normalizedViewShouldRefuseNamesThatAreNotNormalized)
        {
            EXPECT_THROW(Path::NormalizedView("Foo"), std::invalid_argument);
            EXPECT_THROW(Path::NormalizedView("foo\\bar"), std::invalid_argument);
            EXPECT_THROW(Path::NormalizedView("/foo"), std::invalid_argument);
            EXPECT_THROW(Path::NormalizedView("foo//bar"), std::invalid_argument);
            EXPECT_NO_THROW(Path::NormalizedView("foo/bar"));
            EXPECT_NO_THROW(Path::NormalizedView(""));
            EXPECT_TRUE(Path::NormalizedView().empty());
        }

        TEST(VFSNormalizationTest, normalizedShouldAlwaysHoldANormalizedValue)
        {
            const std::vector<std::string> inputs = { "", "A", "A\\B", "//a//b//", "../X", "Con.TXT", eAcuteComposed,
                upperUmlaut + "\\Dir", std::string(300, 'Q') };
            for (const std::string& input : inputs)
            {
                const Path::Normalized value(input);
                EXPECT_TRUE(Path::isNormalized(value.view())) << input;
                // A view of a normalized value, and a normalized value made from a view, are the same text.
                const Path::NormalizedView view(value);
                EXPECT_EQ(view.value(), value.value()) << input;
                EXPECT_EQ(Path::Normalized(view), value) << input;
                // Normalizing again changes nothing.
                EXPECT_EQ(Path::Normalized(value.value()), value) << input;
                EXPECT_EQ(Path::Normalized(std::string(value.value())), value) << input;
            }
        }

        TEST(VFSNormalizationTest, normalizedFromRvalueStringShouldGiveTheSameResultAsFromView)
        {
            for (const char* input : { "A\\B//C", "/x", "X/", "", "..\\.." })
            {
                std::string copy(input);
                EXPECT_EQ(Path::Normalized(std::move(copy)), Path::Normalized(std::string_view(input))) << input;
            }
        }

        TEST(VFSNormalizationTest, pathPartsShouldBeTakenFromTheNormalizedText)
        {
            const Path::Normalized value("Meshes\\Dir\\Foo.Bar.NIF");
            EXPECT_EQ(value.value(), "meshes/dir/foo.bar.nif");
            EXPECT_EQ(value.parent().value(), "meshes/dir");
            EXPECT_EQ(value.filename().value(), "foo.bar.nif");
            EXPECT_EQ(value.stem(), "foo.bar");
            EXPECT_EQ(value.extension().value(), "nif");
            EXPECT_EQ(Path::Normalized("noext").extension().value(), "");
            EXPECT_EQ(Path::Normalized("noext").parent().value(), "");
        }

        TEST(VFSNormalizationTest, appendedNamesShouldBeNormalizedAndKeepDotComponents)
        {
            Path::Normalized joined("Meshes");
            joined /= std::string_view("..\\Dir\\X");
            EXPECT_EQ(joined.value(), "meshes/../dir/x");
            EXPECT_TRUE(Path::isNormalized(joined.view()));
        }

        // ---- Manager lookups ----

        struct VFSNormalizationLookupTest : Test
        {
            std::vector<std::unique_ptr<TestingOpenMW::VFSTestFile>> mFiles;
            std::unique_ptr<Manager> mVfs;
            std::map<std::string, std::string> mContents;

            void add(FileMap& map, const std::string& rawName, const std::string& content)
            {
                mFiles.push_back(std::make_unique<TestingOpenMW::VFSTestFile>(content));
                map[Path::Normalized(rawName)] = mFiles.back().get();
                mContents[Path::normalizeFilename(rawName)] = content;
            }

            void SetUp() override
            {
                FileMap map;
                add(map, "Meshes\\A.NIF", "a");
                add(map, "dir/sub/file.txt", "file");
                add(map, eAcuteComposed + ".txt", "composed");
                add(map, eAcuteDecomposed + ".txt", "decomposed");
                add(map, "CON.TXT", "con");
                add(map, "Trailing.", "dot");
                add(map, "trailing ", "space");
                add(map, "c:/drive/x.txt", "drive");
                add(map, std::string(5000, 'L') + ".bin", "long");
                mVfs = TestingOpenMW::createTestVFS(std::move(map));
            }

            bool has(std::string_view rawName) const { return mVfs->exists(Path::Normalized(rawName)); }
        };

        TEST_F(VFSNormalizationLookupTest, caseAndSlashVariantsShouldFindTheSameFile)
        {
            for (const char* name : { "meshes/a.nif", "MESHES/A.NIF", "Meshes\\A.nif", "meshes//a.nif", "/meshes/a.nif",
                     "\\meshes\\a.nif", "//Meshes///A.NIF" })
            {
                EXPECT_TRUE(has(name)) << name;
                EXPECT_EQ(contentOf(*mVfs, name), "a") << name;
            }
        }

        TEST_F(VFSNormalizationLookupTest, emptyNameShouldNotBeFound)
        {
            EXPECT_FALSE(has(""));
            EXPECT_FALSE(has("/"));
            EXPECT_EQ(mVfs->find(Path::NormalizedView("")), nullptr);
            EXPECT_THROW(mVfs->get(Path::Normalized("")), std::runtime_error);
        }

        TEST_F(VFSNormalizationLookupTest, dotComponentsShouldNotBeResolved)
        {
            // "." and ".." are not interpreted: such a name is a different key and simply misses.
            EXPECT_FALSE(has("meshes/./a.nif"));
            EXPECT_FALSE(has("./meshes/a.nif"));
            EXPECT_FALSE(has("meshes/../meshes/a.nif"));
            EXPECT_FALSE(has("dir/sub/../sub/file.txt"));
            EXPECT_FALSE(has("../meshes/a.nif"));
            EXPECT_FALSE(has(".."));
            EXPECT_FALSE(has("."));
            EXPECT_EQ(mVfs->find(Path::Normalized("meshes/../meshes/a.nif")), nullptr);
            EXPECT_THROW(mVfs->get(Path::Normalized("dir/../dir/sub/file.txt")), std::runtime_error);
            EXPECT_THAT(listNames(*mVfs, ".."), IsEmpty());
            EXPECT_THAT(listNames(*mVfs, "dir/../dir"), IsEmpty());
        }

        TEST_F(VFSNormalizationLookupTest, absoluteLookingNamesShouldOnlyMatchStoredKeys)
        {
            EXPECT_TRUE(has("C:\\Drive\\X.TXT")); // stored as the key "c:/drive/x.txt"
            EXPECT_FALSE(has("c:/meshes/a.nif"));
            EXPECT_FALSE(has("c:\\dir\\sub\\file.txt"));
            EXPECT_FALSE(has("/etc/passwd"));
            EXPECT_FALSE(has("\\\\server\\share\\a.nif"));
            // A leading slash is dropped, so this is the relative name.
            EXPECT_TRUE(has("/dir/sub/file.txt"));
        }

        TEST_F(VFSNormalizationLookupTest, veryLongNamesShouldBeFoundAndMissedLikeShortOnes)
        {
            EXPECT_TRUE(has(std::string(5000, 'l') + ".bin"));
            EXPECT_TRUE(has(std::string(5000, 'L') + ".BIN"));
            EXPECT_FALSE(has(std::string(5001, 'l') + ".bin"));
            EXPECT_FALSE(has(std::string(5000, 'x')));
            EXPECT_FALSE(has(std::string(100000, 'q')));
            EXPECT_EQ(contentOf(*mVfs, std::string(5000, 'l') + ".bin"), "long");
        }

        TEST_F(VFSNormalizationLookupTest, nonAsciiBytesShouldMatchExactly)
        {
            EXPECT_EQ(contentOf(*mVfs, eAcuteComposed + ".txt"), "composed");
            EXPECT_EQ(contentOf(*mVfs, "CAF" + std::string("\xC3\xA9") + ".TXT"), "composed");
            EXPECT_EQ(contentOf(*mVfs, eAcuteDecomposed + ".txt"), "decomposed");
            EXPECT_EQ(contentOf(*mVfs, "CAFE" + std::string("\xCC\x81") + ".TXT"), "decomposed");
            // Different bytes, different keys.
            EXPECT_NE(contentOf(*mVfs, eAcuteComposed + ".txt"), contentOf(*mVfs, eAcuteDecomposed + ".txt"));
            // Non-ASCII upper case is not folded, so a different spelling of the accent misses.
            EXPECT_FALSE(has("caf\xC3\x89.txt"));
            EXPECT_FALSE(has("caf\xC3\xA8.txt"));
        }

        TEST_F(VFSNormalizationLookupTest, trailingDotsSpacesAndReservedNamesShouldBeLeftAlone)
        {
            EXPECT_EQ(contentOf(*mVfs, "TRAILING."), "dot");
            EXPECT_EQ(contentOf(*mVfs, "trailing "), "space");
            EXPECT_FALSE(has("trailing")); // the dot and the space are part of the names, not trimmed
            EXPECT_EQ(contentOf(*mVfs, "Con.Txt"), "con");
            EXPECT_FALSE(has("con"));
            EXPECT_FALSE(has("nul.txt"));
        }

        TEST_F(VFSNormalizationLookupTest, directoryRangeShouldNormalizeItsPrefixToo)
        {
            EXPECT_THAT(listNames(*mVfs, "DIR\\SUB"), ElementsAre("dir/sub/file.txt"));
            EXPECT_THAT(listNames(*mVfs, "/dir"), ElementsAre("dir/sub/file.txt"));
            EXPECT_THAT(listNames(*mVfs, "dir//sub"), ElementsAre("dir/sub/file.txt"));
            EXPECT_THAT(listNames(*mVfs, "meshes/./"), IsEmpty());
        }

        TEST_F(VFSNormalizationLookupTest, everyStoredKeyShouldBeNormalizedAndFindableByItsOwnName)
        {
            const std::vector<std::string> all = listAllNames(*mVfs);
            EXPECT_EQ(all.size(), mContents.size());
            for (const std::string& name : all)
            {
                EXPECT_TRUE(Path::isNormalized(name)) << name.substr(0, 40);
                EXPECT_TRUE(has(name)) << name.substr(0, 40);
                EXPECT_EQ(contentOf(*mVfs, name), mContents.at(name)) << name.substr(0, 40);
            }
        }

        // ---- archives with odd names ----

        TEST(VFSNormalizationArchiveTest, archiveNamesShouldBeNormalizedWhenIndexed)
        {
            ScratchDir temp;
            writeBsa(temp.path() / "odd.bsa",
                { { "Meshes\\\\Double.NIF", "double" }, { "\\Leading.nif", "leading" },
                    { "UPPER/Mixed\\Path.TXT", "mixed" }, { "Dots\\..\\Up.nif", "dots" } });

            const std::unique_ptr<Archive> archive = makeBsaArchive(temp.path() / "odd.bsa", nullptr);
            const FileMap files = listFiles(*archive);

            EXPECT_TRUE(files.contains(Path::NormalizedView("meshes/double.nif")));
            EXPECT_TRUE(files.contains(Path::NormalizedView("leading.nif")));
            EXPECT_TRUE(files.contains(Path::NormalizedView("upper/mixed/path.txt")));
            // ".." inside an archive name is kept as a component, not resolved.
            EXPECT_TRUE(files.contains(Path::NormalizedView("dots/../up.nif")));
            EXPECT_FALSE(files.contains(Path::NormalizedView("up.nif")));
            EXPECT_EQ(files.size(), 4u);
        }

        TEST(VFSNormalizationArchiveTest, looseFilesOnDiskShouldBeIndexedByTheirRelativeNormalizedPath)
        {
            ScratchDir temp;
            writeFile(temp.path() / "Textures" / "Sub Dir" / "My File.DDS", "x");

            FileSystemArchive archive(temp.path());
            const FileMap files = listFiles(archive);

            EXPECT_TRUE(files.contains(Path::NormalizedView("textures/sub dir/my file.dds")));
            EXPECT_EQ(files.size(), 1u);
        }
    }
}
