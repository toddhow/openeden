// Task 0083 review 1, finding 4: the archive factory list is process-wide and was written and read with no lock.
// These tests run registration, clearing and lookup from several threads at once. Without a lock they crash or
// are reported by a sanitizer; with one they must finish with the counts the rules give.

#include <components/vfs/archivefactory.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace VFS
{
    namespace
    {
        class FactoryThreads : public ::testing::Test
        {
        protected:
            void SetUp() override { clearArchiveFactories(); }
            void TearDown() override { clearArchiveFactories(); }
        };

        TEST_F(FactoryThreads, registering_the_same_key_from_many_threads_adds_it_once)
        {
            constexpr int threadCount = 8;
            std::atomic<int> added{ 0 };
            std::vector<std::thread> threads;
            for (int i = 0; i < threadCount; ++i)
                threads.emplace_back([&] {
                    for (int k = 0; k < 200; ++k)
                        if (addArchiveFactoryOnce("key" + std::to_string(k),
                                [](const std::filesystem::path&, const ToUTF8::StatelessUtf8Encoder*) {
                                    return std::unique_ptr<Archive>();
                                }))
                            ++added;
                });
            for (std::thread& t : threads)
                t.join();
            EXPECT_EQ(added.load(), 200);
            EXPECT_EQ(archiveFactoryCount(), 200u);
        }

        TEST_F(FactoryThreads, lookups_run_while_other_threads_register)
        {
            std::atomic<bool> stop{ false };
            std::atomic<int> unknown{ 0 };
            std::vector<std::thread> readers;
            for (int i = 0; i < 4; ++i)
                readers.emplace_back([&] {
                    while (!stop)
                    {
                        try
                        {
                            makeArchive("no-such-file.bin", nullptr);
                        }
                        catch (const std::exception&)
                        {
                            ++unknown;
                        }
                    }
                });
            std::vector<std::thread> writers;
            for (int i = 0; i < 4; ++i)
                writers.emplace_back([i] {
                    for (int k = 0; k < 300; ++k)
                        addArchiveFactoryOnce("w" + std::to_string(i) + "_" + std::to_string(k),
                            [](const std::filesystem::path&, const ToUTF8::StatelessUtf8Encoder*) {
                                return std::unique_ptr<Archive>();
                            });
                });
            for (std::thread& t : writers)
                t.join();
            stop = true;
            for (std::thread& t : readers)
                t.join();
            EXPECT_EQ(archiveFactoryCount(), 1200u);
            EXPECT_GT(unknown.load(), 0);
        }

        TEST_F(FactoryThreads, a_factory_may_register_another_without_deadlock)
        {
            // A factory runs outside the lock, so it can call back into the list.
            addArchiveFactory([](const std::filesystem::path&, const ToUTF8::StatelessUtf8Encoder*) {
                addArchiveFactoryOnce("inner", [](const std::filesystem::path&, const ToUTF8::StatelessUtf8Encoder*) {
                    return std::unique_ptr<Archive>();
                });
                return std::unique_ptr<Archive>();
            });
            EXPECT_THROW(makeArchive("no-such-file.bin", nullptr), std::runtime_error);
            EXPECT_EQ(archiveFactoryCount(), 2u);
        }
    }
}
