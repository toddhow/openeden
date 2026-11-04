#include <gtest/gtest.h>

#include <components/render/screenshot.hpp>

#include <vsg/io/Options.h>
#include <vsg/state/Image.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>
#include <vector>

// Screenshot names (Render::Screenshot::generateFilename): a name is handed out when the screenshot is asked for and
// the file is written later, on a worker thread, so a name must be unique within the process even before any file
// exists. Six debug.takeScreenshot() calls on six consecutive frames used to give one file: every request picked the
// first screenshotNNN that did not exist on disk, the same one each time.
namespace
{
    // An empty folder of its own for each test, removed afterwards.
    class ShotFolder
    {
    public:
        ShotFolder()
            : mDir(std::filesystem::temp_directory_path() / uniqueName())
        {
            std::filesystem::create_directories(mDir);
        }
        ~ShotFolder()
        {
            std::error_code ec;
            std::filesystem::remove_all(mDir, ec);
        }
        ShotFolder(const ShotFolder&) = delete;
        ShotFolder& operator=(const ShotFolder&) = delete;

        const std::filesystem::path& path() const { return mDir; }

        void touch(const std::string& name) const { std::ofstream(mDir / name, std::ios::binary) << "x"; }

    private:
        static std::string uniqueName()
        {
            static std::atomic<unsigned> counter{ 0 };
            return "openeden_screenshotnames_"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_"
                + std::to_string(counter++);
        }

        std::filesystem::path mDir;
    };

    // A screenshot operation with no device and no window: generateFilename only names a file.
    vsg::ref_ptr<Render::Screenshot> makeShot()
    {
        return vsg::ref_ptr<Render::Screenshot>(
            new Render::Screenshot(nullptr, vsg::Image::create(), VK_IMAGE_LAYOUT_UNDEFINED));
    }

    TEST(RenderScreenshotNames, a_single_screenshot_in_an_empty_folder_is_screenshot000)
    {
        const ShotFolder folder;
        auto shot = makeShot();
        shot->generateFilename(folder.path(), "png");
        EXPECT_EQ(shot->writeFilename, folder.path() / "screenshot000.png");
    }

    TEST(RenderScreenshotNames, two_requests_in_a_row_before_any_file_is_written_get_two_names)
    {
        const ShotFolder folder;
        auto first = makeShot();
        auto second = makeShot();
        first->generateFilename(folder.path(), "png");
        second->generateFilename(folder.path(), "png");
        EXPECT_NE(first->writeFilename, second->writeFilename);
        EXPECT_EQ(first->writeFilename, folder.path() / "screenshot000.png");
        EXPECT_EQ(second->writeFilename, folder.path() / "screenshot001.png");
    }

    TEST(RenderScreenshotNames, six_requests_on_six_frames_get_six_names_in_order)
    {
        const ShotFolder folder;
        std::vector<vsg::ref_ptr<Render::Screenshot>> shots;
        for (int i = 0; i < 6; ++i)
        {
            shots.push_back(makeShot());
            shots.back()->generateFilename(folder.path(), "png");
        }
        const char* expected[] = { "screenshot000.png", "screenshot001.png", "screenshot002.png", "screenshot003.png",
            "screenshot004.png", "screenshot005.png" };
        for (std::size_t i = 0; i < shots.size(); ++i)
        {
            EXPECT_EQ(shots[i]->writeFilename, folder.path() / expected[i]) << "request " << i;
        }
    }

    TEST(RenderScreenshotNames, files_already_on_disk_are_still_skipped)
    {
        const ShotFolder folder;
        folder.touch("screenshot000.png");
        folder.touch("screenshot001.png");
        folder.touch("screenshot003.png");
        auto first = makeShot();
        auto second = makeShot();
        first->generateFilename(folder.path(), "png");
        second->generateFilename(folder.path(), "png");
        EXPECT_EQ(first->writeFilename, folder.path() / "screenshot002.png");
        EXPECT_EQ(second->writeFilename, folder.path() / "screenshot004.png");
    }

    TEST(RenderScreenshotNames, another_extension_is_another_name)
    {
        const ShotFolder folder;
        folder.touch("screenshot000.jpg");
        auto shot = makeShot();
        shot->generateFilename(folder.path(), "png");
        EXPECT_EQ(shot->writeFilename, folder.path() / "screenshot000.png");
    }

    TEST(RenderScreenshotNames, names_are_reserved_per_folder)
    {
        const ShotFolder a;
        const ShotFolder b;
        auto inA = makeShot();
        auto inB = makeShot();
        inA->generateFilename(a.path(), "png");
        inB->generateFilename(b.path(), "png");
        EXPECT_EQ(inA->writeFilename, a.path() / "screenshot000.png");
        EXPECT_EQ(inB->writeFilename, b.path() / "screenshot000.png");
    }

    TEST(RenderScreenshotNames, a_screenshot_that_is_gone_without_a_file_gives_its_name_back)
    {
        const ShotFolder folder;
        {
            auto unwritten = makeShot();
            unwritten->generateFilename(folder.path(), "png");
            EXPECT_EQ(unwritten->writeFilename, folder.path() / "screenshot000.png");
        }
        auto next = makeShot();
        next->generateFilename(folder.path(), "png");
        EXPECT_EQ(next->writeFilename, folder.path() / "screenshot000.png");
    }

