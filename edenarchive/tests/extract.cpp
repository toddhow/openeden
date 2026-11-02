// Eden Archive unit tests (task 0052). Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
// Extraction: specification section 15.4 (stricter rules for writing entries to a disk folder).
#include <gtest/gtest.h>

#include <edenarchive/extract.hpp>

#include "testhelp.hpp"

namespace EdenTestSuite
{
    namespace
    {
        using EdenArchive::Archive;
        using EdenArchive::Error;
        using EdenArchive::UnpackOptions;
        using EdenTest::EntrySpec;
        using EdenTest::PackSpec;

        // ---- checkExtractionSafe ----

        TEST(EdenArchiveExtract, namesThatAreFineOnWindowsAndLinuxShouldBeAccepted)
        {
            for (const char* path : { "a.txt", "dir/file.txt", "a/b/c.d", ".hidden", ".git/config", "a.b.c", "x-y_z", "a$b",
                     "a b/c d.txt", "console.txt", "con1", "conx/y", "com0", "com10", "lpt0", "lpt10", "comx", "aux1.txt",
                     "nul-x", "prn_", "clock", "conin", "\xC3\xA4.txt", "\xE6\x97\xA5/\xE6\x9C\xAC", "a.b/c.d", "a...b",
                     "co", "COMMON/x", "data/com/x", "luna/x" })
                EXPECT_FALSE(EdenArchive::checkExtractionSafe(path).has_value()) << path;
        }

        TEST(EdenArchiveExtract, forbiddenCharactersShouldBeRefusedInAnyComponent)
        {
            for (const char* path : { "a:b", "a<b", "a>b", "a\"b", "a|b", "a?b", "a*b", "a\\b", "dir/a:b/c", "dir/x?", "*.txt",
                     "c:/windows/x", "c:x", "d:\\x", "dir/sub:stream", "file.txt:hidden" })
                EXPECT_TRUE(EdenArchive::checkExtractionSafe(path).has_value()) << path;
        }

        TEST(EdenArchiveExtract, componentsEndingInADotOrASpaceShouldBeRefused)
        {
            for (const char* path : { "a.", "a./b", "dir/x.", "dir/x./y", "a ", "a /b", "x/y ", "x/y  ", "...", "dir/..." })
                EXPECT_TRUE(EdenArchive::checkExtractionSafe(path).has_value()) << path;
        }

        TEST(EdenArchiveExtract, windowsDeviceNamesShouldBeRefusedWhateverTheCaseOrExtension)
        {
            std::vector<std::string> names = { "con", "prn", "aux", "nul", "conin$", "conout$", "clock$" };
            for (char digit = '1'; digit <= '9'; ++digit)
            {
                names.push_back(std::string("com") + digit);
                names.push_back(std::string("lpt") + digit);
            }
            names.push_back("com\xC2\xB9"); // superscript one, two and three (U+00B9, U+00B2, U+00B3)
            names.push_back("com\xC2\xB2");
            names.push_back("com\xC2\xB3");
            names.push_back("lpt\xC2\xB9");
            names.push_back("lpt\xC2\xB2");
            names.push_back("lpt\xC2\xB3");

            for (const std::string& name : names)
            {
                std::string upper = name;
                for (char& c : upper)
                    c = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
                for (const std::string& variant : { name, upper, name + ".txt", upper + ".txt", name + ".tar.gz",
                         "dir/" + name, "dir/" + name + "/inner", name + " .txt", name + "  " })
                    EXPECT_TRUE(EdenArchive::checkExtractionSafe(variant).has_value()) << variant;
            }
            // Names that only begin like a device name are fine.
            for (const char* path : { "con1x", "console", "aux_", "nulx.txt", "com", "lpt", "com12", "xcom1", "a.com1" })
                EXPECT_FALSE(EdenArchive::checkExtractionSafe(path).has_value()) << path;
        }

        TEST(EdenArchiveExtract, traversalAndAbsoluteNamesShouldBeRefusedEvenThoughStoredNamesCannotHaveThem)
        {
            // Section 7.3.1 keeps these out of a pack, but a tool must never rely on that alone.
            for (const std::string& path : { std::string(".."), std::string("../x"), std::string("a/../b"), std::string("a/.."),
                     std::string("/etc/passwd"), std::string("/"), std::string(""), std::string("a//b"),
                     std::string("a\x01" "b"), std::string("a\0b", 3) })
                EXPECT_TRUE(EdenArchive::checkExtractionSafe(path).has_value()) << path;
        }

