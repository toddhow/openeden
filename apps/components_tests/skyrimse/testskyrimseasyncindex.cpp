// Task 0109, decision 2 (neutral world model stage N3a-3b): the Skyrim SE world view builds its index on a worker
// thread. Written from the spec: start-up returns at once, a query that comes before the index is done waits for it
// (bounded) and then sees the same answers as a synchronous load, a timeout answers "nothing" and never reads a
// half-built index, cancelling and destroying a view with a running worker is clean. A FileSource that blocks on a
// gate keeps the worker busy at a known point (the string table read of a localised plugin), so the tests do not
// depend on timing. Generated plugins only; no game data.
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    namespace fs = std::filesystem;
    using namespace std::chrono_literals;

    constexpr std::uint32_t kGrupTop = 0, kGrupInteriorBlock = 2, kGrupInteriorSub = 3, kGrupCellChildren = 6,
                            kGrupTemporary = 9;

    struct TempDir
    {
        fs::path mPath;
        TempDir()
        {
            static int counter = 0;
            mPath = fs::temp_directory_path()
                / ("oe_skyrim_asyncindex_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                    + "_" + std::to_string(counter++));
            fs::create_directories(mPath);
        }
        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(mPath, ec);
        }
        fs::path write(const std::string& name, const Bytes& bytes) const
        {
            const fs::path p = mPath / name;
            std::ofstream out(p, std::ios::binary);
            const std::string s = bytes.str();
            out.write(s.data(), static_cast<std::streamsize>(s.size()));
            return p;
        }
    };

    Bytes interiorCell(std::uint32_t id, const std::string& editorId, bool localisedName, std::uint32_t nameId)
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring(editorId)));
        if (localisedName)
            subs.append(sub4("FULL", Bytes().u32(nameId)));
        else
            subs.append(sub4("FULL", Bytes().zstring("Plain " + editorId)));
        subs.append(sub4("DATA", Bytes().u16(0x0001)));
        Bytes sub = record4("CELL", 0, id, subs);
        sub.append(group4(id, kGrupCellChildren, group4(id, kGrupTemporary, Bytes())));
        return group4(Content::fourCC("CELL"), kGrupTop,
            group4(id % 10, kGrupInteriorBlock, group4((id / 10) % 10, kGrupInteriorSub, sub)));
    }

    Bytes skyrimPlugin(std::uint32_t headerFlags, const Bytes& body)
    {
        Bytes bytes = tes4Header(1.7f, headerFlags, "a", "b", 1, {}, 24, 44);
        bytes.append(body);
        return bytes;
    }

    Bytes stringsTable(std::uint32_t id, const std::string& text)
    {
        Bytes data;
        data.zstring(text);
        Bytes all;
        all.u32(1).u32(static_cast<std::uint32_t>(data.size()));
        all.u32(id).u32(0);
        all.append(data);
        return all;
    }

    // A file source whose read() blocks until the test opens the gate: the worker stops in the string table read of the
    // first localised plugin, which is a known point of the index build.
    class GatedFiles final : public FileSource
    {
    public:
        bool read(std::string_view path, std::string& bytes) const override
        {
            {
                std::unique_lock lock(mMutex);
                mReached = true;
                mCv.notify_all();
                mCv.wait(lock, [this] { return mOpen; });
            }
            (void)path;
            bytes = mTable;
            return true;
        }
        void open()
        {
            const std::lock_guard lock(mMutex);
            mOpen = true;
            mCv.notify_all();
        }
        bool waitReached(std::chrono::milliseconds limit) const
        {
            std::unique_lock lock(mMutex);
            return mCv.wait_for(lock, limit, [this] { return mReached; });
        }
        std::string mTable;

    private:
        mutable std::mutex mMutex;
        mutable std::condition_variable mCv;
        mutable bool mReached = false;
        bool mOpen = false;
    };

    // Two plain plugins and, when `gated`, a localised one in the middle that stops the worker at the gate.
    struct Plugins
    {
        TempDir dir;
        std::vector<fs::path> paths;
        explicit Plugins(bool gated)
        {
            paths.push_back(
                dir.write("a.esm", skyrimPlugin(Content::sRecMaster, interiorCell(0x900, "HallA", false, 0))));
            if (gated)
                paths.push_back(dir.write("loc.esm",
                    skyrimPlugin(
                        Content::sRecMaster | Content::sRecLocalized, interiorCell(0x910, "HallLoc", true, 5))));
            paths.push_back(
                dir.write("c.esm", skyrimPlugin(Content::sRecMaster, interiorCell(0x920, "HallC", false, 0))));
        }
    };

    bool eventually(const std::function<bool()>& condition, std::chrono::milliseconds limit = 5000ms)
    {
        const auto end = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < end)
        {
            if (condition())
                return true;
            std::this_thread::sleep_for(2ms);
        }
        return condition();
    }

    TEST(SkyrimSeAsyncIndex, startLoadReturnsWhileTheWorkerIsStillBuilding)
    {
        Plugins plugins(true);
        GatedFiles files;
        files.mTable = stringsTable(5, "Localised").str();
        SkyrimWorldView view;
        ASSERT_TRUE(view.configure(SkyrimWorldView::Options{}, &files));
        ASSERT_TRUE(view.startLoad(plugins.paths, nullptr));
        ASSERT_TRUE(files.waitReached(5000ms)) << "the worker should have reached the gate";
        EXPECT_FALSE(view.isReady()) << "start-up must not wait for the index";
        files.open();
        view.cancelAndJoin();
        EXPECT_TRUE(view.isReady());
    }

    TEST(SkyrimSeAsyncIndex, aQueryBeforeTheIndexIsDoneWaitsAndThenSeesTheWholeIndex)
    {
        Plugins plugins(true);
        GatedFiles files;
        files.mTable = stringsTable(5, "Localised").str();
        SkyrimWorldView view;
        ASSERT_TRUE(view.configure(SkyrimWorldView::Options{}, &files));
        ASSERT_TRUE(view.startLoad(plugins.paths, nullptr));
        ASSERT_TRUE(files.waitReached(5000ms));

        std::atomic<bool> answered{ false };
        std::promise<std::size_t> cells;
        std::thread asker([&] {
            const auto found = view.findCellByEditorId("HallC");
            answered.store(true);
            cells.set_value(found.has_value() ? view.cellCount() : 0);
        });
        EXPECT_FALSE(eventually([&] { return answered.load(); }, 150ms)) << "the query must wait for the worker";
        files.open();
        asker.join();
        EXPECT_EQ(cells.get_future().get(), 3u)
            << "after the wait the whole index is there, the last plugin's cell too";
        EXPECT_EQ(view.indexWaits(), 1u);
        EXPECT_TRUE(view.isReady());
    }

    TEST(SkyrimSeAsyncIndex, everyQueryKindWaitsRatherThanReadingAHalfBuiltIndex)
    {
        Plugins plugins(true);
        GatedFiles files;
        files.mTable = stringsTable(5, "Localised").str();
        SkyrimWorldView view;
        ASSERT_TRUE(view.configure(SkyrimWorldView::Options{}, &files));
        ASSERT_TRUE(view.startLoad(plugins.paths, nullptr));
        ASSERT_TRUE(files.waitReached(5000ms));
        std::atomic<int> done{ 0 };
        std::vector<std::thread> askers;
        const auto ask = [&](const std::function<void()>& query) {
            askers.emplace_back([&done, query] {
                query();
                ++done;
            });
        };
        ask([&] { (void)view.cellCount(); });
        ask([&] { (void)view.worldCount(); });
        ask([&] {
            std::vector<openeden::sdk::game::WorldCellInfo> out;
            (void)view.listCells(0, 10, out);
        });
        ask([&] {
            std::vector<openeden::sdk::game::WorldInfo> out;
            (void)view.listWorlds(out);
        });
        ask([&] { (void)view.findCellByGrid(0, 0, 0); });
        ask([&] { (void)view.findWorldByEditorId("x"); });
        ask([&] { (void)view.stats(); });
        ask([&] { (void)view.errors(); });
        ask([&] { (void)view.teleportDoorCells(); });
        EXPECT_FALSE(eventually([&] { return done.load() > 0; }, 150ms))
            << "no query may return before the worker is done";
        files.open();
        for (std::thread& t : askers)
            t.join();
        EXPECT_EQ(done.load(), 9);
    }

    TEST(SkyrimSeAsyncIndex, aTimeoutAnswersNothingAndTellsOnceThenTheIndexIsUsable)
    {
        Plugins plugins(true);
        GatedFiles files;
        files.mTable = stringsTable(5, "Localised").str();
        std::mutex noticeMutex;
        std::vector<std::string> notices;
        SkyrimWorldView::Options options;
        options.mIndexWait = 30ms;
        options.mNotice = [&](const std::string& text) {
            const std::lock_guard lock(noticeMutex);
            notices.push_back(text);
        };
        SkyrimWorldView view;
        ASSERT_TRUE(view.configure(std::move(options), &files));
        ASSERT_TRUE(view.startLoad(plugins.paths, nullptr));
        ASSERT_TRUE(files.waitReached(5000ms));

        EXPECT_FALSE(view.findCellByEditorId("HallA").has_value());
        EXPECT_EQ(view.cellCount(), 0u);
        openeden::sdk::game::WorldCellContent content;
        std::string error;
        EXPECT_FALSE(view.loadCell(0x900, content, error));
        EXPECT_FALSE(error.empty());
        EXPECT_EQ(view.stats().mCells, 0u);
        EXPECT_EQ(view.find(0x900, 1), nullptr);
        {
            const std::lock_guard lock(noticeMutex);
            ASSERT_EQ(notices.size(), 1u) << "the timeout is said once, not once per query";
            EXPECT_NE(notices[0].find("not ready"), std::string::npos);
        }

        files.open();
        ASSERT_TRUE(eventually([&] { return view.isReady(); }));
        EXPECT_EQ(view.cellCount(), 3u);
        EXPECT_TRUE(view.findCellByEditorId("HallLoc").has_value());
        EXPECT_TRUE(view.loadCell(view.findCellByEditorId("HallA")->id, content, error)) << error;
    }

    TEST(SkyrimSeAsyncIndex, theAsynchronousIndexEqualsTheSynchronousOne)
    {
        Plugins plugins(false);
        SkyrimWorldView sync;
        std::vector<std::string> problems;
        ASSERT_EQ(sync.load(plugins.paths, problems), 2u);

        SkyrimWorldView async;
        std::promise<SkyrimWorldView::LoadReport> reported;
        ASSERT_TRUE(async.startLoad(
            plugins.paths, [&](const SkyrimWorldView::LoadReport& report) { reported.set_value(report); }));
        const SkyrimWorldView::LoadReport report = reported.get_future().get();
        EXPECT_EQ(report.mGiven, 2u);
        EXPECT_EQ(report.mTaken, 2u);
        EXPECT_FALSE(report.mAborted);
        EXPECT_TRUE(report.mProblems.empty());

        ASSERT_EQ(async.cellCount(), sync.cellCount());
        std::vector<openeden::sdk::game::WorldCellInfo> a, b;
        async.listCells(0, 100, a);
        sync.listCells(0, 100, b);
        ASSERT_EQ(a.size(), b.size());
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            EXPECT_EQ(a[i].id, b[i].id);
            EXPECT_EQ(a[i].editorId, b[i].editorId);
            EXPECT_EQ(a[i].name, b[i].name);
        }
        EXPECT_EQ(async.stats().mCells, sync.stats().mCells);
        EXPECT_EQ(async.stats().mBaseForms, sync.stats().mBaseForms);
    }

    TEST(SkyrimSeAsyncIndex, theCompletionFunctionRunsOnTheWorkerWhereTheViewIsUsableAndOtherThreadsStillWait)
    {
        Plugins plugins(false);
        SkyrimWorldView view;
        std::mutex mutex;
        std::condition_variable cv;
        bool insideCallback = false;
        bool releaseCallback = false;
        std::thread::id callbackThread;
        std::size_t cellsSeenByCallback = 0;
        ASSERT_TRUE(view.startLoad(plugins.paths, [&](const SkyrimWorldView::LoadReport&) {
            callbackThread = std::this_thread::get_id();
            cellsSeenByCallback = view.cellCount(); // the worker may use the view: this must not deadlock
            std::unique_lock lock(mutex);
            insideCallback = true;
            cv.notify_all();
            cv.wait(lock, [&] { return releaseCallback; });
        }));
        {
            std::unique_lock lock(mutex);
            ASSERT_TRUE(cv.wait_for(lock, 5000ms, [&] { return insideCallback; }));
        }
        EXPECT_NE(callbackThread, std::this_thread::get_id());
        EXPECT_EQ(cellsSeenByCallback, 2u);
        EXPECT_FALSE(view.isReady()) << "the index is published only after the completion function returns";
        {
            const std::lock_guard lock(mutex);
            releaseCallback = true;
        }
        cv.notify_all();
        EXPECT_EQ(view.cellCount(), 2u);
    }

    TEST(SkyrimSeAsyncIndex, cancelStopsAfterThePluginTheWorkerIsOnAndReportsAborted)
    {
        Plugins plugins(true);
        GatedFiles files;
        files.mTable = stringsTable(5, "Localised").str();
        SkyrimWorldView view;
        ASSERT_TRUE(view.configure(SkyrimWorldView::Options{}, &files));
        std::promise<SkyrimWorldView::LoadReport> reported;
        ASSERT_TRUE(view.startLoad(
            plugins.paths, [&](const SkyrimWorldView::LoadReport& report) { reported.set_value(report); }));
        ASSERT_TRUE(files.waitReached(5000ms));
        // The worker is inside plugin 2 of 3 (a.esm is done). Cancel, then let it go on.
        std::thread canceller([&] { view.cancelAndJoin(); });
        std::this_thread::sleep_for(30ms);
        files.open();
        canceller.join();
        const SkyrimWorldView::LoadReport report = reported.get_future().get();
        EXPECT_TRUE(report.mAborted);
        EXPECT_EQ(report.mTaken, 2u) << "the plugin the worker was on is finished, the next one is not started";
        EXPECT_TRUE(view.isReady());
        EXPECT_EQ(view.cellCount(), 0u) << "an aborted load is never read";
        EXPECT_FALSE(view.findCellByEditorId("HallA").has_value());
        EXPECT_NO_THROW(view.cancelAndJoin()) << "a second call is harmless";
    }

    TEST(SkyrimSeAsyncIndex, aBuildThatThrowsLeavesAViewThatAnswersNothingAndTellsOnce)
    {
        Plugins plugins(false);
        std::mutex noticeMutex;
        std::vector<std::string> notices;
        SkyrimWorldView::Options options;
        options.mNotice = [&](const std::string& text) {
            const std::lock_guard lock(noticeMutex);
            notices.push_back(text);
        };
        SkyrimWorldView view;
        ASSERT_TRUE(view.configure(std::move(options), nullptr));
        ASSERT_TRUE(view.startLoad(
            plugins.paths, [](const SkyrimWorldView::LoadReport&) { throw std::runtime_error("boom"); }));
        ASSERT_TRUE(eventually([&] { return view.isReady(); }));
        EXPECT_EQ(view.cellCount(), 0u) << "a failed build is never read, though both plugins were indexed";
        EXPECT_FALSE(view.findCellByEditorId("HallA").has_value());
        const std::lock_guard lock(noticeMutex);
        ASSERT_FALSE(notices.empty());
        EXPECT_NE(notices[0].find("boom"), std::string::npos);
    }

    TEST(SkyrimSeAsyncIndex, destroyingAViewWithARunningWorkerJoinsIt)
    {
        Plugins plugins(true);
        GatedFiles files;
        files.mTable = stringsTable(5, "Localised").str();
        std::thread opener;
        {
            SkyrimWorldView view;
            ASSERT_TRUE(view.configure(SkyrimWorldView::Options{}, &files));
            ASSERT_TRUE(view.startLoad(plugins.paths, nullptr));
            ASSERT_TRUE(files.waitReached(5000ms));
            opener = std::thread([&] {
                std::this_thread::sleep_for(30ms);
                files.open();
            });
        } // the destructor must wait for the worker, which is released by `opener` meanwhile
        opener.join();
        SUCCEED();
    }

    TEST(SkyrimSeAsyncIndex, startLoadIsRefusedWhileRunningAfterAPluginAndConfigureWhileRunning)
    {
        Plugins plugins(true);
        GatedFiles files;
        files.mTable = stringsTable(5, "Localised").str();
        SkyrimWorldView view;
        ASSERT_TRUE(view.configure(SkyrimWorldView::Options{}, &files));
        ASSERT_TRUE(view.startLoad(plugins.paths, nullptr));
        ASSERT_TRUE(files.waitReached(5000ms));
        EXPECT_FALSE(view.startLoad(plugins.paths, nullptr));
        EXPECT_FALSE(view.configure(SkyrimWorldView::Options{}, &files));
        files.open();
        ASSERT_TRUE(eventually([&] { return view.isReady(); }));
        EXPECT_FALSE(view.startLoad(plugins.paths, nullptr)) << "a view that holds plugins builds no second index";
    }

    TEST(SkyrimSeAsyncIndex, manyThreadsAskingWhileTheWorkerBuildsAllGetTheSameAnswer)
    {
        Plugins plugins(true);
        GatedFiles files;
        files.mTable = stringsTable(5, "Localised").str();
        SkyrimWorldView view;
        ASSERT_TRUE(view.configure(SkyrimWorldView::Options{}, &files));
        ASSERT_TRUE(view.startLoad(plugins.paths, nullptr));
        ASSERT_TRUE(files.waitReached(5000ms));
        std::atomic<int> wrong{ 0 };
        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i)
            threads.emplace_back([&] {
                if (view.cellCount() != 3u)
                    ++wrong;
                if (!view.findCellByEditorId("HallLoc").has_value())
                    ++wrong;
            });
        std::this_thread::sleep_for(30ms);
        files.open();
        for (std::thread& t : threads)
            t.join();
        EXPECT_EQ(wrong.load(), 0);
    }

    // ---- task 0125: the terrain index is built by the worker, not by the first exterior query ----------------------

    TEST(SkyrimSeAsyncIndex, theWorkerBuildsTheTerrainIndexAfterTheCellIndex)
    {
        Plugins plugins(false);
        SkyrimWorldView view;
        ASSERT_TRUE(view.configure(SkyrimWorldView::Options{}, nullptr));
        ASSERT_TRUE(view.startLoad(plugins.paths, nullptr));
        EXPECT_TRUE(eventually([&] { return view.isReady(); }));
        EXPECT_TRUE(eventually([&] { return view.terrainBuiltOnWorker(); })) << "after the cell index, by the worker";
        view.cancelAndJoin();
        EXPECT_NE(view.terrainIndex(), nullptr);
    }

    TEST(SkyrimSeAsyncIndex, aQueryBeforeTheTerrainIndexIsDoneWaitsForItAndGetsTheSameIndex)
    {
        Plugins plugins(false);
        SkyrimWorldView view;
        ASSERT_TRUE(view.configure(SkyrimWorldView::Options{}, nullptr));
        ASSERT_TRUE(view.startLoad(plugins.paths, nullptr));
        ASSERT_TRUE(eventually([&] { return view.isReady(); }));
        const SkyrimTerrainIndex* first = view.terrainIndex(); // may come while the worker is still on the terrain
        ASSERT_NE(first, nullptr);
        view.cancelAndJoin();
        EXPECT_EQ(view.terrainIndex(), first) << "one index, whoever built it";
        EXPECT_TRUE(view.terrainBuiltOnWorker());
    }

    TEST(SkyrimSeAsyncIndex, aViewLoadedWithoutAWorkerBuildsTheTerrainIndexOnDemandAsBefore)
    {
        Plugins plugins(false);
        SkyrimWorldView view;
        std::string error;
        ASSERT_TRUE(view.addPlugin(plugins.paths[0], error)) << error;
        view.finish();
        EXPECT_FALSE(view.terrainBuiltOnWorker());
        EXPECT_NE(view.terrainIndex(), nullptr);
        EXPECT_FALSE(view.terrainBuiltOnWorker());
    }

    TEST(SkyrimSeAsyncIndex, aCancelledBuildBuildsNoTerrainIndex)
    {
        Plugins plugins(true);
        GatedFiles files;
        files.mTable = stringsTable(5, "Localised").str();
        SkyrimWorldView view;
        ASSERT_TRUE(view.configure(SkyrimWorldView::Options{}, &files));
        ASSERT_TRUE(view.startLoad(plugins.paths, nullptr));
        ASSERT_TRUE(files.waitReached(5000ms));
        std::thread canceller([&] { view.cancelAndJoin(); });
        std::this_thread::sleep_for(50ms);
        files.open();
        canceller.join();
        EXPECT_FALSE(view.terrainBuiltOnWorker());
    }
}
