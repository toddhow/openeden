// Task 0050, stage f: the rules for names inside packs and for writing them to disk (components/vfs/entryname.hpp).
// Written from the spec's "Interface fixed for implementation":
//   checkPackEntryName accepts a name when it is non-empty, at most maxLength bytes, already normalized, valid UTF-8
//   (no overlong forms, no surrogates, nothing above U+10FFFF), has no NUL or other control character below 0x20,
//   no leading or trailing '/', and no empty, "." or ".." component.
//   checkExtractionSafe additionally rejects (and does not require a normalized name or a length limit) a drive
//   prefix, any of : < > " | ? * \ in a component, a component ending in '.' or ' ', and a component whose part
//   before the first '.' is a Windows reserved device name (con prn aux nul com1-9 lpt1-9, case-insensitive).
// Hexadecimal escapes are always written as complete runs so that no letter is swallowed by one.

#include <components/vfs/entryname.hpp>
#include <components/vfs/pathutil.hpp>

#include <gtest/gtest.h>

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VFS::Path
{
    namespace
    {
        using namespace testing;

        // A string_view that keeps embedded NUL bytes.
        template <std::size_t N>
        std::string_view bytes(const char (&text)[N])
        {
            return std::string_view(text, N - 1);
        }

        struct NameCase
        {
            std::string mName;
            bool mAcceptable;
            std::string mWhy;
        };

        std::ostream& operator<<(std::ostream& stream, const NameCase& value)
        {
            std::string shown;
            for (const unsigned char c : value.mName.substr(0, 60))
                shown += (c >= 0x20 && c < 0x7f) ? std::string(1, static_cast<char>(c)) : std::format("\\x{:02x}", c);
            return stream << "'" << shown << "' (" << value.mWhy << ")";
        }

        void expectVerdict(const std::optional<std::string>& result, bool acceptable, const NameCase& param)
        {
            if (acceptable)
                EXPECT_FALSE(result.has_value()) << param << ": " << result.value_or("");
            else
            {
                ASSERT_TRUE(result.has_value()) << param << " was accepted";
                EXPECT_FALSE(result->empty()) << param << ": the reason is empty";
            }
        }

        std::string longName(std::size_t length)
        {
            std::string result;
            for (std::size_t i = 0; i < length; ++i)
                result.push_back(i % 10 == 9 ? '/' : 'a');
            if (!result.empty())
                result.back() = 'a'; // never end with a separator
            return result;
        }

        // ---- checkPackEntryName ----

        struct VFSPackEntryNameTest : TestWithParam<NameCase>
        {
        };

        TEST_P(VFSPackEntryNameTest, shouldGiveTheExpectedVerdict)
        {
            const NameCase& param = GetParam();
            expectVerdict(checkPackEntryName(param.mName), param.mAcceptable, param);
        }

        const NameCase packNames[] = {
            // acceptable
            { "a", true, "one letter" },
            { "meshes/x.nif", true, "usual path" },
            { "meshes/dir/sub/file.name.with.dots.nif", true, "dots in a name" },
            { "a b/c d.txt", true, "spaces" },
            { "a-b_c+d(e).txt", true, "punctuation" },
            { ".hidden", true, "leading dot in a component" },
            { "...a", true, "three dots plus a letter is not a dot component" },
            { "a...", true, "trailing dots are allowed in pack names" },
            { "a ", true, "trailing space is allowed in pack names" },
            { "con.txt", true, "reserved Windows names are only a problem when extracting" },
            { "c:/x", true, "a colon is only a problem when extracting" },
            { "caf\xC3\xA9", true, "two byte UTF-8" },
            { "\xE2\x82\xAC", true, "three byte UTF-8" },
            { "\xF0\x90\x80\x80", true, "U+10000, first four byte code point" },
            { "\xF4\x8F\xBF\xBF", true, "U+10FFFF, last code point" },
            { "\xEF\xBF\xBF", true, "U+FFFF" },
            { "\xED\x9F\xBF", true, "U+D7FF, just before the surrogates" },
            { "\xEE\x80\x80", true, "U+E000, just after the surrogates" },
            { "\xC2\x80", true, "U+0080, shortest two byte form" },
            { "\xE0\xA0\x80", true, "U+0800, shortest three byte form" },
            { "\xC3\xBC/x", true, "non-ASCII component then a path" },
            // empty and slashes
            { "", false, "empty" },
            { "/", false, "only a slash" },
            { "/a", false, "leading slash" },
            { "a/", false, "trailing slash" },
            { "a//b", false, "empty component" },
            { "//a", false, "leading double slash" },
            { "a/b//", false, "trailing double slash" },
            // dot components
            { ".", false, "dot" },
            { "..", false, "dot dot" },
            { "./a", false, "leading dot component" },
            { "a/./b", false, "middle dot component" },
            { "a/.", false, "trailing dot component" },
            { "../a", false, "leading dot dot" },
            { "a/../b", false, "middle dot dot" },
            { "a/..", false, "trailing dot dot" },
            { "a/../../b", false, "traversal" },
            // not normalized
            { "A", false, "upper case" },
            { "dir/File.txt", false, "upper case in a component" },
            { "a\\b", false, "backslash" },
            { "a/b\\c", false, "backslash in a later component" },
            // invalid UTF-8
            { "\x80", false, "lone continuation byte" },
            { "\xBF", false, "lone continuation byte, high" },
            { "a\xC3", false, "truncated two byte sequence" },
            { "\xE2\x82", false, "truncated three byte sequence" },
            { "\xF0\x9F\x98", false, "truncated four byte sequence" },
            { "\xC3\x28", false, "bad continuation byte" },
            { "\xE2\x28\xA1", false, "bad second byte of three" },
            { "\xE2\x82\x28", false, "bad third byte of three" },
            { "\xF0\x28\x8C\xBC", false, "bad second byte of four" },
            { "\xF0\x90\x28\xBC", false, "bad third byte of four" },
            { "\xF0\x90\x8C\x28", false, "bad fourth byte of four" },
            { "\xC0\x80", false, "overlong NUL" },
            { "\xC0\xAF", false, "overlong slash" },
            { "\xC1\xBF", false, "overlong two byte form of U+007F" },
            { "\xE0\x80\x80", false, "overlong three byte NUL" },
            { "\xE0\x9F\xBF", false, "overlong three byte form of U+07FF" },
            { "\xF0\x80\x80\x80", false, "overlong four byte NUL" },
            { "\xF0\x8F\xBF\xBF", false, "overlong four byte form of U+FFFF" },
            { "\xED\xA0\x80", false, "surrogate U+D800" },
            { "\xED\xAF\xBF", false, "surrogate U+DBFF" },
            { "\xED\xB0\x80", false, "surrogate U+DC00" },
            { "\xED\xBF\xBF", false, "surrogate U+DFFF" },
            { "\xF4\x90\x80\x80", false, "U+110000, above the last code point" },
            { "\xF5\x80\x80\x80", false, "lead byte F5" },
            { "\xF8\x88\x80\x80\x80", false, "five byte form" },
            { "\xFC\x84\x80\x80\x80\x80", false, "six byte form" },
            { "\xFE", false, "byte FE" },
            { "\xFF", false, "byte FF" },
            { "ok/\xFF/ok", false, "invalid byte inside a path" },
            { "caf\xC3", false, "cut accent at the end" },
            // control characters
            { std::string("a\0b", 3), false, "embedded NUL" },
            { std::string("\0", 1), false, "only NUL" },
            { "a\x01" "b", false, "control character 0x01" },
            { "a\tb", false, "tab" },
            { "a\nb", false, "line feed" },
            { "a\rb", false, "carriage return" },
            { "a\x1b" "b", false, "escape" },
            { "a\x1f" "b", false, "control character 0x1f" },
            { "x/\x07", false, "bell at the end of a component" },
        };

        INSTANTIATE_TEST_SUITE_P(PackNames, VFSPackEntryNameTest, ValuesIn(packNames));

        TEST(VFSPackEntryNameLengthTest, defaultLimitShouldBeUsedWhenNoneIsGiven)
        {
            EXPECT_GT(defaultMaxEntryNameLength, 0u);
            EXPECT_FALSE(checkPackEntryName(longName(defaultMaxEntryNameLength)).has_value());
            EXPECT_TRUE(checkPackEntryName(longName(defaultMaxEntryNameLength + 1)).has_value());
            const std::string atLimit = longName(defaultMaxEntryNameLength);
            EXPECT_FALSE(checkPackEntryName(atLimit, defaultMaxEntryNameLength).has_value());
        }

        TEST(VFSPackEntryNameLengthTest, boundaryShouldBeExactlyTheLimitInBytes)
        {
            for (const std::size_t limit : { std::size_t(1), std::size_t(2), std::size_t(10), std::size_t(255),
                     std::size_t(256), std::size_t(1000), std::size_t(4096) })
            {
                EXPECT_FALSE(checkPackEntryName(std::string(limit, 'a'), limit).has_value()) << limit;
                const std::optional<std::string> tooLong = checkPackEntryName(std::string(limit + 1, 'a'), limit);
                ASSERT_TRUE(tooLong.has_value()) << limit;
                EXPECT_FALSE(tooLong->empty());
            }
        }

        TEST(VFSPackEntryNameLengthTest, limitShouldCountBytesNotCharacters)
        {
            const std::size_t limit = 20;
            // 9 two-byte characters and 2 letters: 11 characters but 20 bytes.
            std::string exact;
            for (int i = 0; i < 9; ++i)
                exact += "\xC3\xBC";
            exact += "ab";
            ASSERT_EQ(exact.size(), limit);
            EXPECT_FALSE(checkPackEntryName(exact, limit).has_value());
            EXPECT_TRUE(checkPackEntryName(exact + "c", limit).has_value());
            // 10 two-byte characters: 10 characters, 20 bytes; one more is 22.
            std::string tens;
            for (int i = 0; i < 10; ++i)
                tens += "\xC3\xBC";
            EXPECT_FALSE(checkPackEntryName(tens, limit).has_value());
            EXPECT_TRUE(checkPackEntryName(tens + "\xC3\xBC", limit).has_value());
        }

        TEST(VFSPackEntryNameLengthTest, longNameWithSeparatorsShouldBeCheckedByTotalLength)
        {
            const std::string name = longName(1024);
            ASSERT_EQ(name.size(), 1024u);
            EXPECT_FALSE(checkPackEntryName(name, 1024).has_value());
            EXPECT_TRUE(checkPackEntryName(name, 1023).has_value());
        }

        TEST(VFSPackEntryNameLengthTest, zeroLimitShouldRejectEverythingNonEmpty)
        {
            EXPECT_TRUE(checkPackEntryName("a", 0).has_value());
            EXPECT_TRUE(checkPackEntryName("", 0).has_value());
        }

        TEST(VFSPackEntryNameLengthTest, veryLongNameShouldBeRejectedWithoutTroubleForTheOtherRules)
        {
            const std::string huge(1000000, 'a');
            EXPECT_TRUE(checkPackEntryName(huge).has_value());
            EXPECT_FALSE(checkPackEntryName(huge, 2000000).has_value());
        }

        TEST(VFSPackEntryNameTest, everyNameThatIsAcceptedShouldAlreadyBeNormalized)
        {
            for (const NameCase& item : packNames)
                if (item.mAcceptable)
                {
                    EXPECT_TRUE(isNormalized(item.mName)) << item;
                    EXPECT_EQ(normalizeFilename(item.mName), item.mName) << item;
                }
        }

        TEST(VFSPackEntryNameTest, embeddedNulShouldBeRejectedAndNotCutShort)
        {
            // A C string API would see only "ok" and accept it.
            EXPECT_TRUE(checkPackEntryName(bytes("ok\0/..")).has_value());
            EXPECT_TRUE(checkPackEntryName(bytes("ok\0")).has_value());
            EXPECT_TRUE(checkPackEntryName(bytes("\0ok")).has_value());
        }

        // ---- checkExtractionSafe ----

        struct VFSExtractionSafeTest : TestWithParam<NameCase>
        {
        };

        TEST_P(VFSExtractionSafeTest, shouldGiveTheExpectedVerdict)
        {
            const NameCase& param = GetParam();
            expectVerdict(checkExtractionSafe(param.mName), param.mAcceptable, param);
        }

        const NameCase extractionNames[] = {
            // acceptable: everything the pack rules accept that is also safe on disk, and names that are not
            // normalized or are long (those rules are not part of the extraction check)
            { "a", true, "one letter" },
            { "meshes/x.nif", true, "usual path" },
            { "Meshes/Dir/File.NIF", true, "upper case is allowed when extracting" },
            { "a b/c d.txt", true, "spaces inside" },
            { ".hidden", true, "leading dot" },
            { "a.b.c", true, "dots inside" },
            { "...a", true, "dots then a letter" },
            { "caf\xC3\xA9/x.txt", true, "UTF-8" },
            { "console.txt", true, "starts with a reserved name but is not one" },
            { "con1", true, "con1 is not reserved" },
            { "com0", true, "com0 is not reserved" },
            { "com10.txt", true, "com10 is not reserved" },
            { "lpt0", true, "lpt0 is not reserved" },
            { "lpt10", true, "lpt10 is not reserved" },
            { "xcon", true, "reserved name only as a suffix" },
            { "aux1", true, "aux1 is not reserved" },
            { "dir/nulx.txt", true, "nulx is not reserved" },
            { "a/b/c/d/e/f/g/h", true, "deep path" },
            // empty, slashes, dot components
            { "", false, "empty" },
            { "/", false, "only a slash" },
            { "/a", false, "absolute path" },
            { "a/", false, "trailing slash" },
            { "a//b", false, "empty component" },
            { ".", false, "dot" },
            { "..", false, "dot dot" },
            { "./a", false, "dot component" },
            { "a/./b", false, "middle dot component" },
            { "../a", false, "parent escape" },
            { "a/..", false, "trailing parent" },
            { "a/../b", false, "parent in the middle" },
            { "a/../../b", false, "traversal out of the folder" },
            { "a/b/../../../c", false, "deep traversal" },
            // drive prefixes, colons and the other forbidden characters
            { "c:/x", false, "drive prefix" },
            { "C:/x", false, "drive prefix, upper case" },
            { "c:x", false, "drive relative path" },
            { "a/c:/x", false, "drive prefix in a later component" },
            { "a:b", false, "colon" },
            { "file.txt:stream", false, "alternate data stream" },
            { "a<b", false, "less than" },
            { "a>b", false, "greater than" },
            { "a\"b", false, "double quote" },
            { "a|b", false, "pipe" },
            { "a?b", false, "question mark" },
            { "a*b", false, "star" },
            { "dir/*.txt", false, "wildcard" },
            { "a\\b", false, "backslash" },
            { "..\\a", false, "backslash traversal" },
            { "\\\\server\\share", false, "UNC path" },
            { "\\a", false, "leading backslash" },
            // trailing dots and spaces
            { "a.", false, "trailing dot" },
            { "a./b", false, "trailing dot in a component" },
            { "a/b.", false, "trailing dot in the last component" },
            { "a ", false, "trailing space" },
            { "a /b", false, "trailing space in a component" },
            { "a/b ", false, "trailing space in the last component" },
            { "...", false, "only dots" },
            { "a/...", false, "only dots in a component" },
            // reserved device names
            { "con", false, "con" },
            { "CON", false, "CON" },
            { "Con", false, "Con" },
            { "prn", false, "prn" },
            { "aux", false, "aux" },
            { "nul", false, "nul" },
            { "com1", false, "com1" },
            { "COM5", false, "COM5" },
            { "com9", false, "com9" },
            { "lpt1", false, "lpt1" },
            { "LPT5", false, "LPT5" },
            { "lpt9", false, "lpt9" },
            { "con.txt", false, "con with an extension" },
            { "CON.TXT", false, "CON with an extension" },
            { "nul.tar.gz", false, "nul with two extensions" },
            { "aux.x", false, "aux with an extension" },
            { "com1.txt", false, "com1 with an extension" },
            { "lpt9.dat", false, "lpt9 with an extension" },
            { "dir/con", false, "reserved name in a later component" },
            { "dir/prn.txt/file", false, "reserved name in a middle component" },
            { "con/file.txt", false, "reserved name as a folder" },
            { "a/b/NUL", false, "reserved name last" },
            // everything the pack rules reject for content reasons
            { "\xC0\x80", false, "overlong NUL" },
            { "\xED\xA0\x80", false, "surrogate" },
            { "\xF4\x90\x80\x80", false, "above U+10FFFF" },
            { "a\xC3", false, "truncated sequence" },
            { "\xFF", false, "invalid byte" },
            { std::string("a\0b", 3), false, "embedded NUL" },
            { "a\x01" "b", false, "control character" },
            { "a\tb", false, "tab" },
            { "a\nb", false, "line feed" },
        };

        INSTANTIATE_TEST_SUITE_P(ExtractionNames, VFSExtractionSafeTest, ValuesIn(extractionNames));

        TEST(VFSExtractionSafeLengthTest, shouldHaveNoLengthLimit)
        {
            EXPECT_FALSE(checkExtractionSafe(std::string(5000, 'a')).has_value());
            EXPECT_FALSE(checkExtractionSafe(longName(defaultMaxEntryNameLength + 1)).has_value());
            EXPECT_FALSE(checkExtractionSafe(longName(100000)).has_value());
        }

        TEST(VFSExtractionSafeLengthTest, shouldNotRequireANormalizedName)
        {
            EXPECT_TRUE(checkPackEntryName("Mixed/Case.TXT").has_value());
            EXPECT_FALSE(checkExtractionSafe("Mixed/Case.TXT").has_value());
        }

        TEST(VFSExtractionSafeLengthTest, embeddedNulShouldBeRejectedAndNotCutShort)
        {
            EXPECT_TRUE(checkExtractionSafe(bytes("ok\0/..")).has_value());
            EXPECT_TRUE(checkExtractionSafe(bytes("ok\0")).has_value());
        }

        // ---- relation between the two ----

        TEST(VFSEntryNameRelationTest, namesRejectedForContentOrStructureShouldBeRejectedByBoth)
        {
            for (const NameCase& item : packNames)
            {
                if (item.mAcceptable)
                    continue;
                // The pack-only reasons: not normalized (upper case, backslash). Extraction rejects a backslash too,
                // but accepts upper case.
                const bool onlyUpperCase = isNormalized(normalizeFilename(item.mName)) && !isNormalized(item.mName)
                    && item.mName.find('\\') == std::string::npos;
                if (onlyUpperCase)
                    continue;
                EXPECT_TRUE(checkExtractionSafe(item.mName).has_value()) << item;
            }
        }

        TEST(VFSEntryNameRelationTest, safeNamesThatAreAlsoNormalizedAndShortShouldBeAcceptableInAPackToo)
        {
            for (const NameCase& item : extractionNames)
            {
                if (!item.mAcceptable || !isNormalized(item.mName) || item.mName.size() > defaultMaxEntryNameLength)
                    continue;
                EXPECT_FALSE(checkPackEntryName(item.mName).has_value()) << item;
            }
        }
    }
}