        TEST(EdenArchiveExtract, theReasonShouldBeShortAndNotEmpty)
        {
            const auto reason = EdenArchive::checkExtractionSafe("nul.txt");
            ASSERT_TRUE(reason.has_value());
            EXPECT_FALSE(reason->empty());
            EXPECT_LT(reason->size(), 200u);
        }

        // ---- unpack ----

        EntrySpec file(std::string path, Bytes content)
        {
            EntrySpec entry;
            entry.mPath = std::move(path);
            entry.mContent = std::move(content);
            return entry;
        }

        struct Opened
        {
            Bytes mBytes;
            std::unique_ptr<Archive> mArchive;
        };

        /// Builds a pack from entries and opens it (the Archive keeps its own copy of the bytes).
        Opened openPack(std::vector<EntrySpec> entries, std::uint32_t chunkSize = 4096)
        {
            PackSpec spec;
            spec.mEntries = std::move(entries);
            spec.mChunkSize = chunkSize;
            Opened out;
            out.mBytes = EdenTest::buildPack(spec);
            Error error;
            out.mArchive = openBytes(out.mBytes, error);
            return out;
        }

        bool anyRegularFile(const std::filesystem::path& root)
        {
            std::error_code ec;
            if (!std::filesystem::exists(root, ec))
                return false;
            for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
            {
                if (it->is_regular_file())
                    return true;
            }
            return false;
        }

        TEST(EdenArchiveExtract, unpackShouldWriteEveryPathEntryAndNothingElse)
        {
            TempDir dir;
            const auto sentinel = dir.path() / "sentinel.txt";
            ASSERT_TRUE(writeFileBytes(sentinel, EdenTest::bytesOf("untouched")));
            const auto out = dir.path() / "out";

            std::vector<EntrySpec> entries = { file("a.txt", EdenTest::bytesOf("alpha")),
                file("dir/b.bin", EdenTest::patternBytes(300, 1)), file("dir/sub/c.txt", EdenTest::textBytes(5000)),
                file("empty.txt", {}), file("big.bin", EdenTest::patternBytes(20000, 2)) };
            EntrySpec keyOnly;
            keyOnly.mKey = 99;
            keyOnly.mContent = EdenTest::bytesOf("key only entries have no file name");
            entries.push_back(keyOnly);
            Opened pack = openPack(entries);
            ASSERT_NE(pack.mArchive, nullptr);

            const Error error = EdenArchive::unpack(*pack.mArchive, out, UnpackOptions{});
            ASSERT_TRUE(error.ok()) << error.describe();

            EXPECT_EQ(readFileBytes(out / "a.txt"), EdenTest::bytesOf("alpha"));
            EXPECT_EQ(readFileBytes(out / "dir" / "b.bin"), EdenTest::patternBytes(300, 1));
            EXPECT_EQ(readFileBytes(out / "dir" / "sub" / "c.txt"), EdenTest::textBytes(5000));
            EXPECT_TRUE(std::filesystem::is_regular_file(out / "empty.txt"));
            EXPECT_EQ(std::filesystem::file_size(out / "empty.txt"), 0u);
            EXPECT_EQ(readFileBytes(out / "big.bin"), EdenTest::patternBytes(20000, 2));
            EXPECT_EQ(listTree(out),
                (std::vector<std::string>{ "a.txt", "big.bin", "dir", "dir/b.bin", "dir/sub", "dir/sub/c.txt", "empty.txt" }))
                << "only the path entries, no temporary files";
            // Nothing outside the output folder.
            std::vector<std::string> top;
            for (const auto& item : std::filesystem::directory_iterator(dir.path()))
                top.push_back(item.path().filename().string());
            std::sort(top.begin(), top.end());
            EXPECT_EQ(top, (std::vector<std::string>{ "out", "sentinel.txt" }));
            EXPECT_EQ(readFileBytes(sentinel), EdenTest::bytesOf("untouched"));
        }

        TEST(EdenArchiveExtract, unpackShouldRefuseToOverwriteUnlessToldTo)
        {
            TempDir dir;
            const auto out = dir.path() / "out";
            Opened pack = openPack({ file("a.txt", EdenTest::bytesOf("from the pack")), file("b.txt", EdenTest::bytesOf("second")) });
            ASSERT_NE(pack.mArchive, nullptr);
            ASSERT_TRUE(EdenArchive::unpack(*pack.mArchive, out, UnpackOptions{}).ok());

            ASSERT_TRUE(writeFileBytes(out / "a.txt", EdenTest::bytesOf("edited by hand")));
            const Error refused = EdenArchive::unpack(*pack.mArchive, out, UnpackOptions{});
            EXPECT_EQ(className(refused), "E_IO");
            EXPECT_EQ(readFileBytes(out / "a.txt"), EdenTest::bytesOf("edited by hand"));
            EXPECT_EQ(listTree(out), (std::vector<std::string>{ "a.txt", "b.txt" })) << "no temporary file left behind";

            UnpackOptions overwrite;
            overwrite.mOverwrite = true;
            ASSERT_TRUE(EdenArchive::unpack(*pack.mArchive, out, overwrite).ok());
            EXPECT_EQ(readFileBytes(out / "a.txt"), EdenTest::bytesOf("from the pack"));
            EXPECT_EQ(listTree(out), (std::vector<std::string>{ "a.txt", "b.txt" }));
        }

