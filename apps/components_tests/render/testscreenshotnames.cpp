#include <gtest/gtest.h>

#include <components/render/screenshot.hpp>

#include <vsg/core/Array2D.h>
#include <vsg/core/observer_ptr.h>
#include <vsg/io/Options.h>
#include <vsg/state/Image.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
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
        // It took no number: handed over after them, it is the next one.
        const auto later = handOver(folder.path(), { dropped });
        ASSERT_EQ(later.size(), 1u);
        EXPECT_EQ(dropped->writeFilename, folder.path() / "screenshot002.png");
    }

    // Render::ScreenshotWriter (third review round): the screenshots handed to the writer thread are kept until they
    // have run, so that the engine can wait for them at quit instead of losing them in a stopped queue.

    // A screenshot that does no Vulkan work when it runs.
    struct FakeShot : public Render::Screenshot
    {
        FakeShot()
            : Screenshot(nullptr, vsg::Image::create(), VK_IMAGE_LAYOUT_UNDEFINED)
        {
        }
        void operate() override {}
    };

    vsg::ref_ptr<Render::Screenshot> makeFake()
    {
        return vsg::ref_ptr<Render::Screenshot>(new FakeShot);
    }

    TEST(RenderScreenshotWriter, a_written_screenshot_goes_to_the_queue_and_is_pending_until_it_has_run)
    {
        std::vector<vsg::ref_ptr<Render::Screenshot>> queue;
        Render::ScreenshotWriter writer([&queue](vsg::ref_ptr<Render::Screenshot> s) { queue.push_back(s); });
        auto shot = makeFake();
        writer.write(shot);
        ASSERT_EQ(queue.size(), 1u);
        EXPECT_EQ(queue[0], shot);
        EXPECT_EQ(writer.pending(), 1u);
        shot->run();
        EXPECT_EQ(writer.pending(), 0u);
    }

    TEST(RenderScreenshotWriter, waiting_returns_nothing_when_the_writer_runs_everything)
    {
        std::vector<vsg::ref_ptr<Render::Screenshot>> queue;
        Render::ScreenshotWriter writer([&queue](vsg::ref_ptr<Render::Screenshot> s) { queue.push_back(s); });
        for (int i = 0; i < 4; ++i)
            writer.write(makeFake());
        std::thread worker([&queue] {
            for (auto& shot : queue)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                shot->run();
            }
        });
        const auto unwritten = writer.waitForAll(std::chrono::seconds(10));
        worker.join();
        EXPECT_TRUE(unwritten.empty());
        EXPECT_EQ(writer.pending(), 0u);
    }

    TEST(RenderScreenshotWriter, waiting_gives_back_what_has_not_run_when_the_time_is_up)
    {
        std::vector<vsg::ref_ptr<Render::Screenshot>> queue;
        Render::ScreenshotWriter writer([&queue](vsg::ref_ptr<Render::Screenshot> s) { queue.push_back(s); });
        auto ran = makeFake();
        auto stuck = makeFake();
        writer.write(ran);
        writer.write(stuck);
        ran->run();
        const auto unwritten = writer.waitForAll(std::chrono::milliseconds(50));
        ASSERT_EQ(unwritten.size(), 1u);
        EXPECT_EQ(unwritten[0], stuck);
        EXPECT_EQ(writer.pending(), 0u); // forgotten
    }

    TEST(RenderScreenshotWriter, the_limit_is_for_all_of_them_not_for_each)
    {
        std::vector<vsg::ref_ptr<Render::Screenshot>> queue;
        Render::ScreenshotWriter writer([&queue](vsg::ref_ptr<Render::Screenshot> s) { queue.push_back(s); });
        for (int i = 0; i < 5; ++i)
            writer.write(makeFake());
        const auto start = std::chrono::steady_clock::now();
        const auto unwritten = writer.waitForAll(std::chrono::milliseconds(200));
        const auto waited = std::chrono::steady_clock::now() - start;
        EXPECT_EQ(unwritten.size(), 5u);
        EXPECT_GE(waited, std::chrono::milliseconds(200));
        EXPECT_LT(waited, std::chrono::milliseconds(800)); // five waits of the whole limit would be 1000 ms
    }

    TEST(RenderScreenshotWriter, screenshots_that_have_run_are_forgotten_when_the_next_is_written)
    {
        std::vector<vsg::ref_ptr<Render::Screenshot>> queue;
        Render::ScreenshotWriter writer([&queue](vsg::ref_ptr<Render::Screenshot> s) { queue.push_back(s); });
        auto first = makeFake();
        writer.write(first);
        first->run();
        vsg::observer_ptr<Render::Screenshot> watch(first);
        first = nullptr;
        queue.clear();
        writer.write(makeFake());
        EXPECT_FALSE(watch.ref_ptr().valid()); // the writer no longer holds the one that ran
        EXPECT_EQ(writer.pending(), 1u);
    }

    // Render::makeOpaque (third review round): window captures are written opaque, as OpenMW 0.51 wrote them.

    TEST(RenderScreenshotOpaque, every_alpha_of_a_bgra_picture_becomes_255_and_the_colours_stay)
    {
        auto picture = vsg::ubvec4Array2D::create(3, 2, vsg::Data::Properties(VK_FORMAT_B8G8R8A8_UNORM));
        for (std::uint32_t y = 0; y < 2; ++y)
        {
            for (std::uint32_t x = 0; x < 3; ++x)
            {
                picture->set(x, y,
                    vsg::ubvec4(static_cast<std::uint8_t>(10 * x), static_cast<std::uint8_t>(20 * y), 7,
                        static_cast<std::uint8_t>(40 * (x + y))));
            }
        }
        Render::makeOpaque(*picture);
        for (std::uint32_t y = 0; y < 2; ++y)
        {
            for (std::uint32_t x = 0; x < 3; ++x)
            {
                const vsg::ubvec4 pixel = picture->at(x, y);
                EXPECT_EQ(static_cast<int>(pixel.a), 255) << x << "," << y;
                EXPECT_EQ(static_cast<int>(pixel.r), static_cast<int>(10 * x));
                EXPECT_EQ(static_cast<int>(pixel.g), static_cast<int>(20 * y));
                EXPECT_EQ(static_cast<int>(pixel.b), 7);
            }
        }
    }

    TEST(RenderScreenshotOpaque, an_rgba_picture_too)
    {
        auto picture = vsg::ubvec4Array2D::create(2, 2, vsg::Data::Properties(VK_FORMAT_R8G8B8A8_UNORM));
        picture->set(1, 1, vsg::ubvec4(1, 2, 3, 0));
        Render::makeOpaque(*picture);
        EXPECT_EQ(static_cast<int>(picture->at(1, 1).a), 255);
        EXPECT_EQ(static_cast<int>(picture->at(1, 1).r), 1);
    }

    TEST(RenderScreenshotOpaque, three_channels_and_other_formats_are_left_alone)
    {
        auto rgb = vsg::ubvec3Array2D::create(1, 1, vsg::Data::Properties(VK_FORMAT_R8G8B8_UNORM));
        rgb->set(0, 0, vsg::ubvec3(9, 8, 7));
        Render::makeOpaque(*rgb);
        EXPECT_TRUE(rgb->at(0, 0) == vsg::ubvec3(9, 8, 7));

        // Four channels of another kind (an sRGB tag, as the world map's never is): not a window capture's format.
        auto other = vsg::ubvec4Array2D::create(1, 1, vsg::Data::Properties(VK_FORMAT_R8G8B8A8_SRGB));
        other->set(0, 0, vsg::ubvec4(1, 2, 3, 4));
        Render::makeOpaque(*other);
        EXPECT_EQ(static_cast<int>(other->at(0, 0).a), 4);
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
