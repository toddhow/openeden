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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
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
#endif
}