    TEST(RenderScreenshotNames, a_written_file_keeps_its_name_after_the_screenshot_is_gone)
    {
        const ShotFolder folder;
        {
            auto written = makeShot();
            written->generateFilename(folder.path(), "png");
            std::ofstream(written->writeFilename, std::ios::binary) << "x";
        }
        auto next = makeShot();
        next->generateFilename(folder.path(), "png");
        EXPECT_EQ(next->writeFilename, folder.path() / "screenshot001.png");
    }

    TEST(RenderScreenshotNames, naming_one_screenshot_again_does_not_keep_its_first_name)
    {
        const ShotFolder folder;
        auto shot = makeShot();
        shot->generateFilename(folder.path(), "png");
        shot->generateFilename(folder.path(), "png");
        EXPECT_EQ(shot->writeFilename, folder.path() / "screenshot000.png");
        auto other = makeShot();
        other->generateFilename(folder.path(), "png");
        EXPECT_EQ(other->writeFilename, folder.path() / "screenshot001.png");
    }

    // 8 threads of 16 requests each, 128 requests in all, started together.
    TEST(RenderScreenshotNames, concurrent_requests_get_distinct_names)
    {
        const ShotFolder folder;
        constexpr std::size_t threadCount = 8;
        constexpr std::size_t perThread = 16;
        std::vector<std::vector<vsg::ref_ptr<Render::Screenshot>>> shots(threadCount);
        std::atomic<bool> go{ false };
        std::vector<std::thread> threads;
        for (std::size_t t = 0; t < threadCount; ++t)
        {
            threads.emplace_back([&shots, &go, &folder, t] {
                while (!go.load())
                    std::this_thread::yield();
                for (std::size_t i = 0; i < perThread; ++i)
                {
                    shots[t].push_back(makeShot());
                    shots[t].back()->generateFilename(folder.path(), "png");
                }
            });
        }
        go = true;
        for (auto& thread : threads)
            thread.join();

        std::set<std::filesystem::path> names;
        for (const auto& perThreadShots : shots)
        {
            for (const auto& shot : perThreadShots)
                names.insert(shot->writeFilename);
        }
        EXPECT_EQ(names.size(), threadCount * perThread);
        // And they are the first ones: nothing was skipped.
        EXPECT_EQ(names.count(folder.path() / "screenshot000.png"), 1u);
        EXPECT_EQ(names.count(folder.path() / "screenshot127.png"), 1u);
    }

    // Render::nameAndQueueScreenshot, the game's last step (second round of review): a screenshot is named only when
    // its copy has finished and it is handed over, so a request that never got a copy takes no number.

    std::vector<vsg::ref_ptr<Render::Screenshot>> handOver(
        const std::filesystem::path& dir, const std::vector<vsg::ref_ptr<Render::Screenshot>>& finished)
    {
        std::vector<vsg::ref_ptr<Render::Screenshot>> queued;
        auto options = vsg::Options::create();
        for (const auto& shot : finished)
        {
            Render::nameAndQueueScreenshot(
                shot, dir, "png", options, [&queued](vsg::ref_ptr<Render::Screenshot> s) { queued.push_back(s); });
        }
        return queued;
    }

    TEST(RenderScreenshotHandOver, a_finished_screenshot_is_named_and_queued)
    {
        const ShotFolder folder;
        auto shot = makeShot();
        const auto queued = handOver(folder.path(), { shot });
        ASSERT_EQ(queued.size(), 1u);
        EXPECT_EQ(queued[0], shot);
        EXPECT_EQ(shot->writeFilename, folder.path() / "screenshot000.png");
        EXPECT_NE(shot->writeOptions, nullptr);
    }

    TEST(RenderScreenshotHandOver, a_screenshot_is_named_when_handed_over_not_when_made)
    {
        const ShotFolder folder;
        auto dropped = makeShot(); // made for a request, never finished: never handed over
        auto first = makeShot();
        auto second = makeShot();
        EXPECT_TRUE(dropped->writeFilename.empty());
        const auto queued = handOver(folder.path(), { first, second });
        ASSERT_EQ(queued.size(), 2u);
        EXPECT_EQ(first->writeFilename, folder.path() / "screenshot000.png");
        EXPECT_EQ(second->writeFilename, folder.path() / "screenshot001.png");
        EXPECT_TRUE(dropped->writeFilename.empty());
    }

    TEST(RenderScreenshotHandOver, names_follow_the_order_of_handing_over)
    {
        const ShotFolder folder;
        auto earlier = makeShot();
        auto later = makeShot();
        handOver(folder.path(), { later });
        handOver(folder.path(), { earlier });
        EXPECT_EQ(later->writeFilename, folder.path() / "screenshot000.png");
        EXPECT_EQ(earlier->writeFilename, folder.path() / "screenshot001.png");
    }
}