        TEST(EdenArchiveExtract, unpackShouldCheckEveryNameBeforeWritingTheFirstFile)
        {
            // Names that the container allows but a disk folder must not receive.
            for (const char* bad : { "con.txt", "c:/evil.txt", "a?b", "trailing.", "aux", "dir/lpt1.txt", "x:y", "q*", "sp ace ",
                     "w/nul" })
            {
                SCOPED_TRACE(bad);
                TempDir dir;
                const auto out = dir.path() / "out";
                // "aaa.txt" sorts first and is fine: it must not be written either.
                Opened pack = openPack({ file("aaa.txt", EdenTest::bytesOf("fine")), file(bad, EdenTest::bytesOf("bad")) });
                ASSERT_NE(pack.mArchive, nullptr) << "the container accepts this name";
                const Error error = EdenArchive::unpack(*pack.mArchive, out, UnpackOptions{});
                EXPECT_EQ(className(error), "E_NAME");
                EXPECT_FALSE(anyRegularFile(out)) << "a refused unpack wrote a file";
                EXPECT_FALSE(anyRegularFile(dir.path()));
            }
        }

        TEST(EdenArchiveExtract, aCallerSuppliedNameCheckShouldReplaceTheDefault)
        {
            TempDir dir;
            const auto out = dir.path() / "out";
            Opened pack = openPack({ file("no/x.txt", EdenTest::bytesOf("x")), file("yes/y.txt", EdenTest::bytesOf("y")) });
            ASSERT_NE(pack.mArchive, nullptr);
            UnpackOptions options;
            std::size_t calls = 0;
            options.mNameCheck = [&calls](std::string_view path) -> std::optional<std::string> {
                ++calls;
                if (path.starts_with("no/"))
                    return "not allowed here";
                return std::nullopt;
            };
            const Error error = EdenArchive::unpack(*pack.mArchive, out, options);
            EXPECT_EQ(className(error), "E_NAME");
            EXPECT_GE(calls, 1u);
            EXPECT_FALSE(anyRegularFile(out));

            // A check that accepts everything lets a harmless pack through.
            options.mNameCheck = [](std::string_view) -> std::optional<std::string> { return std::nullopt; };
            Opened fine = openPack({ file("yes/y.txt", EdenTest::bytesOf("y")) });
            ASSERT_NE(fine.mArchive, nullptr);
            EXPECT_TRUE(EdenArchive::unpack(*fine.mArchive, out, options).ok());
            EXPECT_EQ(readFileBytes(out / "yes" / "y.txt"), EdenTest::bytesOf("y"));
        }

        TEST(EdenArchiveExtract, aNameThatIsAlsoADirectoryShouldBeRefused)
        {
            TempDir dir;
            const auto out = dir.path() / "out";
            Opened pack = openPack({ file("a", EdenTest::bytesOf("file")), file("a/b", EdenTest::bytesOf("inside")),
                file("z.txt", EdenTest::bytesOf("z")) });
            if (pack.mArchive == nullptr)
                GTEST_SKIP() << "this reader rejects the pack at open (section 7.3.2 allows that)";
            const Error error = EdenArchive::unpack(*pack.mArchive, out, UnpackOptions{});
            EXPECT_EQ(className(error), "E_NAME");
            EXPECT_FALSE(anyRegularFile(out));
        }

        TEST(EdenArchiveExtract, twoNamesThatAreOneFileOnACaseInsensitiveSystemShouldBeRefused)
        {
            TempDir dir;
            const auto out = dir.path() / "out";
            // U+00E9 and U+00C9: different bytes to the container, one file name on Windows and macOS.
            Opened pack = openPack({ file("\xC3\x89.txt", EdenTest::bytesOf("upper")), file("\xC3\xA9.txt", EdenTest::bytesOf("lower")) });
            ASSERT_NE(pack.mArchive, nullptr);
            const Error error = EdenArchive::unpack(*pack.mArchive, out, UnpackOptions{});
            EXPECT_EQ(className(error), "E_NAME");
            EXPECT_FALSE(anyRegularFile(out));
        }

