// Task 0050, stage e: the thread-safety contract. After buildIndex() the manager is immutable: exists, find, get,
// getRecursiveDirectoryIterator, getArchive, getSize, getLastModified, getStem, File::open and File::readRange may be
// called from any thread at once. These tests build one index from loose files and BSA archives and then read it from
// many threads, comparing everything with the expected content. They are deterministic and meant to run under the
// AddressSanitizer and UndefinedBehaviorSanitizer jobs.

#include "vfsfakes.hpp"

#include <components/files/constrainedfilestream.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace VFS
{
    namespace
    {
        using namespace testing;
        using namespace VFSTest;

        constexpr unsigned fileCount = 300;
        constexpr unsigned threadCount = 8;
        constexpr unsigned repetitions = 2;

        struct Expected
        {
            std::string mContent;
            std::string mDescriptionPart; // part of Manager::getArchive's answer for the winning source
        };

        std::string nameOf(unsigned i)
        {
            return std::format("dir{}/file{}.dat", i % 10, i);
        }

        std::string bsaNameOf(unsigned i)
        {
            return std::format("Dir{}\\File{}.DAT", i % 10, i); // upper case and backslashes, as in real archives
        }

        std::string makeContent(unsigned i, const char* source)
        {
            std::string content = std::format("{}:{}:", source, i);
            content += patternContent(50 + (i * 37) % 2000, i);
            return content;
        }

        struct VFSThreadsTest : Test
        {
            ScratchDir mTemp;
            Manager mVfs;
            std::map<std::string, Expected> mExpected;
            std::string mFirstFailure;
            std::mutex mFailureMutex;
            std::atomic<unsigned> mFailures{ 0 };

            void SetUp() override
            {
                const auto dirA = mTemp.makeDir("dataa");
                const auto dirB = mTemp.makeDir("datab");

                // Archive: files 0 to 149. Folder A: 100 to 249. Folder B: 200 to 299. Later sources win, folders
                // beat archives, so: 0-99 archive, 100-199 folder A, 200-299 folder B.
                std::vector<BsaEntry> entries;
                for (unsigned i = 0; i < 150; ++i)
                    entries.push_back({ bsaNameOf(i), makeContent(i, "bsa") });
                writeBsa(dirA / "base.bsa", entries);
                for (unsigned i = 100; i < 250; ++i)
                    writeFile(dirA / nameOf(i), makeContent(i, "A"));
                for (unsigned i = 200; i < 300; ++i)
                    writeFile(dirB / nameOf(i), makeContent(i, "B"));

                registerAll(mVfs, { dirA, dirB }, { "base.bsa" });

                for (unsigned i = 0; i < fileCount; ++i)
                {
                    if (i < 100)
                        mExpected[nameOf(i)] = { makeContent(i, "bsa"), "BSA: " };
                    else if (i < 200)
                        mExpected[nameOf(i)] = { makeContent(i, "A"), "DIR: " + dirA.string() };
                    else
                        mExpected[nameOf(i)] = { makeContent(i, "B"), "DIR: " + dirB.string() };
                }
                mExpected["base.bsa"] = { readAll(*Files::openConstrainedFileStream(dirA / "base.bsa")),
                    "DIR: " + dirA.string() };
            }

            void fail(const std::string& message)
            {
                ++mFailures;
                std::lock_guard lock(mFailureMutex);
                if (mFirstFailure.empty())
                    mFirstFailure = message;
            }

            // Everything a reader thread does for one name.
            void checkName(const std::string& name, const Expected& expected, unsigned seed)
            {
                const Path::Normalized normalized(name);
                if (!mVfs.exists(normalized) || !mVfs.exists(Path::NormalizedView(normalized)))
                    return fail("exists " + name);

                const Files::IStreamPtr found = mVfs.find(Path::NormalizedView(normalized));
                if (found == nullptr || readAll(*found) != expected.mContent)
                    return fail("find " + name);

                if (readAll(mVfs.get(normalized)) != expected.mContent
                    || readAll(mVfs.get(Path::NormalizedView(normalized))) != expected.mContent)
                    return fail("get " + name);

                if (mVfs.getSize(Path::NormalizedView(normalized)) != expected.mContent.size())
                    return fail("getSize " + name);

                const std::size_t size = expected.mContent.size();
                const std::size_t length = std::min<std::size_t>(size, 1 + seed % 40);
                const std::size_t offset = size == 0 ? 0 : (seed * 7919u) % (size - length + 1);
                std::string window(length, '\0');
                mVfs.readRange(Path::NormalizedView(normalized), offset, length, window.data());
                if (window != expected.mContent.substr(offset, length))
                    return fail("readRange " + name);

                const std::string archive = mVfs.getArchive(normalized);
                if (archive.find(expected.mDescriptionPart) == std::string::npos)
                    return fail("getArchive " + name + ": " + archive);

                if (mVfs.getStem(Path::NormalizedView(normalized)).empty())
                    return fail("getStem " + name);
                (void)mVfs.getLastModified(Path::NormalizedView(normalized));
            }

            void runReader(unsigned thread)
            {
                try
                {
                    for (unsigned rep = 0; rep < repetitions; ++rep)
                    {
                        unsigned seed = thread * 31 + rep;
                        for (const auto& [name, expected] : mExpected)
                        {
                            // Every thread visits every name, starting at a different place, so that threads read
                            // the same files at the same time.
                            if ((++seed + thread) % 2 == 0 || rep == 0)
                                checkName(name, expected, seed);
                        }

                        // Directory ranges.
                        std::size_t count = 0;
                        for (const Path::Normalized& name : mVfs.getRecursiveDirectoryIterator("dir3"))
                        {
                            ++count;
                            if (!name.view().starts_with("dir3/"))
                                fail("range content " + name.value());
                        }
                        if (count != fileCount / 10)
                            fail("range count " + std::to_string(count));

                        std::size_t all = 0;
                        for ([[maybe_unused]] const Path::Normalized& name : mVfs.getRecursiveDirectoryIterator())
                            ++all;
                        if (all != fileCount + 1)
                            fail("full range count " + std::to_string(all));

                        // Misses.
                        if (mVfs.exists(Path::Normalized("nothere/file.dat")))
                            fail("exists of a missing name");
                        if (mVfs.find(Path::NormalizedView("nothere/file.dat")) != nullptr)
                            fail("find of a missing name");
                        try
                        {
                            (void)mVfs.get(Path::Normalized("nothere/file.dat"));
                            fail("get of a missing name did not throw");
                        }
                        catch (const std::runtime_error&)
                        {
                        }
                        try
                        {
                            (void)mVfs.getSize(Path::NormalizedView("nothere/file.dat"));
                            fail("getSize of a missing name did not throw");
                        }
                        catch (const std::runtime_error&)
                        {
                        }
                    }
                }
                catch (const std::exception& e)
                {
                    fail(std::string("exception: ") + e.what());
                }
                catch (...)
                {
                    fail("unknown exception");
                }
            }
        };

        TEST_F(VFSThreadsTest, expectedNamesShouldBeTheSingleThreadedAnswer)
        {
            // The test's own expectations first, without threads, so a failure below is about threading.
            ASSERT_EQ(mExpected.size(), fileCount + 1u);
            for (const auto& [name, expected] : mExpected)
                checkName(name, expected, 3);
            EXPECT_EQ(mFailures.load(), 0u) << mFirstFailure;
        }

        TEST_F(VFSThreadsTest, manyThreadsShouldReadTheBuiltIndexAtOnce)
        {
            std::atomic<bool> go{ false };
            std::vector<std::thread> threads;
            for (unsigned t = 0; t < threadCount; ++t)
                threads.emplace_back([this, t, &go] {
                    while (!go)
                        std::this_thread::yield();
                    runReader(t);
                });
            go = true;
            for (std::thread& thread : threads)
                thread.join();

            EXPECT_EQ(mFailures.load(), 0u) << mFirstFailure;
        }

        TEST_F(VFSThreadsTest, manyThreadsShouldOpenAndReadTheSameFileAtOnce)
        {
            // All threads read the same few files, including a BSA entry and a loose file, through streams and
            // through range reads, with different offsets.
            const std::vector<std::string> names = { nameOf(5), nameOf(150), nameOf(250), "base.bsa" };
            std::atomic<bool> go{ false };
            std::vector<std::thread> threads;
            for (unsigned t = 0; t < threadCount; ++t)
                threads.emplace_back([this, t, &go, &names] {
                    while (!go)
                        std::this_thread::yield();
                    try
                    {
                        for (unsigned i = 0; i < 200; ++i)
                        {
                            const std::string& name = names[(i + t) % names.size()];
                            const Expected& expected = mExpected.at(name);
                            const Path::Normalized normalized(name);
                            Files::IStreamPtr stream = mVfs.get(normalized);
                            // Read the stream in small pieces to keep many streams open at the same time.
                            std::string content;
                            char buffer[37];
                            while (stream->read(buffer, sizeof(buffer)) || stream->gcount() > 0)
                                content.append(buffer, static_cast<std::size_t>(stream->gcount()));
                            if (content != expected.mContent)
                                fail("stream content of " + name);

                            const std::size_t length = std::min<std::size_t>(expected.mContent.size(), 29);
                            std::string window(length, '\0');
                            const std::size_t offset = (i * 13 + t) % (expected.mContent.size() - length + 1);
                            mVfs.readRange(Path::NormalizedView(normalized), offset, length, window.data());
                            if (window != expected.mContent.substr(offset, length))
                                fail("range content of " + name);
                        }
                    }
                    catch (const std::exception& e)
                    {
                        fail(std::string("exception: ") + e.what());
                    }
                });
            go = true;
            for (std::thread& thread : threads)
                thread.join();

            EXPECT_EQ(mFailures.load(), 0u) << mFirstFailure;
        }

        TEST_F(VFSThreadsTest, threadsOverAThirdKindOfArchiveShouldAgreeToo)
        {
            Manager vfs;
            FakeArchive& fake = addFakeArchive(vfs, "fake");
            for (unsigned i = 0; i < 100; ++i)
                fake.add(nameOf(i), makeContent(i, "fake"), makeHash(static_cast<std::uint8_t>(i)));
            vfs.buildIndex();

            std::atomic<unsigned> failures{ 0 };
            std::vector<std::thread> threads;
            for (unsigned t = 0; t < threadCount; ++t)
                threads.emplace_back([&, t] {
                    try
                    {
                        for (unsigned i = 0; i < 100; ++i)
                        {
                            const std::string name = nameOf((i + t * 11) % 100);
                            const std::string expected = makeContent((i + t * 11) % 100, "fake");
                            if (readAll(vfs.get(Path::Normalized(name))) != expected)
                                ++failures;
                            if (vfs.getSize(Path::NormalizedView(Path::Normalized(name))) != expected.size())
                                ++failures;
                        }
                    }
                    catch (...)
                    {
                        ++failures;
                    }
                });
            for (std::thread& thread : threads)
                thread.join();
            EXPECT_EQ(failures.load(), 0u);
        }
    }
}
