// Task 0072, stage h: the Slang compiler in the game (components/vsgutil/slangruntime.*, and the run-time path of
// vsgUtil::findSlangPermutation). The cache, the self-test and the failure rules are tested with a fake compiler; the
// library itself (when the build has it) must reproduce every permutation the build wrote, byte for byte, and must
// compile sets the build never listed to something that declares no more than the GLSL does.
#include <gtest/gtest.h>

#include <components/pipeline/terrain.hpp>
#include <components/resource/shadersettings.hpp>
#include <components/vsgutil/shaderpermutation.hpp>
#include <components/vsgutil/slangruntime.hpp>
#include <components/vsgutil/spirvinterface.hpp>

#include <vsg/all.h>
#include <vsg/io/glsl.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using vsgUtil::SlangRuntime;
    using vsgUtil::SlangRuntimeCheck;
    using vsgUtil::SlangRuntimeRequest;

    // A module that is only a header and a few words: all SlangRuntime looks at is the magic.
    std::vector<uint32_t> fakeWords(uint32_t marker)
    {
        return { 0x07230203u, 0x00010000u, 0u, 8u, 0u, marker };
    }

    class FakeCompiler final : public vsgUtil::SlangRuntimeCompiler
    {
    public:
        std::atomic<int> mCompiles{ 0 };
        std::string mIdentity = "fake-compiler-1";
        std::vector<uint32_t> mOutput = fakeWords(1);
        std::string mError;
        std::vector<SlangRuntimeRequest> mSeen;
        std::mutex mMutex;

        std::string cacheIdentity() override { return mIdentity; }
        std::vector<uint32_t> compile(const SlangRuntimeRequest& request, std::string& error) override
        {
            ++mCompiles;
            {
                std::lock_guard lock(mMutex);
                mSeen.push_back(request);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            error = mError;
            return mOutput;
        }
    };

    // A fresh folder with two .slang files, and a cache folder beside it.
    class Fixture
    {
    public:
        Fixture()
        {
            static std::atomic<unsigned> counter{ 0 };
            mRoot = std::filesystem::temp_directory_path()
                / ("openeden_slangruntime_"
                    + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_"
                    + std::to_string(counter++));
            std::filesystem::create_directories(mRoot / "shaders" / "lib");
            write("shaders/a.slang", "// a\n");
            write("shaders/lib/b.slang", "// b\n");
            mRequest.mStageFile = "default/main.frag";
            mRequest.mInfo = { "a.slang", "fragMain", "fragment" };
            mRequest.mDefines = { "DIFFUSE_MAP", "MATERIAL" };
            mRequest.mShaderRoot = mRoot / "shaders";
        }
        ~Fixture()
        {
            std::error_code ec;
            std::filesystem::remove_all(mRoot, ec);
        }
        void write(const std::string& file, const std::string& text)
        {
            std::ofstream out(mRoot / file, std::ios::binary);
            out << text;
        }
        std::filesystem::path cache() const { return mRoot / "cache"; }
        std::size_t cacheFiles() const
        {
            std::size_t count = 0;
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(cache(), ec))
            {
                if (entry.path().extension() == ".spv")
                    ++count;
            }
            return count;
        }
        vsg::ref_ptr<SlangRuntime> runtime(FakeCompiler*& compiler, bool withCache = true)
        {
            auto fake = std::make_unique<FakeCompiler>();
            compiler = fake.get();
            auto result = SlangRuntime::create(std::move(fake), withCache ? cache() : std::filesystem::path());
            result->setLog([this](bool warning, const std::string& line) {
                if (!warning)
                    return; // the summary at the end of a runtime's life
                std::lock_guard lock(mLogMutex);
                mLog.push_back(line);
            });
            return result;
        }
        std::vector<std::string> log()
        {
            std::lock_guard lock(mLogMutex);
            return mLog;
        }

        std::filesystem::path mRoot;
        SlangRuntimeRequest mRequest;
        SlangRuntimeCheck mCheck;

    private:
        std::mutex mLogMutex;
        std::vector<std::string> mLog;
    };

    TEST(SlangRuntime, a_compile_is_cached_and_the_second_call_reads_the_cache)
    {
        Fixture f;
        FakeCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        EXPECT_EQ(runtime->obtain(f.mRequest, f.mCheck), fakeWords(1));
        EXPECT_EQ(compiler->mCompiles, 1);
        EXPECT_EQ(f.cacheFiles(), 1u);
        EXPECT_EQ(runtime->obtain(f.mRequest, f.mCheck), fakeWords(1));
        EXPECT_EQ(compiler->mCompiles, 1);
        const auto stats = runtime->stats();
        EXPECT_EQ(stats.mCompiled, 1u);
        EXPECT_EQ(stats.mCacheHits, 1u);
        // A second runtime (another run of the game) with the same cache compiles nothing.
        FakeCompiler* other = nullptr;
        auto next = f.runtime(other);
        EXPECT_EQ(next->obtain(f.mRequest, f.mCheck), fakeWords(1));
        EXPECT_EQ(other->mCompiles, 0);
        EXPECT_TRUE(f.log().empty());
    }

    TEST(SlangRuntime, the_request_reaches_the_compiler_as_it_was_given)
    {
        Fixture f;
        FakeCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        runtime->obtain(f.mRequest, f.mCheck);
        ASSERT_EQ(compiler->mSeen.size(), 1u);
        EXPECT_EQ(compiler->mSeen[0].mStageFile, "default/main.frag");
        EXPECT_EQ(compiler->mSeen[0].mInfo.mEntry, "fragMain");
        EXPECT_EQ(compiler->mSeen[0].mDefines, (std::set<std::string>{ "DIFFUSE_MAP", "MATERIAL" }));
        EXPECT_EQ(compiler->mSeen[0].mShaderRoot, f.mRequest.mShaderRoot);
    }

    TEST(SlangRuntime, everything_the_result_depends_on_is_in_the_key)
    {
        Fixture f;
        FakeCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        const std::string base = runtime->keyText(f.mRequest, f.mCheck);
        EXPECT_EQ(base, runtime->keyText(f.mRequest, f.mCheck));
        const auto differs = [&](const SlangRuntimeRequest& request, const SlangRuntimeCheck& check) {
            return runtime->keyText(request, check) != base
                && SlangRuntime::fileNameOf(runtime->keyText(request, check)) != SlangRuntime::fileNameOf(base);
        };
        {
            auto request = f.mRequest;
            request.mDefines.insert("NORMAL");
            EXPECT_TRUE(differs(request, f.mCheck)) << "the define set";
        }
        {
            auto request = f.mRequest;
            request.mStageFile = "default/main.vert";
            EXPECT_TRUE(differs(request, f.mCheck)) << "the stage file";
        }
        {
            auto request = f.mRequest;
            request.mInfo.mEntry = "other";
            EXPECT_TRUE(differs(request, f.mCheck)) << "the entry point";
        }
        {
            auto request = f.mRequest;
            request.mInfo.mSource = "lib/b.slang";
            EXPECT_TRUE(differs(request, f.mCheck)) << "the source file";
        }
        {
            SlangRuntimeCheck check = f.mCheck;
            check.mKey = "another GLSL";
            EXPECT_TRUE(differs(f.mRequest, check)) << "the GLSL it was checked against";
        }
        {
            // Any .slang file under the root, an include of the source included: edited between two runs.
            FakeCompiler* other = nullptr;
            f.write("shaders/lib/b.slang", "// b changed\n");
            auto next = f.runtime(other);
            EXPECT_NE(next->keyText(f.mRequest, f.mCheck), base) << "an included file";
        }
        {
            FakeCompiler* other = nullptr;
            auto next = f.runtime(other);
            other->mIdentity = "fake-compiler-2";
            EXPECT_NE(next->keyText(f.mRequest, f.mCheck), base) << "the compiler";
        }
    }

    TEST(SlangRuntime, a_failed_compile_gives_nothing_logs_once_and_is_not_tried_again)
    {
        Fixture f;
        FakeCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        compiler->mOutput.clear();
        compiler->mError = "error[E30015]: undefined identifier";
        EXPECT_TRUE(runtime->obtain(f.mRequest, f.mCheck).empty());
        EXPECT_TRUE(runtime->obtain(f.mRequest, f.mCheck).empty());
        EXPECT_EQ(compiler->mCompiles, 1);
        ASSERT_EQ(f.log().size(), 1u);
        EXPECT_NE(f.log()[0].find("default/main.frag"), std::string::npos);
        EXPECT_NE(f.log()[0].find("undefined identifier"), std::string::npos);
        EXPECT_NE(f.log()[0].find("GLSL"), std::string::npos);
        EXPECT_EQ(f.cacheFiles(), 0u);
        EXPECT_EQ(runtime->stats().mFailed, 1u);
        // Another set still compiles.
        compiler->mOutput = fakeWords(2);
        auto other = f.mRequest;
        other.mDefines.insert("NORMAL");
        EXPECT_EQ(runtime->obtain(other, f.mCheck), fakeWords(2));
    }

    TEST(SlangRuntime, output_that_is_not_spirv_is_refused)
    {
        Fixture f;
        FakeCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        compiler->mOutput = { 1u, 2u, 3u };
        EXPECT_TRUE(runtime->obtain(f.mRequest, f.mCheck).empty());
        EXPECT_EQ(f.cacheFiles(), 0u);
        EXPECT_EQ(f.log().size(), 1u);
    }

    TEST(SlangRuntime, a_result_the_check_refuses_is_not_used_and_not_cached)
    {
        Fixture f;
        FakeCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        int checks = 0;
        f.mCheck.mCheck = [&checks](const std::vector<uint32_t>&) {
            ++checks;
            return std::string("the twin declares what the GLSL does not: in location=9 vec4f32");
        };
        EXPECT_TRUE(runtime->obtain(f.mRequest, f.mCheck).empty());
        EXPECT_TRUE(runtime->obtain(f.mRequest, f.mCheck).empty());
        EXPECT_EQ(checks, 1);
        EXPECT_EQ(compiler->mCompiles, 1);
        EXPECT_EQ(f.cacheFiles(), 0u);
        ASSERT_EQ(f.log().size(), 1u);
        EXPECT_NE(f.log()[0].find("in location=9"), std::string::npos);
    }

    TEST(SlangRuntime, a_cache_hit_is_not_checked_again)
    {
        Fixture f;
        FakeCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        int checks = 0;
        f.mCheck.mCheck = [&checks](const std::vector<uint32_t>&) {
            ++checks;
            return std::string();
        };
        runtime->obtain(f.mRequest, f.mCheck);
        FakeCompiler* other = nullptr;
        auto next = f.runtime(other);
        EXPECT_EQ(next->obtain(f.mRequest, f.mCheck), fakeWords(1));
        EXPECT_EQ(checks, 1);
    }

    TEST(SlangRuntime, a_damaged_or_foreign_cache_file_is_a_miss)
    {
        Fixture f;
        FakeCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        runtime->obtain(f.mRequest, f.mCheck);
        std::filesystem::path file;
        for (const auto& entry : std::filesystem::directory_iterator(f.cache()))
            file = entry.path();
        ASSERT_FALSE(file.empty());
        std::string bytes;
        {
            std::ifstream in(file, std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        const auto rewrite = [&](const std::string& text) {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
        };
        const auto compilesAfter = [&]() {
            FakeCompiler* other = nullptr;
            auto next = f.runtime(other);
            const auto words = next->obtain(f.mRequest, f.mCheck);
            EXPECT_EQ(words, fakeWords(1));
            return other->mCompiles.load();
        };
        {
            SCOPED_TRACE("cut short");
            rewrite(bytes.substr(0, bytes.size() - 3));
            EXPECT_EQ(compilesAfter(), 1);
        }
        {
            SCOPED_TRACE("a flipped word");
            std::string flipped = bytes;
            flipped[flipped.size() - 12] ^= 0x40;
            rewrite(flipped);
            EXPECT_EQ(compilesAfter(), 1);
        }
        {
            SCOPED_TRACE("empty");
            rewrite("");
            EXPECT_EQ(compilesAfter(), 1);
        }
        {
            SCOPED_TRACE("another key under this name");
            std::string other = bytes;
            // The key text starts after the magic and its length; change its first byte.
            other[6 + 8] ^= 0x01;
            rewrite(other);
            EXPECT_EQ(compilesAfter(), 1);
        }
        {
            SCOPED_TRACE("intact");
            rewrite(bytes);
            EXPECT_EQ(compilesAfter(), 0);
        }
    }

    TEST(SlangRuntime, without_a_cache_folder_nothing_is_written)
    {
        Fixture f;
        FakeCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler, false);
        EXPECT_EQ(runtime->obtain(f.mRequest, f.mCheck), fakeWords(1));
        EXPECT_EQ(runtime->obtain(f.mRequest, f.mCheck), fakeWords(1));
        EXPECT_EQ(compiler->mCompiles, 2);
        EXPECT_FALSE(std::filesystem::exists(f.cache()));
    }

    TEST(SlangRuntime, a_cache_folder_that_cannot_be_written_still_gives_the_result_and_says_so_once)
    {
        Fixture f;
        f.write("blocker", "a file where the folder should be");
        FakeCompiler* compiler = nullptr;
        auto fake = std::make_unique<FakeCompiler>();
        compiler = fake.get();
        auto runtime = SlangRuntime::create(std::move(fake), f.mRoot / "blocker" / "cache");
        runtime->setLog([&f](bool, const std::string& line) { f.write("log.txt", line); });
        EXPECT_EQ(runtime->obtain(f.mRequest, f.mCheck), fakeWords(1));
        EXPECT_EQ(compiler->mCompiles, 1);
        EXPECT_TRUE(std::filesystem::exists(f.mRoot / "log.txt"));
    }

    TEST(SlangRuntime, a_compiler_with_no_identity_disables_the_runtime_and_says_so_once)
    {
        Fixture f;
        FakeCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        compiler->mIdentity.clear();
        EXPECT_TRUE(runtime->obtain(f.mRequest, f.mCheck).empty());
        EXPECT_TRUE(runtime->disabled());
        EXPECT_TRUE(runtime->obtain(f.mRequest, f.mCheck).empty());
        EXPECT_EQ(compiler->mCompiles, 0);
        EXPECT_EQ(f.log().size(), 1u);
    }

    TEST(SlangRuntime, the_self_test_must_reproduce_the_builds_permutation_before_anything_is_compiled)
    {
        Fixture f;
        SlangRuntimeRequest test = f.mRequest;
        test.mStageFile = "post/omwfx.vert";
        {
            SCOPED_TRACE("it matches");
            FakeCompiler* compiler = nullptr;
            auto runtime = f.runtime(compiler);
            runtime->setSelfTest(test, fakeWords(1));
            EXPECT_EQ(runtime->obtain(f.mRequest, f.mCheck), fakeWords(1));
            // The self-test compiled once, and then the request itself.
            EXPECT_EQ(compiler->mCompiles, 2);
            EXPECT_EQ(compiler->mSeen[0].mStageFile, "post/omwfx.vert");
            EXPECT_FALSE(runtime->disabled());
        }
        std::filesystem::remove_all(f.cache());
        {
            SCOPED_TRACE("it does not match");
            FakeCompiler* compiler = nullptr;
            auto runtime = f.runtime(compiler);
            runtime->setSelfTest(test, fakeWords(99));
            EXPECT_TRUE(runtime->obtain(f.mRequest, f.mCheck).empty());
            EXPECT_TRUE(runtime->disabled());
            EXPECT_EQ(compiler->mCompiles, 1);
            EXPECT_TRUE(runtime->obtain(f.mRequest, f.mCheck).empty());
            EXPECT_EQ(compiler->mCompiles, 1);
            EXPECT_EQ(f.cacheFiles(), 0u);
            EXPECT_EQ(f.log().size(), 1u);
            EXPECT_NE(f.log()[0].find("does not reproduce"), std::string::npos);
        }
        std::filesystem::remove_all(f.cache());
        {
            SCOPED_TRACE("it does not compile");
            FakeCompiler* compiler = nullptr;
            auto runtime = f.runtime(compiler);
            runtime->setSelfTest(test, fakeWords(1));
            compiler->mOutput.clear();
            compiler->mError = "cannot load the Slang library";
            EXPECT_TRUE(runtime->obtain(f.mRequest, f.mCheck).empty());
            EXPECT_TRUE(runtime->disabled());
        }
    }

    TEST(SlangRuntime, a_cache_hit_needs_no_self_test_and_no_compiler)
    {
        Fixture f;
        FakeCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        runtime->obtain(f.mRequest, f.mCheck);
        FakeCompiler* other = nullptr;
        auto next = f.runtime(other);
        SlangRuntimeRequest test = f.mRequest;
        test.mStageFile = "post/omwfx.vert";
        next->setSelfTest(test, fakeWords(99)); // would fail, if it ran
        EXPECT_EQ(next->obtain(f.mRequest, f.mCheck), fakeWords(1));
        EXPECT_EQ(other->mCompiles, 0);
        EXPECT_FALSE(next->disabled());
    }

    TEST(SlangRuntime, threads_asking_for_the_same_set_compile_it_once)
    {
        Fixture f;
        FakeCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        std::vector<std::thread> threads;
        std::vector<std::vector<uint32_t>> results(8);
        for (std::size_t i = 0; i < results.size(); ++i)
            threads.emplace_back([&, i] { results[i] = runtime->obtain(f.mRequest, f.mCheck); });
        for (auto& thread : threads)
            thread.join();
        for (const auto& result : results)
            EXPECT_EQ(result, fakeWords(1));
        EXPECT_EQ(compiler->mCompiles, 1);
    }

    TEST(SlangRuntime, threads_asking_for_different_sets_never_compile_at_the_same_time)
    {
        Fixture f;
        FakeCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        std::atomic<int> inside{ 0 };
        std::atomic<int> most{ 0 };
        struct Probe final : vsgUtil::SlangRuntimeCompiler
        {
            std::atomic<int>& mInside;
            std::atomic<int>& mMost;
            Probe(std::atomic<int>& inside, std::atomic<int>& most)
                : mInside(inside)
                , mMost(most)
            {
            }
            std::string cacheIdentity() override { return "probe"; }
            std::vector<uint32_t> compile(const SlangRuntimeRequest&, std::string&) override
            {
                const int now = ++mInside;
                int seen = mMost.load();
                while (now > seen && !mMost.compare_exchange_weak(seen, now))
                {
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                --mInside;
                return fakeWords(7);
            }
        };
        auto probing = SlangRuntime::create(std::make_unique<Probe>(inside, most), std::filesystem::path());
        std::vector<std::thread> threads;
        for (int i = 0; i < 6; ++i)
        {
            threads.emplace_back([&, i] {
                auto request = f.mRequest;
                request.mDefines.insert("SET_" + std::to_string(i));
                probing->obtain(request, f.mCheck);
            });
        }
        for (auto& thread : threads)
            thread.join();
        EXPECT_EQ(most.load(), 1) << "one Slang global session is not thread safe";
    }

    TEST(SlangRuntime, a_runtime_that_did_something_says_what_it_did_when_it_ends)
    {
        Fixture f;
        std::vector<std::string> lines;
        {
            FakeCompiler* compiler = nullptr;
            auto fake = std::make_unique<FakeCompiler>();
            compiler = fake.get();
            auto runtime = SlangRuntime::create(std::move(fake), f.cache());
            runtime->setLog(
                [&lines](bool warning, const std::string& line) { lines.push_back((warning ? "W " : "I ") + line); });
            runtime->obtain(f.mRequest, f.mCheck);
            runtime->obtain(f.mRequest, f.mCheck);
            EXPECT_TRUE(lines.empty());
            EXPECT_EQ(compiler->mCompiles, 1);
        }
        ASSERT_EQ(lines.size(), 1u);
        EXPECT_EQ(lines[0].rfind("I Slang run-time shaders: 1 compiled", 0), 0u) << lines[0];
        EXPECT_NE(lines[0].find("1 read from the cache"), std::string::npos) << lines[0];
        // A runtime that was never asked says nothing.
        lines.clear();
        {
            auto runtime = SlangRuntime::create(std::make_unique<FakeCompiler>(), f.cache());
            runtime->setLog([&lines](bool, const std::string& line) { lines.push_back(line); });
        }
        EXPECT_TRUE(lines.empty());
    }

    TEST(SlangStageInfo, is_source_entry_and_stage_separated_by_tabs)
    {
        const auto info = vsgUtil::parseSlangStageInfo("default/default.frag.slang\tfragMain\tfragment\n");
        EXPECT_EQ(info.mSource, "default/default.frag.slang");
        EXPECT_EQ(info.mEntry, "fragMain");
        EXPECT_EQ(info.mStage, "fragment");
        EXPECT_NO_THROW(vsgUtil::parseSlangStageInfo("a.slang\tm\tvertex\r\n"));
        for (const char* bad : { "", "a.slang\tmain\n", "a.slang\tmain\tvertex\textra\n", "\tmain\tvertex\n",
                 "../a.slang\tmain\tvertex\n", "/a.slang\tmain\tvertex\n", "C:/a.slang\tmain\tvertex\n",
                 "a\\b.slang\tmain\tvertex\n", "a.slang\tmain\tvertex\nb.slang\tmain\tvertex\n" })
        {
            EXPECT_THROW(vsgUtil::parseSlangStageInfo(bad), std::invalid_argument) << bad;
        }
    }

    // ---- Review of stage h, round 4 (findings 2, 3, 5 and 7): written before the implementation. ----

    // A compiler that records the thread of every call, can be told to wait inside prepare(), and fails on request.
    class ScriptedCompiler final : public vsgUtil::SlangRuntimeCompiler
    {
    public:
        std::atomic<int> mCompiles{ 0 };
        std::atomic<int> mPrepares{ 0 };
        std::string mIdentity = "scripted-compiler-1";
        std::vector<uint32_t> mOutput = fakeWords(1);
        std::string mError;
        bool mPrepareOk = true;
        std::function<void()> mInPrepare;
        std::mutex mMutex;
        std::vector<std::thread::id> mCompileThreads;
        std::vector<std::string> mCompiledStages;
        std::thread::id mPrepareThread;

        std::string cacheIdentity() override { return mIdentity; }
        bool prepare(std::string& error) override
        {
            ++mPrepares;
            mPrepareThread = std::this_thread::get_id();
            if (mInPrepare)
                mInPrepare();
            if (!mPrepareOk)
                error = "cannot load the Slang library";
            return mPrepareOk;
        }
        std::vector<uint32_t> compile(const SlangRuntimeRequest& request, std::string& error) override
        {
            ++mCompiles;
            {
                std::lock_guard lock(mMutex);
                mCompileThreads.push_back(std::this_thread::get_id());
                mCompiledStages.push_back(request.mStageFile);
            }
            error = mError;
            return mOutput;
        }
    };

    // Test-side copy of the cache entry format (components/vsgutil/slangruntime.cpp): the magic, the key size, the key,
    // the word count, the words, and a 64-bit FNV-1a of everything before it. The hash is not secret, so a hostile
    // file can have a right one.
    void writeEntry(const std::filesystem::path& file, const std::string& key, const std::vector<uint32_t>& words,
        const char* magic = "OEPC1\n")
    {
        std::string bytes(magic);
        const std::uint64_t keySize = key.size();
        bytes.append(reinterpret_cast<const char*>(&keySize), sizeof(keySize));
        bytes += key;
        const std::uint64_t count = words.size();
        bytes.append(reinterpret_cast<const char*>(&count), sizeof(count));
        bytes.append(reinterpret_cast<const char*>(words.data()), words.size() * sizeof(uint32_t));
        std::uint64_t hash = 1469598103934665603ull;
        for (const char ch : bytes)
        {
            hash ^= static_cast<unsigned char>(ch);
            hash *= 1099511628211ull;
        }
        bytes.append(reinterpret_cast<const char*>(&hash), sizeof(hash));
        std::filesystem::create_directories(file.parent_path());
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    // A tiny SPIR-V module the readers can parse: a capability, the memory model, an int type and one specialization
    // constant; with `specConstantOp` also an OpSpecConstantOp (IMul of the constant with itself).
    std::vector<uint32_t> moduleWords(uint32_t version, bool specConstantOp)
    {
        std::vector<uint32_t> words = { 0x07230203u, version, 0u, 8u, 0u };
        const auto op = [&words](uint32_t opcode, std::initializer_list<uint32_t> operands) {
            words.push_back(static_cast<uint32_t>((operands.size() + 1) << 16) | opcode);
            words.insert(words.end(), operands);
        };
        op(17, { 1 }); // OpCapability Shader
        op(14, { 0, 1 }); // OpMemoryModel Logical GLSL450
        op(21, { 1, 32, 1 }); // OpTypeInt %1 32 1
        op(50, { 1, 2, 1 }); // OpSpecConstant %1 %2 1
        if (specConstantOp)
            op(52, { 1, 3, 132, 2, 2 }); // OpSpecConstantOp %1 %3 IMul %2 %2
        return words;
    }

    // The production hit check, as runtimeTwin hands it to the runtime.
    SlangRuntimeCheck productionHitCheck()
    {
        SlangRuntimeCheck check;
        check.mCheckHit = [](const std::vector<uint32_t>& words) { return vsgUtil::checkSlangTwinStructure(words); };
        return check;
    }

    TEST(SlangCacheValidation, the_structure_check_accepts_a_spirv_1_0_module_and_refuses_the_rest)
    {
        EXPECT_EQ(vsgUtil::checkSlangTwinStructure(moduleWords(0x00010000u, false)), "");
        const std::string newer = vsgUtil::checkSlangTwinStructure(moduleWords(0x00010300u, false));
        EXPECT_NE(newer, "");
        EXPECT_NE(newer.find("1.0"), std::string::npos) << newer;
        const std::string op = vsgUtil::checkSlangTwinStructure(moduleWords(0x00010000u, true));
        EXPECT_NE(op.find("OpSpecConstantOp"), std::string::npos) << op;
        EXPECT_NE(vsgUtil::checkSlangTwinStructure({}), "");
        EXPECT_NE(vsgUtil::checkSlangTwinStructure(fakeWords(1)), "") << "an instruction of length 0";
        auto cut = moduleWords(0x00010000u, false);
        cut[cut.size() - 4] |= 0x00FF0000u; // the last instruction (OpSpecConstant, 4 words) claims 255
        EXPECT_NE(vsgUtil::checkSlangTwinStructure(cut), "");
    }

    TEST(SlangCacheValidation, a_hit_the_hit_check_refuses_is_a_miss_that_is_compiled_and_replaced)
    {
        Fixture f;
        SlangRuntimeCheck refusing;
        refusing.mCheckHit = [](const std::vector<uint32_t>&) { return std::string("not 1.0"); };
        SlangRuntimeCheck accepting;
        accepting.mCheckHit = [](const std::vector<uint32_t>&) { return std::string(); };
        FakeCompiler* first = nullptr;
        auto firstRuntime = f.runtime(first);
        EXPECT_EQ(firstRuntime->obtain(f.mRequest, SlangRuntimeCheck{}), fakeWords(1));
        EXPECT_EQ(first->mCompiles, 1);
        FakeCompiler* second = nullptr;
        auto secondRuntime = f.runtime(second);
        EXPECT_EQ(secondRuntime->obtain(f.mRequest, refusing), fakeWords(1));
        EXPECT_EQ(second->mCompiles, 1)
            << "a refused entry is compiled again (a fresh compile is not subject to the hit check)";
        ASSERT_EQ(f.log().size(), 1u);
        EXPECT_NE(f.log()[0].find("not 1.0"), std::string::npos) << f.log()[0];
        EXPECT_NE(f.log()[0].find("cache"), std::string::npos) << f.log()[0];
        FakeCompiler* third = nullptr;
        auto thirdRuntime = f.runtime(third);
        EXPECT_EQ(thirdRuntime->obtain(f.mRequest, accepting), fakeWords(1));
        EXPECT_EQ(third->mCompiles, 0) << "the replacement is a hit";
    }

    TEST(SlangCacheValidation, a_hostile_file_with_a_right_hash_is_a_miss_under_the_production_check)
    {
        for (const auto& [name, bad] : std::vector<std::pair<std::string, std::vector<uint32_t>>>{
                 { "SPIR-V 1.3", moduleWords(0x00010300u, false) },
                 { "an OpSpecConstantOp", moduleWords(0x00010000u, true) },
                 { "an instruction of length 0", fakeWords(1) } })
        {
            SCOPED_TRACE(name);
            Fixture f;
            FakeCompiler* compiler = nullptr;
            auto runtime = f.runtime(compiler);
            compiler->mOutput = moduleWords(0x00010000u, false);
            ASSERT_EQ(runtime->obtain(f.mRequest, productionHitCheck()), compiler->mOutput);
            std::filesystem::path file;
            for (const auto& entry : std::filesystem::directory_iterator(f.cache()))
                file = entry.path();
            ASSERT_FALSE(file.empty());
            writeEntry(file, runtime->keyText(f.mRequest, productionHitCheck()), bad);
            FakeCompiler* other = nullptr;
            auto next = f.runtime(other);
            other->mOutput = moduleWords(0x00010000u, false);
            EXPECT_EQ(next->obtain(f.mRequest, productionHitCheck()), other->mOutput);
            EXPECT_EQ(other->mCompiles, 1) << "the hostile entry must not be used";
            FakeCompiler* last = nullptr;
            auto again = f.runtime(last);
            EXPECT_EQ(again->obtain(f.mRequest, productionHitCheck()), other->mOutput);
            EXPECT_EQ(last->mCompiles, 0) << "the replacement entry is a hit";
        }
    }

    TEST(SlangCacheValidation, the_key_carries_the_version_of_the_checks)
    {
        Fixture f;
        FakeCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        const std::string key = runtime->keyText(f.mRequest, f.mCheck);
        EXPECT_NE(
            key.find("\ncheckversion " + std::to_string(vsgUtil::slangCacheCheckVersion) + "\n"), std::string::npos)
            << key;
    }

    TEST(SlangCacheValidation, the_size_of_an_entry_is_bounded_by_its_key_and_the_word_limit)
    {
        // magic (6) + key size (8) + key + count (8) + 16 Mi words + hash (8)
        EXPECT_EQ(SlangRuntime::maxCacheFileBytes(100), 6u + 8u + 100u + 8u + 4ull * 16u * 1024u * 1024u + 8u);
        Fixture f;
        FakeCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        runtime->obtain(f.mRequest, f.mCheck);
        const std::string key = runtime->keyText(f.mRequest, f.mCheck);
        std::filesystem::path file;
        for (const auto& entry : std::filesystem::directory_iterator(f.cache()))
            file = entry.path();
        EXPECT_LE(std::filesystem::file_size(file), SlangRuntime::maxCacheFileBytes(key.size()));
    }

    TEST(SlangRefusedSets, a_set_the_check_refused_is_remembered_on_disk)
    {
        Fixture f;
        int checks = 0;
        f.mCheck.mCheck = [&checks](const std::vector<uint32_t>&) {
            ++checks;
            return std::string("the twin declares what the GLSL does not: in location=9 vec4f32");
        };
        {
            FakeCompiler* compiler = nullptr;
            auto runtime = f.runtime(compiler);
            EXPECT_TRUE(runtime->obtain(f.mRequest, f.mCheck).empty());
            EXPECT_EQ(compiler->mCompiles, 1);
            EXPECT_EQ(f.log().size(), 1u);
        }
        {
            // Another run of the game: nothing is compiled or checked, and the set is not logged as a new failure.
            FakeCompiler* compiler = nullptr;
            auto runtime = f.runtime(compiler);
            EXPECT_TRUE(runtime->obtain(f.mRequest, f.mCheck).empty());
            EXPECT_EQ(compiler->mCompiles, 0);
            EXPECT_EQ(checks, 1);
            EXPECT_EQ(runtime->stats().mRefusedSets, 1u);
            EXPECT_EQ(runtime->stats().mFailed, 0u);
            EXPECT_EQ(f.log().size(), 1u);
        }
        {
            // A different compiler (its identity is in the key) tries again: the refusal belongs to this key only.
            FakeCompiler* compiler = nullptr;
            auto runtime = f.runtime(compiler);
            compiler->mIdentity = "fake-compiler-2";
            EXPECT_TRUE(runtime->obtain(f.mRequest, f.mCheck).empty());
            EXPECT_EQ(compiler->mCompiles, 1);
        }
        {
            // So does an edit of a .slang file.
            f.write("shaders/lib/b.slang", "// b edited\n");
            FakeCompiler* compiler = nullptr;
            auto runtime = f.runtime(compiler);
            EXPECT_TRUE(runtime->obtain(f.mRequest, f.mCheck).empty());
            EXPECT_EQ(compiler->mCompiles, 1);
        }
    }

    TEST(SlangRefusedSets, a_failed_compile_is_not_remembered_on_disk)
    {
        Fixture f;
        {
            FakeCompiler* compiler = nullptr;
            auto runtime = f.runtime(compiler);
            compiler->mOutput.clear();
            compiler->mError = "cannot load the Slang library";
            EXPECT_TRUE(runtime->obtain(f.mRequest, f.mCheck).empty());
        }
        FakeCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        EXPECT_EQ(runtime->obtain(f.mRequest, f.mCheck), fakeWords(1));
        EXPECT_EQ(compiler->mCompiles, 1) << "a library that was missing once may be there next time";
    }

    TEST(SlangCacheCleaning, removes_what_this_runtime_cannot_use_again_and_nothing_else)
    {
        Fixture f;
        const auto entries = [&f] {
            std::set<std::string> names;
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(f.cache(), ec))
                names.insert(entry.path().filename().string());
            return names;
        };
        // An entry of another compiler, one made before an edit of a .slang file, and the current one.
        FakeCompiler* oldCompiler = nullptr;
        auto oldRuntime = f.runtime(oldCompiler);
        oldCompiler->mIdentity = "fake-compiler-old";
        oldRuntime->obtain(f.mRequest, f.mCheck);
        FakeCompiler* beforeEdit = nullptr;
        auto beforeEditRuntime = f.runtime(beforeEdit);
        auto other = f.mRequest;
        other.mDefines.insert("NORMAL");
        beforeEditRuntime->obtain(other, f.mCheck);
        f.write("shaders/lib/b.slang", "// b edited\n");
        FakeCompiler* current = nullptr;
        auto runtime = f.runtime(current);
        runtime->obtain(f.mRequest, f.mCheck);
        const std::string currentName = SlangRuntime::fileNameOf(runtime->keyText(f.mRequest, f.mCheck));
        ASSERT_EQ(entries().size(), 3u);
        // A temporary file a crash left, an hour old; a fresh one (another process may be writing it); an unrelated
        // file.
        f.write("cache/aaaa.spv.tmp1", "x");
        std::filesystem::last_write_time(
            f.cache() / "aaaa.spv.tmp1", std::filesystem::file_time_type::clock::now() - std::chrono::hours(1));
        f.write("cache/bbbb.spv.tmp2", "x");
        f.write("cache/notes.txt", "mine");
        const auto result = runtime->cleanCache(f.mRoot / "shaders");
        EXPECT_EQ(result.mRemoved, 3u)
            << "the entry of the old compiler, the one before the edit and the old temp file";
        EXPECT_EQ(entries(), (std::set<std::string>{ currentName, "bbbb.spv.tmp2", "notes.txt" }));
        EXPECT_GT(result.mBytesKept, 0u);
        EXPECT_EQ(runtime->stats().mCleaned, 3u);
    }

    TEST(SlangCacheCleaning, removes_the_oldest_entries_above_the_size_cap)
    {
        Fixture f;
        FakeCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        std::vector<std::string> names;
        for (int i = 0; i < 3; ++i)
        {
            auto request = f.mRequest;
            request.mDefines.insert("SET_" + std::to_string(i));
            runtime->obtain(request, f.mCheck);
            names.push_back(SlangRuntime::fileNameOf(runtime->keyText(request, f.mCheck)));
            // Entry i is i hours old... reversed: entry 0 is the oldest.
            std::filesystem::last_write_time(
                f.cache() / names.back(), std::filesystem::file_time_type::clock::now() - std::chrono::hours(10 - i));
        }
        const auto size = std::filesystem::file_size(f.cache() / names[2]);
        const auto result = runtime->cleanCache(f.mRoot / "shaders", size * 2 + 1);
        EXPECT_EQ(result.mRemoved, 1u);
        EXPECT_FALSE(std::filesystem::exists(f.cache() / names[0])) << "the oldest goes first";
        EXPECT_TRUE(std::filesystem::exists(f.cache() / names[1]));
        EXPECT_TRUE(std::filesystem::exists(f.cache() / names[2]));
    }

    TEST(SlangCacheCleaning, a_file_of_the_cache_folder_that_is_not_an_entry_is_removed_if_it_looks_like_one)
    {
        Fixture f;
        FakeCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        runtime->obtain(f.mRequest, f.mCheck);
        f.write("cache/00000000000000000000000000000000.spv", "not an entry");
        const auto result = runtime->cleanCache(f.mRoot / "shaders");
        EXPECT_EQ(result.mRemoved, 1u);
        EXPECT_EQ(f.cacheFiles(), 1u);
    }

    TEST(SlangSelfTests, every_test_must_reproduce_its_permutation_in_the_order_added)
    {
        Fixture f;
        SlangRuntimeRequest small = f.mRequest;
        small.mStageFile = "post/omwfx.vert";
        SlangRuntimeRequest large = f.mRequest;
        large.mStageFile = "default/main.frag";
        {
            SCOPED_TRACE("both match");
            auto fake = std::make_unique<ScriptedCompiler>();
            auto* compiler = fake.get();
            auto runtime = SlangRuntime::create(std::move(fake), f.cache());
            runtime->setLog([&f](bool, const std::string& line) { f.write("log.txt", line); });
            runtime->addSelfTest(small, fakeWords(1));
            runtime->addSelfTest(large, fakeWords(1));
            EXPECT_EQ(runtime->obtain(f.mRequest, f.mCheck), fakeWords(1));
            EXPECT_EQ(compiler->mCompiledStages,
                (std::vector<std::string>{ "post/omwfx.vert", "default/main.frag", "default/main.frag" }));
            EXPECT_FALSE(runtime->disabled());
        }
        std::filesystem::remove_all(f.cache());
        {
            SCOPED_TRACE("the second does not match");
            auto fake = std::make_unique<ScriptedCompiler>();
            auto* compiler = fake.get();
            auto runtime = SlangRuntime::create(std::move(fake), f.cache());
            runtime->setLog([&f](bool, const std::string& line) { f.write("log.txt", line); });
            runtime->addSelfTest(small, fakeWords(1));
            runtime->addSelfTest(large, fakeWords(99));
            EXPECT_TRUE(runtime->obtain(f.mRequest, f.mCheck).empty());
            EXPECT_TRUE(runtime->disabled());
            EXPECT_EQ(compiler->mCompiles, 2) << "the request itself is never compiled";
            EXPECT_EQ(f.cacheFiles(), 0u);
        }
        {
            SCOPED_TRACE("setSelfTest replaces the earlier ones");
            auto fake = std::make_unique<ScriptedCompiler>();
            auto* compiler = fake.get();
            auto runtime = SlangRuntime::create(std::move(fake), std::filesystem::path());
            runtime->addSelfTest(large, fakeWords(99));
            runtime->setSelfTest(small, fakeWords(1));
            EXPECT_EQ(runtime->obtain(f.mRequest, f.mCheck), fakeWords(1));
            EXPECT_EQ(compiler->mCompiledStages, (std::vector<std::string>{ "post/omwfx.vert", "default/main.frag" }));
        }
    }

    TEST(SlangPreload, prepares_the_compiler_and_runs_the_self_tests_on_a_worker_thread)
    {
        Fixture f;
        auto fake = std::make_unique<ScriptedCompiler>();
        auto* compiler = fake.get();
        auto runtime = SlangRuntime::create(std::move(fake), f.cache());
        runtime->setLog([&f](bool, const std::string& line) { f.write("log.txt", line); });
        SlangRuntimeRequest test = f.mRequest;
        test.mStageFile = "post/omwfx.vert";
        runtime->setSelfTest(test, fakeWords(1));
        runtime->startPreload();
        runtime->startPreload(); // twice: nothing happens
        runtime->waitForPreload();
        EXPECT_EQ(compiler->mPrepares, 1);
        EXPECT_NE(compiler->mPrepareThread, std::this_thread::get_id());
        ASSERT_EQ(compiler->mCompileThreads.size(), 1u) << "the self-test";
        EXPECT_EQ(compiler->mCompileThreads[0], compiler->mPrepareThread);
        EXPECT_TRUE(runtime->stats().mPreloaded);
        // The first compile finds everything done: it compiles its request and nothing else, on this thread.
        EXPECT_EQ(runtime->obtain(f.mRequest, f.mCheck), fakeWords(1));
        EXPECT_EQ(compiler->mPrepares, 1);
        EXPECT_EQ(compiler->mCompiles, 2);
        EXPECT_EQ(compiler->mCompileThreads[1], std::this_thread::get_id());
        EXPECT_FALSE(runtime->disabled());
        const auto stats = runtime->stats();
        EXPECT_EQ(stats.mCompiledOnMain, 1u);
        EXPECT_EQ(stats.mCompiledOnOther, 0u);
    }

    TEST(SlangPreload, a_compile_that_comes_during_the_preload_waits_for_it_and_the_wait_is_counted)
    {
        Fixture f;
        auto fake = std::make_unique<ScriptedCompiler>();
        auto* compiler = fake.get();
        std::promise<void> release;
        std::promise<void> inside;
        auto released = release.get_future().share();
        compiler->mInPrepare = [&] {
            inside.set_value();
            released.wait();
        };
        auto runtime = SlangRuntime::create(std::move(fake), f.cache());
        runtime->startPreload();
        inside.get_future().wait(); // the worker holds the compile lock
        std::thread releaser([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            release.set_value();
        });
        EXPECT_EQ(runtime->obtain(f.mRequest, f.mCheck), fakeWords(1));
        releaser.join();
        const auto stats = runtime->stats();
        EXPECT_GE(stats.mLongestWaitMilliseconds, 80.0);
        EXPECT_TRUE(stats.mLongestWaitWasMain);
        EXPECT_EQ(compiler->mPrepares, 1) << "the first compile does not prepare again";
    }

    TEST(SlangPreload, a_cache_hit_does_not_wait_for_the_preload)
    {
        Fixture f;
        {
            FakeCompiler* compiler = nullptr;
            f.runtime(compiler)->obtain(f.mRequest, f.mCheck);
        }
        auto fake = std::make_unique<ScriptedCompiler>();
        auto* compiler = fake.get();
        std::promise<void> release;
        std::promise<void> inside;
        auto released = release.get_future().share();
        compiler->mInPrepare = [&] {
            inside.set_value();
            released.wait();
        };
        // The key must match the earlier run's: the same identity text as FakeCompiler's.
        compiler->mIdentity = "fake-compiler-1";
        auto runtime = SlangRuntime::create(std::move(fake), f.cache());
        runtime->startPreload();
        inside.get_future().wait();
        EXPECT_EQ(runtime->obtain(f.mRequest, f.mCheck), fakeWords(1)) << "answered while the preload is still blocked";
        EXPECT_EQ(compiler->mCompiles, 0);
        EXPECT_EQ(runtime->stats().mCacheHits, 1u);
        release.set_value();
        runtime->waitForPreload();
    }

    TEST(SlangPreload, a_compiler_that_cannot_start_disables_the_runtime_and_says_so_once)
    {
        Fixture f;
        auto fake = std::make_unique<ScriptedCompiler>();
        auto* compiler = fake.get();
        compiler->mPrepareOk = false;
        std::mutex mutex;
        std::vector<std::string> warnings;
        auto runtime = SlangRuntime::create(std::move(fake), f.cache());
        runtime->setLog([&](bool warning, const std::string& line) {
            std::lock_guard lock(mutex);
            if (warning)
                warnings.push_back(line);
        });
        runtime->startPreload();
        runtime->waitForPreload();
        EXPECT_TRUE(runtime->disabled());
        EXPECT_TRUE(runtime->obtain(f.mRequest, f.mCheck).empty());
        EXPECT_TRUE(runtime->obtain(f.mRequest, f.mCheck).empty());
        EXPECT_EQ(compiler->mCompiles, 0);
        EXPECT_EQ(compiler->mPrepares, 1);
        ASSERT_EQ(warnings.size(), 1u);
        EXPECT_NE(warnings[0].find("cannot load the Slang library"), std::string::npos) << warnings[0];
    }

    TEST(SlangPreload, the_destructor_waits_for_a_running_preload)
    {
        Fixture f;
        auto finished = std::make_shared<std::atomic<bool>>(false);
        {
            auto fake = std::make_unique<ScriptedCompiler>();
            fake->mInPrepare = [finished] {
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                finished->store(true);
            };
            auto runtime = SlangRuntime::create(std::move(fake), f.cache());
            runtime->startPreload();
        }
        EXPECT_TRUE(finished->load());
    }

    TEST(SlangPreload, cleans_the_cache_folder_for_the_shader_root_of_the_self_test)
    {
        Fixture f;
        {
            FakeCompiler* compiler = nullptr;
            auto runtime = f.runtime(compiler);
            compiler->mIdentity = "fake-compiler-old";
            runtime->obtain(f.mRequest, f.mCheck);
        }
        ASSERT_EQ(f.cacheFiles(), 1u);
        auto fake = std::make_unique<ScriptedCompiler>();
        auto runtime = SlangRuntime::create(std::move(fake), f.cache());
        runtime->setSelfTest(f.mRequest, fakeWords(1));
        runtime->startPreload();
        runtime->waitForPreload();
        EXPECT_EQ(f.cacheFiles(), 0u) << "the entry of another compiler is gone";
        EXPECT_EQ(runtime->stats().mCleaned, 1u);
    }

    TEST(SlangPreload, the_summary_says_where_the_time_went)
    {
        Fixture f;
        std::vector<std::string> lines;
        {
            auto runtime = SlangRuntime::create(std::make_unique<ScriptedCompiler>(), f.cache());
            runtime->setLog([&lines](bool warning, const std::string& line) {
                if (!warning)
                    lines.push_back(line);
            });
            runtime->startPreload();
            runtime->waitForPreload();
            runtime->obtain(f.mRequest, f.mCheck);
            std::thread other([&] {
                auto request = f.mRequest;
                request.mDefines.insert("NORMAL");
                runtime->obtain(request, f.mCheck);
            });
            other.join();
            const auto stats = runtime->stats();
            EXPECT_EQ(stats.mCompiledOnMain, 1u);
            EXPECT_EQ(stats.mCompiledOnOther, 1u);
        }
        ASSERT_EQ(lines.size(), 1u);
        EXPECT_NE(lines[0].find("2 compiled"), std::string::npos) << lines[0];
        EXPECT_NE(lines[0].find("1 on the main thread"), std::string::npos) << lines[0];
        EXPECT_NE(lines[0].find("1 on other threads"), std::string::npos) << lines[0];
        EXPECT_NE(lines[0].find("longest wait on the compile lock"), std::string::npos) << lines[0];
        EXPECT_NE(lines[0].find("start-up preload"), std::string::npos) << lines[0];
    }

#if OPENEDEN_SLANG_ENABLED && defined(OPENEDEN_SLANG_LIBRARY_DIR) && defined(OPENEDEN_SLANG_RUNTIME_TESTS)
    std::vector<uint32_t> readWords(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in)
            return {};
        const std::streamsize size = in.tellg();
        std::vector<uint32_t> words(static_cast<std::size_t>(size) / sizeof(uint32_t));
        in.seekg(0);
        in.read(reinterpret_cast<char*>(words.data()), size);
        return words;
    }

    std::string readText(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    bool libraryPresent()
    {
        const std::filesystem::path dir(OPENEDEN_SLANG_LIBRARY_DIR);
        return std::filesystem::exists(dir / "slang.dll") || std::filesystem::exists(dir / "libslang-compiler.so");
    }

    // The options the game's shader reader has: the built shaders as search path and the settings reader.
    vsg::ref_ptr<vsg::Options> gameOptions(const std::set<std::string>& settingsDefines = {})
    {
        auto options = vsg::Options::create();
        options->paths = { OPENEDEN_SHADER_BUILD_DIR };
        vsg::ref_ptr<Resource::ShaderSettings> settings{ new Resource::ShaderSettings };
        for (const std::string& name : settingsDefines)
        {
            const std::string file = name == "DEBUG_CHUNKS" ? "terrain.settings" : "shadow.settings";
            auto& object = settings->objects[file];
            const auto* module = dynamic_cast<const vsg::ShaderModule*>(object.get());
            std::string text = module ? module->source : std::string();
            if (!text.empty() && text.back() != '\n')
                text += '\n';
            object = vsg::ShaderModule::create(text + "#define " + name + " 1\n");
        }
        options->readerWriters = { settings, vsg::glsl::create(), vsg::spirv::create() };
        options->sharedObjects = vsg::SharedObjects::create();
        return options;
    }

    vsg::ref_ptr<SlangRuntime> libraryRuntime(const std::filesystem::path& cache)
    {
        auto options = gameOptions();
        auto runtime = SlangRuntime::create(vsgUtil::createDynamicSlangCompiler(OPENEDEN_SLANG_LIBRARY_DIR), cache);
        runtime->setLog([](bool warning, const std::string& line) {
            if (warning)
            {
                ADD_FAILURE() << line;
            }
        });
        return runtime;
    }

    // Every "<stage file>.permutations" under the built shaders: (stage file, canonical set, built file).
    struct Listed
    {
        std::string mStage;
        std::string mSet;
        std::filesystem::path mFile;
    };
    std::vector<Listed> allListed()
    {
        std::vector<Listed> result;
        const std::filesystem::path built(OPENEDEN_SHADER_BUILD_DIR);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(built))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".permutations")
                continue;
            std::filesystem::path relative = std::filesystem::relative(entry.path(), built);
            relative.replace_extension();
            const auto manifest = vsgUtil::parsePermutationManifest(readText(entry.path()));
            for (const auto& [set, file] : manifest.mFiles)
                result.push_back({ relative.generic_string(), set, entry.path().parent_path() / file });
        }
        return result;
    }

    std::set<std::string> namesOf(const std::string& canonical)
    {
        std::set<std::string> names;
        std::istringstream in(canonical == "-" ? std::string() : canonical);
        for (std::string name; in >> name;)
            names.insert(name);
        return names;
    }

    SlangRuntimeRequest requestFor(const std::string& stage, const std::set<std::string>& names)
    {
        SlangRuntimeRequest request;
        request.mStageFile = stage;
        request.mDefines = names;
        request.mShaderRoot = OPENEDEN_SHADER_BUILD_DIR;
        request.mInfo = vsgUtil::parseSlangStageInfo(
            readText(std::filesystem::path(OPENEDEN_SHADER_BUILD_DIR) / (stage + ".slang-stage")));
        return request;
    }

    TEST(SlangRuntimeLibrary, every_permutation_the_build_wrote_is_reproduced_byte_for_byte)
    {
        if (!libraryPresent())
            GTEST_SKIP() << "the Slang library is not in " << OPENEDEN_SLANG_LIBRARY_DIR;
        auto runtime = libraryRuntime({});
        const auto listed = allListed();
        ASSERT_GT(listed.size(), 100u);
        std::size_t same = 0;
        for (const Listed& item : listed)
        {
            SCOPED_TRACE(item.mStage + " [" + item.mSet + "]");
            const auto words = runtime->obtain(requestFor(item.mStage, namesOf(item.mSet)), {});
            const auto built = readWords(item.mFile);
            ASSERT_FALSE(words.empty());
            EXPECT_EQ(words, built);
            same += words == built ? 1 : 0;
        }
        EXPECT_EQ(same, listed.size());
    }

    TEST(SlangRuntimeLibrary, a_second_runtime_with_the_cache_reads_every_set_without_the_library)
    {
        if (!libraryPresent())
            GTEST_SKIP() << "the Slang library is not in " << OPENEDEN_SLANG_LIBRARY_DIR;
        const std::filesystem::path cache
            = std::filesystem::temp_directory_path() / "openeden_slangruntime_library_cache";
        std::filesystem::remove_all(cache);
        const auto listed = allListed();
        std::vector<Listed> some;
        for (std::size_t i = 0; i < listed.size(); i += 7)
            some.push_back(listed[i]);
        {
            auto runtime = libraryRuntime(cache);
            for (const Listed& item : some)
                ASSERT_FALSE(runtime->obtain(requestFor(item.mStage, namesOf(item.mSet)), {}).empty());
            EXPECT_EQ(runtime->stats().mCompiled, some.size());
        }
        {
            // Another run of the game: every set is in the cache, so nothing is compiled (the library is not even
            // started: the self-test and the load come with the first compile).
            auto runtime = libraryRuntime(cache);
            for (const Listed& item : some)
                EXPECT_EQ(runtime->obtain(requestFor(item.mStage, namesOf(item.mSet)), {}), readWords(item.mFile));
            EXPECT_EQ(runtime->stats().mCompiled, 0u);
            EXPECT_EQ(runtime->stats().mCacheHits, some.size());
        }
        std::filesystem::remove_all(cache);
    }

    // The check the game makes on a twin made at run time, with the GLSL read as the game reads it.
    std::string gameCheck(
        const std::string& stage, const std::set<std::string>& names, const std::vector<uint32_t>& words)
    {
        std::set<std::string> settings;
        std::set<std::string> requested;
        const auto settingsNames = Resource::settingsDefineNames();
        for (const std::string& name : names)
        {
            if (name.rfind("UVSETS_", 0) == 0)
                continue;
            if (std::find(settingsNames.begin(), settingsNames.end(), name) != settingsNames.end())
                settings.insert(name);
            else
                requested.insert(name);
        }
        auto options = gameOptions(settings);
        auto stageObject = vsg::read_cast<vsg::ShaderStage>(stage, options);
        if (!stageObject || !stageObject->module)
            return "the GLSL stage cannot be read";
        const std::string source = stageObject->module->source;
        const vsgUtil::SlangGlslStage glsl{ source, &requested };
        return vsgUtil::checkTwinAgainstGlsl(stage, names, glsl, words, options);
    }

    TEST(SlangRuntimeLibrary, the_check_accepts_every_listed_permutation)
    {
        if (!libraryPresent())
            GTEST_SKIP() << "the Slang library is not in " << OPENEDEN_SLANG_LIBRARY_DIR;
        std::size_t checked = 0;
        for (const Listed& item : allListed())
        {
            // Sampled: the whole list is compared to the GLSL by SlangPermutations, with its exceptions. A stage with
            // "slang-only" exceptions (the sky's texture coordinate arrays, which the twins take as one set) is
            // refused by this check, which allows none: such a set is compiled at run time only when the build lists
            // it, and an unlisted one keeps the GLSL.
            const std::string exceptions
                = readText(std::filesystem::path(OPENEDEN_SHADER_SOURCE_DIR) / (item.mStage + ".interface-exceptions"));
            if (++checked % 5 != 0 || exceptions.find("\nslang-only:") != std::string::npos
                || exceptions.find("] slang-only:") != std::string::npos)
                continue;
            SCOPED_TRACE(item.mStage + " [" + item.mSet + "]");
            EXPECT_EQ(gameCheck(item.mStage, namesOf(item.mSet), readWords(item.mFile)), "");
        }
    }

    TEST(SlangRuntimeLibrary, the_check_refuses_a_twin_that_declares_what_the_glsl_does_not)
    {
        if (!libraryPresent())
            GTEST_SKIP() << "the Slang library is not in " << OPENEDEN_SLANG_LIBRARY_DIR;
        // The GUI's fragment twin against another stage's GLSL: it declares a texture the sky's moon never has.
        const auto gui = readWords(std::filesystem::path(OPENEDEN_SHADER_BUILD_DIR) / "gui" / "gui.frag.pda39a3ee.spv");
        ASSERT_FALSE(gui.empty());
        EXPECT_NE(gameCheck("sky/sunglare/main.frag", {}, gui), "");
        EXPECT_NE(gameCheck("sky/sunglare/main.vert", {}, gui), "");
        EXPECT_NE(gameCheck("gui/gui.frag", {}, { 1u, 2u, 3u }), "");
    }

    TEST(SlangRuntimeLibrary, sets_the_build_never_listed_compile_and_pass_the_check)
    {
        if (!libraryPresent())
            GTEST_SKIP() << "the Slang library is not in " << OPENEDEN_SLANG_LIBRARY_DIR;
        std::set<std::pair<std::string, std::string>> samples;
        // The terrain's fragment sets the settings can make, as Pipeline::terrainDefines builds them from random
        // choices (26,040 reachable; 39 are listed), plus the settings defines of shadow.settings.
        std::mt19937 random(72);
        const std::vector<std::string> shadow = { "CLOUD_SHADOW", "VOLUME_CLOUD_SHADOW", "DEBUG_SHADOW" };
        const auto listedTerrain = [] {
            std::set<std::string> sets;
            for (const Listed& item : allListed())
            {
                if (item.mStage == "terrain/main.frag")
                    sets.insert(item.mSet);
            }
            return sets;
        }();
        for (int i = 0; samples.size() < 14 && i < 400; ++i)
        {
            Pipeline::WetnessOptions wetness;
            Pipeline::CoverOptions cover;
            const bool pbr = random() % 2 == 0;
            wetness.enabled = pbr && random() % 2 == 0;
            wetness.puddles = wetness.enabled && random() % 2 == 0;
            cover.enabled = pbr && random() % 2 == 0;
            const bool maps = random() % 2 == 0;
            const unsigned outputs = random() % 3 == 0 ? 3u : 0u;
            const unsigned parallax = maps ? random() % 4 : 0u;
            auto names = Pipeline::terrainDefines(maps, outputs, pbr && random() % 2 == 0, wetness, pbr && maps, cover,
                parallax, outputs != 0 && random() % 2 == 0);
            for (const std::string& name : shadow)
            {
                if (random() % 3 == 0)
                    names.insert(name);
            }
            // The fragment stage imports only some of these; the canonical set is what the stage compiles with.
            const std::string canonical = vsgUtil::canonicalDefines(names);
            if (listedTerrain.count(canonical) == 0)
                samples.emplace("terrain/main.frag", canonical);
        }
        // The object shader's content names beyond the traced sets, on lit textured bases with one texture
        // coordinate set.
        const std::vector<std::string> extras
            = { "DARK_MAP", "DETAIL_MAP", "DECAL_MAP", "GLOW_MAP", "LOD_FADE", "ALPHA_TO_COVERAGE", "ADJUST_COVERAGE" };
        for (int i = 0; i < 10; ++i)
        {
            std::set<std::string> names = { "DIFFUSE_MAP", "MATERIAL", "NORMAL", "TEXCOORD", "UVSETS_1" };
            for (const std::string& name : extras)
            {
                if (random() % 3 == 0)
                    names.insert(name);
            }
            samples.emplace("default/main.frag", vsgUtil::canonicalDefines(names));
        }
        for (const char* base : { "NORMAL TEXCOORD UVSETS_1 VERTEX", "COLOR NORMAL TEXCOORD UVSETS_1 VERTEX",
                 "NORMAL TEXCOORD TEXMAT UVSETS_1 VERTEX" })
        {
            // GBUFFER_MOTION comes with SKIN only (the pipeline derives it from the skin's bones); without SKIN the
            // twin declares an input the GLSL does not, which the check refuses (shown by the first run of this test).
            for (const char* extra : { "", " BILLBOARD", " INSTANCE" })
            {
                std::set<std::string> names = namesOf(std::string(base) + extra);
                samples.emplace("default/main.vert", vsgUtil::canonicalDefines(names));
            }
        }
        auto runtime = libraryRuntime({});
        std::size_t accepted = 0;
        std::vector<std::string> refused;
        for (const auto& [stage, canonical] : samples)
        {
            const auto names = namesOf(canonical);
            const auto words = runtime->obtain(requestFor(stage, names),
                SlangRuntimeCheck{
                    "test", [&](const std::vector<uint32_t>& result) { return gameCheck(stage, names, result); } });
            if (words.empty())
                refused.push_back(stage + " [" + canonical + "]");
            else
                ++accepted;
        }
        EXPECT_GT(samples.size(), 30u);
        EXPECT_EQ(accepted, samples.size());
        for (const std::string& line : refused)
            ADD_FAILURE() << "compiled or checked badly: " << line;
    }

    // ---- Review of stage h, round 4 (findings 4, 5, 9 and 12): written before the code they test. ----

    // The names a stage is compiled with, split as the game does: the settings defines come from a settings file, the
    // rest are requested of the pipeline. Owns the GLSL source the SlangGlslStage views.
    struct OwnedGlsl
    {
        vsg::ref_ptr<vsg::Options> mOptions;
        std::string mSource;
        std::set<std::string> mRequested;
        vsgUtil::SlangGlslStage mStage;
    };

    std::unique_ptr<OwnedGlsl> glslFor(const std::string& stage, const std::set<std::string>& names)
    {
        auto owned = std::make_unique<OwnedGlsl>();
        std::set<std::string> settings;
        const auto settingsNames = Resource::settingsDefineNames();
        for (const std::string& name : names)
        {
            if (name.rfind("UVSETS_", 0) == 0)
                continue;
            if (std::find(settingsNames.begin(), settingsNames.end(), name) != settingsNames.end())
                settings.insert(name);
            else
                owned->mRequested.insert(name);
        }
        owned->mOptions = gameOptions(settings);
        auto stageObject = vsg::read_cast<vsg::ShaderStage>(stage, owned->mOptions);
        if (!stageObject || !stageObject->module)
            return nullptr;
        owned->mSource = stageObject->module->source;
        owned->mStage = vsgUtil::SlangGlslStage{ owned->mSource, &owned->mRequested };
        return owned;
    }

    // A set of the object shader's fragment stage that the build does not list (checked), and that compiles.
    const std::set<std::string> unlistedObjectFragment
        = { "DARK_MAP", "DETAIL_MAP", "DIFFUSE_MAP", "GLOW_MAP", "MATERIAL", "NORMAL", "TEXCOORD", "UVSETS_1" };
    // A vertex set the check refuses (GBUFFER_MOTION comes with SKIN only), and one that is fine.
    const std::set<std::string> refusedObjectVertex = { "GBUFFER_MOTION", "NORMAL", "TEXCOORD", "UVSETS_1", "VERTEX" };
    const std::set<std::string> unlistedObjectVertex = { "BILLBOARD", "NORMAL", "TEXCOORD", "UVSETS_1", "VERTEX" };

    struct GameRuntime
    {
        vsg::ref_ptr<vsg::Options> mOptions = gameOptions();
        std::filesystem::path mCache;
        std::vector<std::string> mWarnings;
        std::mutex mMutex;

        explicit GameRuntime(const char* name, bool preload = false)
        {
            mCache = std::filesystem::temp_directory_path() / name;
            std::filesystem::remove_all(mCache);
            vsgUtil::createSlangRuntime(
                mOptions, OPENEDEN_SHADER_BUILD_DIR, OPENEDEN_SLANG_LIBRARY_DIR, mCache,
                [this](bool warning, const std::string& line) {
                    if (!warning)
                        return;
                    std::lock_guard lock(mMutex);
                    mWarnings.push_back(line);
                },
                preload);
        }
        ~GameRuntime()
        {
            mOptions = nullptr; // the runtime goes with the options
            std::error_code ec;
            std::filesystem::remove_all(mCache, ec);
        }
        SlangRuntime* runtime() const { return mOptions->getObject<SlangRuntime>(vsgUtil::slangRuntimeKey); }
    };

    TEST(SlangRuntimeLibrary, the_library_the_build_copied_is_found_and_starts)
    {
        // Review of stage h, finding 4: the other library tests skip when the library is absent, which on Linux would
        // have kept CI green with no proof that anything loads. This one fails.
        const std::filesystem::path dir(OPENEDEN_SLANG_LIBRARY_DIR);
        ASSERT_TRUE(std::filesystem::exists(dir)) << dir;
        auto compiler = vsgUtil::createDynamicSlangCompiler(dir);
        EXPECT_NE(compiler->cacheIdentity(), "") << "the library files are not all in " << dir;
        std::string error;
        EXPECT_TRUE(compiler->prepare(error)) << error;
    }

    TEST(SlangRuntimeLibrary, the_runtime_the_game_makes_has_its_self_tests_and_works_through_findSlangPermutation)
    {
        GameRuntime game("openeden_slangruntime_production");
        ASSERT_NE(game.runtime(), nullptr);
        const auto manifest = vsgUtil::parsePermutationManifest(
            readText(std::filesystem::path(OPENEDEN_SHADER_BUILD_DIR) / "default" / "main.frag.permutations"));
        ASSERT_FALSE(vsgUtil::selectPermutation(manifest, unlistedObjectFragment)) << "the test's set is listed now";
        auto glsl = glslFor("default/main.frag", unlistedObjectFragment);
        ASSERT_NE(glsl, nullptr);
        // Review round 4, finding 12: asked once with no GLSL to check against (a trace), the set stays eligible.
        EXPECT_EQ(vsgUtil::findSlangPermutation("default/main.frag", unlistedObjectFragment, game.mOptions), nullptr);
        auto module
            = vsgUtil::findSlangPermutation("default/main.frag", unlistedObjectFragment, game.mOptions, &glsl->mStage);
        ASSERT_NE(module, nullptr);
        EXPECT_FALSE(module->code.empty());
        EXPECT_EQ(game.runtime()->stats().mCompiled, 1u);
        EXPECT_TRUE(game.mWarnings.empty()) << game.mWarnings.front();
        // The same module while something holds it.
        EXPECT_EQ(
            vsgUtil::findSlangPermutation("default/main.frag", unlistedObjectFragment, game.mOptions, &glsl->mStage),
            module);
        // Once nothing holds it, the next call reads the cache and does not compile.
        const auto words = module->code;
        module = nullptr;
        auto again
            = vsgUtil::findSlangPermutation("default/main.frag", unlistedObjectFragment, game.mOptions, &glsl->mStage);
        ASSERT_NE(again, nullptr);
        EXPECT_EQ(again->code, words);
        EXPECT_EQ(game.runtime()->stats().mCompiled, 1u);
        EXPECT_EQ(game.runtime()->stats().mCacheHits, 1u);
    }

    TEST(SlangRuntimeLibrary, a_refused_set_falls_back_to_the_glsl_for_both_stages_and_is_not_asked_again)
    {
        GameRuntime game("openeden_slangruntime_fallback");
        ASSERT_NE(game.runtime(), nullptr);
        auto vertexGlsl = glslFor("default/main.vert", refusedObjectVertex);
        auto fragmentGlsl = glslFor("default/main.frag", unlistedObjectFragment);
        ASSERT_NE(vertexGlsl, nullptr);
        ASSERT_NE(fragmentGlsl, nullptr);
        const auto vertex = vsgUtil::findSlangPermutation(
            "default/main.vert", refusedObjectVertex, game.mOptions, &vertexGlsl->mStage);
        const auto fragment = vsgUtil::findSlangPermutation(
            "default/main.frag", unlistedObjectFragment, game.mOptions, &fragmentGlsl->mStage);
        EXPECT_EQ(vertex, nullptr) << "GBUFFER_MOTION without SKIN is refused";
        ASSERT_NE(fragment, nullptr);
        ASSERT_EQ(game.mWarnings.size(), 1u);
        EXPECT_NE(game.mWarnings[0].find("default/main.vert"), std::string::npos) << game.mWarnings[0];
        // The pipeline: a GLSL vertex stage and a GLSL fragment stage; the fragment has a twin and the vertex has not.
        vsg::ShaderStages stages = { vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", vertexGlsl->mSource),
            vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", fragmentGlsl->mSource) };
        const auto vertexModule = stages[0]->module;
        const auto fragmentModule = stages[1]->module;
        EXPECT_FALSE(vsgUtil::takeSlangTwinsTogether(stages, { vertex, fragment }));
        EXPECT_EQ(stages[0]->module, vertexModule);
        EXPECT_EQ(stages[1]->module, fragmentModule) << "the fragment stage keeps its GLSL too";
        EXPECT_TRUE(stages[1]->module->code.empty());
        // Asked again, the refused set is answered at once: no second compile, no second warning.
        const auto before = game.runtime()->stats();
        EXPECT_EQ(
            vsgUtil::findSlangPermutation("default/main.vert", refusedObjectVertex, game.mOptions, &vertexGlsl->mStage),
            nullptr);
        EXPECT_EQ(game.runtime()->stats().mFailed, before.mFailed);
        EXPECT_EQ(game.runtime()->stats().mCompiled, before.mCompiled);
        EXPECT_EQ(game.mWarnings.size(), 1u);
    }

    TEST(SlangRuntimeLibrary, sets_the_build_never_listed_pass_through_the_production_check_with_an_exact_count)
    {
        // Review round 4, finding 9: the older test of this name sampled sets and checked them with its own copy of
        // the game's check (and only that more than 30 were tried). This one goes through findSlangPermutation, which
        // is the game's path, over a fixed list, and expects every set to come back.
        GameRuntime game("openeden_slangruntime_exact");
        ASSERT_NE(game.runtime(), nullptr);
        const std::vector<std::pair<std::string, std::set<std::string>>> sets = {
            { "default/main.frag", unlistedObjectFragment },
            { "default/main.frag",
                { "ALPHA_TO_COVERAGE", "DIFFUSE_MAP", "MATERIAL", "NORMAL", "TEXCOORD", "UVSETS_1" } },
            { "default/main.frag", { "DIFFUSE_MAP", "LOD_FADE", "MATERIAL", "NORMAL", "TEXCOORD", "UVSETS_1" } },
            { "default/main.frag", { "ADJUST_COVERAGE", "DIFFUSE_MAP", "MATERIAL", "NORMAL", "TEXCOORD", "UVSETS_1" } },
            { "default/main.vert", unlistedObjectVertex },
            { "default/main.vert", { "INSTANCE", "NORMAL", "TEXCOORD", "UVSETS_1", "VERTEX" } },
            { "default/main.vert", { "COLOR", "NORMAL", "TEXCOORD", "TEXMAT", "UVSETS_1", "VERTEX" } },
        };
        const auto manifests = [&](const std::string& stage) {
            return vsgUtil::parsePermutationManifest(
                readText(std::filesystem::path(OPENEDEN_SHADER_BUILD_DIR) / (stage + ".permutations")));
        };
        std::size_t compiled = 0;
        for (const auto& [stage, names] : sets)
        {
            SCOPED_TRACE(stage + " [" + vsgUtil::canonicalDefines(names) + "]");
            ASSERT_FALSE(vsgUtil::selectPermutation(manifests(stage), names)) << "listed now: not an unlisted set";
            auto glsl = glslFor(stage, names);
            ASSERT_NE(glsl, nullptr);
            const auto module = vsgUtil::findSlangPermutation(stage, names, game.mOptions, &glsl->mStage);
            if (module)
                ++compiled;
        }
        EXPECT_EQ(compiled, sets.size());
        EXPECT_EQ(game.runtime()->stats().mCompiled, sets.size());
        EXPECT_TRUE(game.mWarnings.empty()) << (game.mWarnings.empty() ? "" : game.mWarnings.front());
    }

    TEST(SlangRuntimeLibrary, the_preload_starts_the_library_and_the_first_compile_is_only_a_compile)
    {
        GameRuntime game("openeden_slangruntime_preload", true);
        ASSERT_NE(game.runtime(), nullptr);
        game.runtime()->waitForPreload();
        const auto afterPreload = game.runtime()->stats();
        EXPECT_TRUE(afterPreload.mPreloaded);
        EXPECT_GT(afterPreload.mPreloadMilliseconds, 0.0);
        EXPECT_FALSE(game.runtime()->disabled()) << "both self-tests reproduced the build's bytes";
        EXPECT_EQ(afterPreload.mCompiled, 0u) << "the self-tests are not counted as compiles";
        auto glsl = glslFor("default/main.frag", unlistedObjectFragment);
        ASSERT_NE(glsl, nullptr);
        const auto start = std::chrono::steady_clock::now();
        const auto module
            = vsgUtil::findSlangPermutation("default/main.frag", unlistedObjectFragment, game.mOptions, &glsl->mStage);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        ASSERT_NE(module, nullptr);
        std::cout << "[ info ] first compile after the preload: " << ms << " ms, preload took "
                  << afterPreload.mPreloadMilliseconds << " ms\n";
        EXPECT_LT(game.runtime()->stats().mLongestWaitMilliseconds, 50.0);
    }

    // The child process of the test below: a library folder without glslang, in a process that has never loaded it.
    bool glslangLoaded()
    {
#ifdef _WIN32
        return GetModuleHandleW(L"slang-glslang.dll") != nullptr;
#else
        return readText("/proc/self/maps").find("slang-glslang") != std::string::npos;
#endif
    }

    TEST(SlangIsolated, child_of_the_self_test_without_glslang)
    {
        const char* folder = std::getenv("OPENEDEN_SLANG_ISOLATED_DIR");
        if (folder == nullptr)
            GTEST_SKIP() << "runs as a child of SlangRuntimeLibrary.the_self_test_refuses_a_library_without_glslang";
        ASSERT_FALSE(glslangLoaded()) << "glslang is already loaded in this process: the test would prove nothing";
        // (a) The game's loader refuses the folder at once: the file named for glslang does not load.
        {
            auto compiler = vsgUtil::createDynamicSlangCompiler(folder);
            std::string error;
            EXPECT_FALSE(compiler->prepare(error));
            EXPECT_NE(error.find("glslang"), std::string::npos) << error;
        }
        ASSERT_FALSE(glslangLoaded());
        // (b) Without the preload checks (the old behaviour), the library compiles and the self-test is what stops it.
        // The self-test is the object shader's largest listed fragment set, from the build.
        std::string line;
        auto runtime = SlangRuntime::create(
            vsgUtil::createDynamicSlangCompiler(folder, vsgUtil::SlangLoadOptions{ false }), std::filesystem::path());
        runtime->setLog([&line](bool warning, const std::string& text) {
            if (warning)
                line = text;
        });
        Listed largest;
        std::size_t most = 0;
        for (const Listed& item : allListed())
        {
            if (item.mStage != "default/main.frag")
                continue;
            const std::size_t count = namesOf(item.mSet).size();
            if (count > most)
            {
                most = count;
                largest = item;
            }
        }
        ASSERT_GT(most, 0u);
        runtime->addSelfTest(requestFor(largest.mStage, namesOf(largest.mSet)), readWords(largest.mFile));
        const auto words = runtime->obtain(requestFor("post/omwfx.vert", {}), {});
        EXPECT_TRUE(words.empty());
        EXPECT_TRUE(runtime->disabled());
        std::cout << "[ info ] the self-test said: " << line << "\n";
        // On Windows the library compiles other code (the self-test says it does not reproduce the build's output); on
        // Linux it may fail to compile at all (the self-test says the compiler cannot start). Either way the self-test
        // is what stops it.
        EXPECT_TRUE(
            line.find("does not reproduce") != std::string::npos || line.find("cannot start") != std::string::npos)
            << "the self-test did not catch it: " << line;
    }

    TEST(SlangRuntimeLibrary, the_self_test_refuses_a_library_without_glslang)
    {
        // A copy of the library folder without slang-glslang, tried in a process of its own: a load by bare name
        // returns the module a process already has, so any earlier test that loaded glslang would hide the problem.
        namespace fs = std::filesystem;
        const fs::path copy = fs::temp_directory_path() / "openeden_slangruntime_noglslang";
        fs::remove_all(copy);
        fs::create_directories(copy);
        std::size_t copied = 0;
        for (const auto& entry : fs::directory_iterator(OPENEDEN_SLANG_LIBRARY_DIR))
        {
            const std::string name = entry.path().filename().string();
            if (!entry.is_regular_file() && !entry.is_symlink())
                continue;
            if (name.find("slang-glslang") != std::string::npos)
                continue;
            if (name.find("slang") == std::string::npos || name.find(".bin") != std::string::npos)
                continue;
            fs::copy_file(entry.path(), copy / name, fs::copy_options::overwrite_existing);
            ++copied;
        }
        ASSERT_GE(copied, 2u);
        // A file with glslang's name that is not a library: the folder then has all the files the runtime counts (a
        // folder with one missing is refused earlier, as "the compiler's files are missing"), and the file does not
        // load.
#ifdef _WIN32
        const fs::path bogus = copy / "slang-glslang.dll";
#else
        const fs::path bogus = copy / "libslang-glslang-bogus.so";
#endif
        {
            std::ofstream out(bogus, std::ios::binary);
            out << "not a library";
        }
#ifdef _WIN32
        wchar_t self[4096];
        const DWORD length = GetModuleFileNameW(nullptr, self, 4096);
        ASSERT_GT(length, 0u);
        const fs::path exe(std::wstring(self, length));
        _putenv_s("OPENEDEN_SLANG_ISOLATED_DIR", copy.string().c_str());
#else
        const fs::path exe = fs::read_symlink("/proc/self/exe");
        setenv("OPENEDEN_SLANG_ISOLATED_DIR", copy.c_str(), 1);
#endif
        const fs::path log = fs::temp_directory_path() / "openeden_slangruntime_noglslang.log";
        std::string command = "\"" + exe.string()
            + "\" --gtest_filter=SlangIsolated.child_of_the_self_test_without_glslang > \"" + log.string() + "\" 2>&1";
#ifdef _WIN32
        command = "\"" + command + "\"";
#endif
        const int status = std::system(command.c_str());
#ifdef _WIN32
        _putenv_s("OPENEDEN_SLANG_ISOLATED_DIR", "");
#else
        unsetenv("OPENEDEN_SLANG_ISOLATED_DIR");
#endif
        const std::string output = readText(log);
        EXPECT_EQ(status, 0) << output;
        EXPECT_NE(output.find("[  PASSED  ] 1 test"), std::string::npos) << output;
        fs::remove_all(copy);
        fs::remove(log);
    }
#endif
}
