// Eden Archive unit tests (task 0052). Copyright 2026 the OpenEden contributors. Apache-2.0 (see LICENSE).
//
// The committed conformance corpus (edenarchive/corpus/*.edenpak and expected.txt) must be exactly what the shared case
// list generates. Golden data changes only in a pull request that says why (root CLAUDE.md), so a difference here is
// a failure to be explained, not to be silenced.
#include <gtest/gtest.h>

#include <set>

#include "testhelp.hpp"

namespace EdenTestSuite
{
    namespace
    {
        using EdenArchive::Error;

        TEST(EdenArchiveCorpusFiles, theCommittedCorpusShouldEqualTheRegeneratedOne)
        {
#ifndef EDENARCHIVE_CORPUS_DIR
            GTEST_SKIP() << "EDENARCHIVE_CORPUS_DIR is not defined";
#else
            const std::filesystem::path committed = EDENARCHIVE_CORPUS_DIR;
            if (!std::filesystem::exists(committed / "expected.txt"))
                GTEST_SKIP() << "no committed corpus at " << committed.string();

            // Regenerate into a temporary folder through the same case list the generator uses.
            TempDir dir;
            for (const EdenTest::CorpusCase& c : EdenTest::corpusCases())
                ASSERT_TRUE(writeFileBytes(dir.path() / (c.mId + ".edenpak"), c.mBytes));
            const std::string expected = EdenTest::expectedText();
            ASSERT_TRUE(writeFileBytes(dir.path() / "expected.txt", EdenTest::bytesOf(expected)));

            // expected.txt, ignoring line-ending conversion by version control.
            std::string committedText = EdenTest::textOf(readFileBytes(committed / "expected.txt"));
            committedText.erase(std::remove(committedText.begin(), committedText.end(), '\r'), committedText.end());
            EXPECT_EQ(committedText, expected) << "expected.txt differs from the case list";

            std::set<std::string> listed;
            for (const EdenTest::CorpusCase& c : EdenTest::corpusCases())
            {
                SCOPED_TRACE(c.mId);
                listed.insert(c.mId + ".edenpak");
                const std::filesystem::path file = committed / (c.mId + ".edenpak");
                ASSERT_TRUE(std::filesystem::exists(file)) << "missing committed file";
                const Bytes onDisk = readFileBytes(file);
                EXPECT_EQ(readFileBytes(dir.path() / (c.mId + ".edenpak")), c.mBytes);
                if (!c.mUsesRealZstd)
                {
                    EXPECT_EQ(onDisk, c.mBytes) << "bytes differ";
                    continue;
                }
                // Real zstd output depends on the library version and the CPU (specification 17.3): the committed pack
                // must have the same logical content (manifest digest) and decode to the same bytes.
                Error error;
                const std::unique_ptr<EdenArchive::Archive> committedPack = openBytes(onDisk, error);
                ASSERT_NE(committedPack, nullptr) << error.describe();
                const std::unique_ptr<EdenArchive::Archive> freshPack = openBytes(c.mBytes, error);
                ASSERT_NE(freshPack, nullptr);
                EXPECT_EQ(committedPack->info().mManifestDigest, freshPack->info().mManifestDigest);
                checkEntries(*committedPack, c.mEntries);
            }

            for (const auto& item : std::filesystem::directory_iterator(committed))
            {
                const std::string name = item.path().filename().string();
                if (item.path().extension() == ".edenpak")
                {
                    EXPECT_TRUE(listed.count(name) > 0) << "committed file with no case: " << name;
                }
            }
#endif
        }

        TEST(EdenArchiveCorpusFiles, theCommittedFilesShouldBehaveAsTheExpectationFileSays)
        {
#ifndef EDENARCHIVE_CORPUS_DIR
            GTEST_SKIP() << "EDENARCHIVE_CORPUS_DIR is not defined";
#else
            const std::filesystem::path committed = EDENARCHIVE_CORPUS_DIR;
            std::ifstream stream(committed / "expected.txt");
            if (!stream)
                GTEST_SKIP() << "no committed corpus at " << committed.string();
            std::string line;
            std::size_t checked = 0;
            while (std::getline(stream, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.empty())
                    continue;
                // "<id> <valid|invalid> <class or -> <open|read> <offset or -> <description>"
                std::vector<std::string> fields;
                std::size_t at = 0;
                for (int i = 0; i < 5; ++i)
                {
                    const std::size_t space = line.find(' ', at);
                    ASSERT_NE(space, std::string::npos) << line;
                    fields.push_back(line.substr(at, space - at));
                    at = space + 1;
                }
                SCOPED_TRACE(line);
                ASSERT_EQ(fields.size(), 5u);
                const Bytes bytes = readFileBytes(committed / (fields[0] + ".edenpak"));
                Error error;
                const std::unique_ptr<EdenArchive::Archive> archive = openBytes(bytes, error);
                const bool valid = fields[1] == "valid";
                if (valid || fields[3] == "read")
                    EXPECT_NE(archive, nullptr) << error.describe();
                else
                {
                    EXPECT_EQ(archive, nullptr);
                    if (fields[2] != "-")
                    {
                        const std::string got = className(error);
                        EXPECT_TRUE(fields[2] == got || fields[2].find(got) != std::string::npos) << got;
                    }
                    if (fields[4] != "-")
                    {
                        EXPECT_EQ(error.mOffset, std::stoull(fields[4], nullptr, 16));
                    }
                }
                ++checked;
            }
            EXPECT_EQ(checked, EdenTest::corpusCases().size());
#endif
        }
    }
}