        TEST(EdenArchiveExtract, aFolderWhereAFileGoesAndAFileWhereAFolderGoesShouldBeRefused)
        {
            {
                TempDir dir;
                const auto out = dir.path() / "out";
                std::filesystem::create_directories(out / "a.txt");
                Opened pack = openPack({ file("a.txt", EdenTest::bytesOf("x")) });
                ASSERT_NE(pack.mArchive, nullptr);
                UnpackOptions overwrite;
                overwrite.mOverwrite = true; // even then a directory is not replaced
                EXPECT_FALSE(EdenArchive::unpack(*pack.mArchive, out, overwrite).ok());
                EXPECT_TRUE(std::filesystem::is_directory(out / "a.txt"));
            }
            {
                TempDir dir;
                const auto out = dir.path() / "out";
                std::filesystem::create_directories(out);
                ASSERT_TRUE(writeFileBytes(out / "dir", EdenTest::bytesOf("i am a file")));
                Opened pack = openPack({ file("dir/b.bin", EdenTest::bytesOf("x")) });
                ASSERT_NE(pack.mArchive, nullptr);
                EXPECT_FALSE(EdenArchive::unpack(*pack.mArchive, out, UnpackOptions{}).ok());
                EXPECT_EQ(readFileBytes(out / "dir"), EdenTest::bytesOf("i am a file"));
            }
        }

        TEST(EdenArchiveExtract, anOutputPathThatIsAFileShouldBeAnIoError)
        {
            TempDir dir;
            const auto out = dir.path() / "out";
            ASSERT_TRUE(writeFileBytes(out, EdenTest::bytesOf("a file")));
            Opened pack = openPack({ file("a.txt", EdenTest::bytesOf("x")) });
            ASSERT_NE(pack.mArchive, nullptr);
            EXPECT_FALSE(EdenArchive::unpack(*pack.mArchive, out, UnpackOptions{}).ok());
            EXPECT_EQ(readFileBytes(out), EdenTest::bytesOf("a file"));
        }

        TEST(EdenArchiveExtract, aSymbolicLinkBelowTheOutputFolderShouldNeverBeFollowed)
        {
            TempDir dir;
            const auto outside = dir.path() / "outside";
            const auto out = dir.path() / "out";
            std::filesystem::create_directories(outside);
            std::filesystem::create_directories(out);
            std::error_code ec;
            std::filesystem::create_directory_symlink(outside, out / "sub", ec);
            if (ec)
                GTEST_SKIP() << "cannot create a symbolic link here: " << ec.message();

            Opened pack = openPack({ file("sub/f.txt", EdenTest::bytesOf("escaped")), file("zzz.txt", EdenTest::bytesOf("z")) });
            ASSERT_NE(pack.mArchive, nullptr);
            UnpackOptions overwrite;
            overwrite.mOverwrite = true;
            EXPECT_FALSE(EdenArchive::unpack(*pack.mArchive, out, overwrite).ok());
            EXPECT_FALSE(anyRegularFile(outside)) << "the link was followed and a file was written outside the folder";
        }

        TEST(EdenArchiveExtract, aDamagedChunkShouldFailTheUnpackWithHashAndLeaveNoFileForThatEntry)
        {
            TempDir dir;
            const auto out = dir.path() / "out";
            PackSpec spec;
            spec.mChunkSize = 4096;
            spec.mEntries = { file("a.bin", EdenTest::patternBytes(3 * 4096, 7)) };
            Bytes bytes = EdenTest::buildPack(spec);
            bytes[0x1000 + 4096 + 3] ^= 0x10;
            Error openError;
            const std::unique_ptr<Archive> archive = openBytes(bytes, openError);
            ASSERT_NE(archive, nullptr) << openError.describe();
            const Error error = EdenArchive::unpack(*archive, out, UnpackOptions{});
            EXPECT_EQ(className(error), "E_HASH");
            EXPECT_FALSE(std::filesystem::exists(out / "a.bin")) << "a half-written or unverified file was left in place";
            EXPECT_FALSE(anyRegularFile(out)) << "no temporary file may remain";
        }

        TEST(EdenArchiveExtract, anEmptyPackShouldCreateOnlyTheOutputFolder)
        {
            TempDir dir;
            const auto out = dir.path() / "nested" / "out";
            Opened pack = openPack({});
            ASSERT_NE(pack.mArchive, nullptr);
            EXPECT_TRUE(EdenArchive::unpack(*pack.mArchive, out, UnpackOptions{}).ok());
            EXPECT_FALSE(anyRegularFile(dir.path()));
        }
    }
}
