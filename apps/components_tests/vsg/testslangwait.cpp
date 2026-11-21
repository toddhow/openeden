// Task 0363, stage f: a shader set that is neither prebuilt nor cached is WAITED for (SlangRuntime::obtainOrWait),
// with the window kept alive on the main thread, a label for the loading screen, a bound, and a clear answer when the
// compile fails. Tested with a fake compiler; the thread that makes the runtime is the "main" thread, as in the game.
#include <gtest/gtest.h>

#include <components/vsgutil/slangruntime.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using vsgUtil::SlangAvailability;
    using vsgUtil::SlangRuntime;
    using vsgUtil::SlangRuntimeCheck;
    using vsgUtil::SlangRuntimeRequest;
    using vsgUtil::SlangWaitSettings;

    std::vector<uint32_t> words(uint32_t marker)
    {
        return { 0x07230203u, 0x00010000u, 0u, 8u, 0u, marker };
    }

    class WaitCompiler final : public vsgUtil::SlangRuntimeCompiler
    {
    public:
        std::atomic<int> mCompiles{ 0 };
        std::vector<uint32_t> mOutput = words(7);
        std::string mError;
        bool mPrepareOk = true;
        std::shared_future<void> mGate; // when valid, a compile waits for it

        std::string cacheIdentity() override { return "wait-compiler-1"; }
        bool prepare(std::string& error) override
        {
            if (!mPrepareOk)
                error = "cannot load the Slang library";
            return mPrepareOk;
        }
        std::vector<uint32_t> compile(const SlangRuntimeRequest&, std::string& error) override
        {
            ++mCompiles;
            if (mGate.valid())
                mGate.wait();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            error = mError;
            return mOutput;
        }
    };

    double sinceMs(std::chrono::steady_clock::time_point from)
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count();
    }

    struct Observed
    {
        std::mutex mMutex;
        std::vector<bool> mCalls;
        std::atomic<int> mPumps{ 0 };
        SlangWaitSettings settings(std::chrono::milliseconds other, std::chrono::milliseconds main)
        {
            SlangWaitSettings result;
            result.mOtherThreads = other;
            result.mMainThread = main;
            result.mPump = [this] { ++mPumps; };
            result.mObserver = [this](bool begin) {
                std::lock_guard lock(mMutex);
                mCalls.push_back(begin);
            };
            return result;
        }
        std::vector<bool> calls()
        {
            std::lock_guard lock(mMutex);
            return mCalls;
        }
    };

    class Fixture
    {
    public:
        Fixture()
        {
            static std::atomic<unsigned> counter{ 0 };
            mRoot = std::filesystem::temp_directory_path()
                / ("openeden_slangwait_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                    + "_" + std::to_string(counter++));
            std::filesystem::create_directories(mRoot / "shaders");
            std::ofstream(mRoot / "shaders" / "a.slang") << "// a\n";
            mRequest.mStageFile = "default/main.frag";
            mRequest.mInfo = { "a.slang", "fragMain", "fragment", {} };
            mRequest.mDefines = { "DIFFUSE_MAP" };
            mRequest.mShaderRoot = mRoot / "shaders";
        }
        ~Fixture()
        {
            std::error_code ec;
            std::filesystem::remove_all(mRoot, ec);
        }
        vsg::ref_ptr<SlangRuntime> runtime(WaitCompiler*& compiler)
        {
            auto fake = std::make_unique<WaitCompiler>();
            compiler = fake.get();
            auto result = SlangRuntime::create(std::move(fake), mRoot / "cache");
            result->setLog([this](bool warning, const std::string& line) {
                if (!warning)
                    return;
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

    TEST(SlangWait, a_set_the_worker_has_to_compile_is_waited_for_and_comes_back_ready)
    {
        Fixture f;
        WaitCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        Observed observed;
        runtime->setWaitSettings(observed.settings(std::chrono::seconds(10), std::chrono::seconds(10)));
        const auto answer = runtime->obtainOrWait(f.mRequest, f.mCheck);
        EXPECT_EQ(answer.mState, SlangAvailability::Ready);
        EXPECT_EQ(answer.mWords, words(7));
        EXPECT_EQ(compiler->mCompiles, 1);
        EXPECT_EQ(observed.calls(), (std::vector<bool>{ true, false })) << "one begin and one end";
        const auto stats = runtime->stats();
        EXPECT_EQ(stats.mWaits, 1u);
        EXPECT_EQ(stats.mMainThreadWaits, 1u);
        EXPECT_EQ(stats.mWaitTimeouts, 0u);
        EXPECT_GT(stats.mWaitedMilliseconds, 0.0);
        EXPECT_EQ(stats.mCompiledOnMain, 0u) << "the compile is the worker's, never the caller's";
    }

    TEST(SlangWait, the_main_thread_keeps_pumping_its_window_while_it_waits_and_other_threads_do_not)
    {
        Fixture f;
        WaitCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        std::promise<void> release;
        compiler->mGate = release.get_future().share();
        Observed observed;
        runtime->setWaitSettings(observed.settings(std::chrono::seconds(10), std::chrono::seconds(10)));
        std::thread opener([&release] {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            release.set_value();
        });
        const auto answer = runtime->obtainOrWait(f.mRequest, f.mCheck);
        opener.join();
        EXPECT_EQ(answer.mState, SlangAvailability::Ready);
        EXPECT_GE(observed.mPumps.load(), 4) << "about every 25 ms over 200 ms";

        // another thread, another set: waits too, never pumps
        const int pumpsBefore = observed.mPumps.load();
        auto other = f.mRequest;
        other.mDefines = { "NORMAL_MAP" };
        SlangAvailability state = SlangAvailability::Unavailable;
        std::thread asker([&] { state = runtime->obtainOrWait(other, f.mCheck).mState; });
        asker.join();
        EXPECT_EQ(state, SlangAvailability::Ready);
        EXPECT_EQ(observed.mPumps.load(), pumpsBefore);
        EXPECT_EQ(runtime->stats().mMainThreadWaits, 1u);
        EXPECT_EQ(runtime->stats().mWaits, 2u);
    }

    TEST(SlangWait, a_cache_hit_does_not_wait_and_tells_nobody)
    {
        Fixture f;
        WaitCompiler* compiler = nullptr;
        {
            auto first = f.runtime(compiler);
            first->obtain(f.mRequest, f.mCheck);
        }
        auto runtime = f.runtime(compiler);
        Observed observed;
        runtime->setWaitSettings(observed.settings(std::chrono::seconds(10), std::chrono::seconds(10)));
        const auto answer = runtime->obtainOrWait(f.mRequest, f.mCheck);
        EXPECT_EQ(answer.mState, SlangAvailability::Ready);
        EXPECT_EQ(compiler->mCompiles, 0);
        EXPECT_TRUE(observed.calls().empty());
        EXPECT_EQ(runtime->stats().mWaits, 0u);
    }

    TEST(SlangWait, a_compile_that_fails_ends_the_wait_unavailable_with_a_warning_and_is_not_asked_again)
    {
        Fixture f;
        WaitCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        compiler->mOutput.clear();
        compiler->mError = "error: no such entry point";
        Observed observed;
        runtime->setWaitSettings(observed.settings(std::chrono::seconds(10), std::chrono::seconds(10)));
        const auto answer = runtime->obtainOrWait(f.mRequest, f.mCheck);
        EXPECT_EQ(answer.mState, SlangAvailability::Unavailable);
        EXPECT_TRUE(answer.mWords.empty());
        EXPECT_EQ(observed.calls(), (std::vector<bool>{ true, false }));
        EXPECT_EQ(f.log().size(), 1u) << "the failure is logged once, with its reason";
        const int compiles = compiler->mCompiles;
        EXPECT_EQ(runtime->obtainOrWait(f.mRequest, f.mCheck).mState, SlangAvailability::Unavailable);
        EXPECT_EQ(compiler->mCompiles, compiles) << "a failed set is remembered";
        EXPECT_EQ(observed.calls().size(), 2u) << "and the second ask does not wait";
    }

    TEST(SlangWait, a_compiler_that_cannot_start_disables_the_runtime_and_the_waiter_gets_unavailable)
    {
        Fixture f;
        WaitCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        compiler->mPrepareOk = false;
        Observed observed;
        runtime->setWaitSettings(observed.settings(std::chrono::seconds(10), std::chrono::seconds(10)));
        EXPECT_EQ(runtime->obtainOrWait(f.mRequest, f.mCheck).mState, SlangAvailability::Unavailable);
        EXPECT_TRUE(runtime->disabled());
        EXPECT_EQ(observed.calls(), (std::vector<bool>{ true, false }));
        EXPECT_FALSE(f.log().empty());
    }

    TEST(SlangWait, a_wait_that_runs_out_answers_pending_once_warns_and_is_not_repeated)
    {
        Fixture f;
        WaitCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        std::promise<void> release;
        compiler->mGate = release.get_future().share();
        Observed observed;
        runtime->setWaitSettings(observed.settings(std::chrono::milliseconds(80), std::chrono::milliseconds(80)));
        const auto begin = std::chrono::steady_clock::now();
        const auto first = runtime->obtainOrWait(f.mRequest, f.mCheck);
        const double firstMs = sinceMs(begin);
        EXPECT_EQ(first.mState, SlangAvailability::Pending);
        EXPECT_GE(firstMs, 70.0);
        EXPECT_LT(firstMs, 2000.0);
        EXPECT_EQ(observed.calls(), (std::vector<bool>{ true, false })) << "an end follows a time-out";
        ASSERT_EQ(f.log().size(), 1u);
        EXPECT_NE(f.log()[0].find("still not compiled"), std::string::npos) << f.log()[0];

        const auto again = std::chrono::steady_clock::now();
        const auto second = runtime->obtainOrWait(f.mRequest, f.mCheck);
        EXPECT_EQ(second.mState, SlangAvailability::Pending);
        EXPECT_LT(sinceMs(again), 60.0) << "it ran into the limit once: no second wait";
        EXPECT_EQ(observed.calls().size(), 2u);
        EXPECT_EQ(f.log().size(), 1u) << "one warning";
        EXPECT_EQ(runtime->stats().mWaitTimeouts, 1u);

        release.set_value();
        runtime->waitForIdle();
        EXPECT_EQ(runtime->obtainOrWait(f.mRequest, f.mCheck).mState, SlangAvailability::Ready)
            << "the queued compile still finishes and is used afterwards";
    }

    TEST(SlangWait, a_limit_of_zero_means_no_wait_at_all)
    {
        Fixture f;
        WaitCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        Observed observed;
        runtime->setWaitSettings(observed.settings(std::chrono::milliseconds(0), std::chrono::milliseconds(0)));
        EXPECT_EQ(runtime->obtainOrWait(f.mRequest, f.mCheck).mState, SlangAvailability::Pending);
        EXPECT_TRUE(observed.calls().empty());
        runtime->waitForIdle();
        EXPECT_EQ(runtime->stats().mWaits, 0u);
    }

    TEST(SlangWait, the_main_thread_has_its_own_limit_and_other_threads_theirs)
    {
        Fixture f;
        WaitCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        std::promise<void> release;
        compiler->mGate = release.get_future().share();
        Observed observed;
        // a long limit for loaders, a short one for the main thread
        runtime->setWaitSettings(observed.settings(std::chrono::seconds(10), std::chrono::milliseconds(60)));
        EXPECT_EQ(runtime->obtainOrWait(f.mRequest, f.mCheck).mState, SlangAvailability::Pending) << "main: 60 ms";
        auto other = f.mRequest;
        other.mDefines = { "NORMAL_MAP" };
        SlangAvailability state = SlangAvailability::Unavailable;
        std::thread asker([&] { state = runtime->obtainOrWait(other, f.mCheck).mState; });
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        release.set_value(); // the loader had waited longer than the main thread's limit and is let through
        asker.join();
        EXPECT_EQ(state, SlangAvailability::Ready);
    }

    TEST(SlangWait, the_main_thread_limit_can_be_asked_for_each_wait_and_other_threads_ignore_it)
    {
        Fixture f;
        WaitCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        std::promise<void> release;
        compiler->mGate = release.get_future().share();
        Observed observed;
        auto settings = observed.settings(std::chrono::seconds(10), std::chrono::seconds(10));
        std::atomic<int> asked{ 0 };
        settings.mMainThreadLimit = [&asked] {
            ++asked;
            return std::chrono::milliseconds(50); // in play: short, whatever mMainThread says
        };
        runtime->setWaitSettings(settings);
        const auto begin = std::chrono::steady_clock::now();
        EXPECT_EQ(runtime->obtainOrWait(f.mRequest, f.mCheck).mState, SlangAvailability::Pending);
        EXPECT_LT(sinceMs(begin), 1000.0);
        EXPECT_EQ(asked, 1);
        auto other = f.mRequest;
        other.mDefines = { "NORMAL_MAP" };
        SlangAvailability state = SlangAvailability::Unavailable;
        std::thread asker([&] { state = runtime->obtainOrWait(other, f.mCheck).mState; });
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        release.set_value();
        asker.join();
        EXPECT_EQ(state, SlangAvailability::Ready);
        EXPECT_EQ(asked, 1) << "a loader thread never asks for the main thread's limit";
    }

    TEST(SlangWait, an_observer_or_pump_that_throws_does_not_break_the_wait)
    {
        Fixture f;
        WaitCompiler* compiler = nullptr;
        auto runtime = f.runtime(compiler);
        SlangWaitSettings settings;
        settings.mPump = [] { throw std::runtime_error("pump"); };
        settings.mObserver = [](bool) { throw std::runtime_error("observer"); };
        runtime->setWaitSettings(settings);
        EXPECT_EQ(runtime->obtainOrWait(f.mRequest, f.mCheck).mState, SlangAvailability::Ready);
    }
}
