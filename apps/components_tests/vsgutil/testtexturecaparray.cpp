// Tests for the texture downscaler's follow-up work (task 0049): arrays that must stay of one size, loader threads
// racing on one image, alpha tested images, and the memory budget setting's text. All images are generated; nothing
// needs a device or a file.

#include <components/vsgutil/readimage.hpp>
#include <components/vsgutil/texturecap.hpp>
#include <components/vsgutil/textureresize.hpp>

#include "texturetestutil.hpp"

#include <vsg/io/ReaderWriter.h>
#include <vsg/utils/SharedObjects.h>

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace
{
    using namespace TextureTest;
    using vsgUtil::ImageUse;
    using vsgUtil::TextureCap::Category;
    using vsgUtil::TextureCap::Kind;

    // Serves generated DDS files and ready-made images by name.
    class MemoryFiles : public vsg::Inherit<vsg::ReaderWriter, MemoryFiles>
    {
    public:
        std::map<std::string, std::vector<uint8_t>> dds;
        std::map<std::string, vsg::ref_ptr<vsg::Data>> images;

        vsg::ref_ptr<vsg::Object> read(const vsg::Path& filename, vsg::ref_ptr<const vsg::Options>) const override
        {
            const auto found = dds.find(filename.string());
            if (found != dds.end())
            {
                return readDds(found->second);
            }
            const auto image = images.find(filename.string());
            if (image != images.end())
            {
                return image->second;
            }
            return {};
        }
    };

    // The same cutout test image as the shrink tests: 2 by 2 blocks of two opaque and two clear texels, of 16
    // opacities, repeated over `size` by `size`.
    vsg::ref_ptr<vsg::Data> cutout(uint32_t size)
    {
        auto image = vsg::ubvec4Array2D::create(size, size, vsg::Data::Properties{ VK_FORMAT_R8G8B8A8_UNORM });
        for (uint32_t y = 0; y < size; y += 2)
        {
            for (uint32_t x = 0; x < size; x += 2)
            {
                const uint32_t block = (x / 2 + 4 * (y / 2)) % 16;
                const uint8_t v = static_cast<uint8_t>(16 * block + 15);
                image->set(x, y, vsg::ubvec4(255, 255, 255, v));
                image->set(x + 1, y, vsg::ubvec4(255, 255, 255, v));
                image->set(x, y + 1, vsg::ubvec4(255, 255, 255, 0));
                image->set(x + 1, y + 1, vsg::ubvec4(255, 255, 255, 0));
            }
        }
        return image;
    }

    int passing(const vsg::Data& data, int threshold)
    {
        int count = 0;
        const auto& array = static_cast<const vsg::ubvec4Array2D&>(data);
        for (uint32_t y = 0; y < array.height(); ++y)
        {
            for (uint32_t x = 0; x < array.width(); ++x)
            {
                if (array.at(x, y).a >= threshold)
                {
                    ++count;
                }
            }
        }
        return count;
    }

    class TextureCapArrayTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            mSaved = vsgUtil::TextureCap::getCaps();
            vsgUtil::TextureCap::Caps caps;
            caps.global = 16;
            vsgUtil::TextureCap::setCaps(caps);
            vsgUtil::TextureResize::report().reset();
            auto files = MemoryFiles::create();
            files->dds["chainA.dds"] = makeDds(DdsFormat::Bc1Legacy, 64, 64, 7);
            files->dds["chainB.dds"] = makeDds(DdsFormat::Bc1Legacy, 64, 64, 7);
            files->dds["single.dds"] = makeDds(DdsFormat::Bc1Legacy, 64, 64, 1);
            files->images["leaf.tga"] = cutout(32);
            mOptions = vsg::Options::create();
            mOptions->readerWriters = { files };
            mOptions->sharedObjects = vsg::SharedObjects::create();
        }

        void TearDown() override
        {
            vsgUtil::TextureCap::setCaps(mSaved);
            vsgUtil::TextureResize::report().reset();
        }

        static ImageUse terrain()
        {
            ImageUse use;
            use.category = Category::Terrain;
            return use;
        }

        vsg::ref_ptr<vsg::Options> mOptions;
        vsgUtil::TextureCap::Caps mSaved;
    };

    TEST_F(TextureCapArrayTest, an_array_of_images_that_can_all_be_capped_is_capped)
    {
        const auto images = vsgUtil::readArrayImages({ "chainA.dds", "chainB.dds" }, mOptions, terrain(), true);
        ASSERT_EQ(images.size(), 2u);
        EXPECT_EQ(images[0]->width(), 4u); // 16 texels in blocks
        EXPECT_EQ(images[1]->width(), 4u);
    }

    TEST_F(TextureCapArrayTest, one_image_that_cannot_be_capped_keeps_the_whole_array_at_full_size)
    {
        // A single level block compressed image cannot be made smaller; capping the others would leave layers of two
        // sizes, and the array draws such a layer grey.
        const auto images
            = vsgUtil::readArrayImages({ "chainA.dds", "single.dds", "chainB.dds" }, mOptions, terrain(), true);
        ASSERT_EQ(images.size(), 3u);
        for (const auto& image : images)
        {
            EXPECT_EQ(image->width(), 16u); // 64 texels in blocks
        }
    }

    TEST_F(TextureCapArrayTest, with_no_cap_an_array_is_read_as_it_is)
    {
        vsgUtil::TextureCap::setCaps({});
        const auto images = vsgUtil::readArrayImages({ "chainA.dds", "single.dds" }, mOptions, terrain(), true);
        EXPECT_EQ(images[0]->width(), 16u);
        EXPECT_EQ(images[1]->width(), 16u);
    }

    TEST_F(TextureCapArrayTest, a_missing_image_is_null_or_throws)
    {
        const auto images = vsgUtil::readArrayImages({ "chainA.dds", "nothing.dds" }, mOptions, terrain(), false);
        ASSERT_EQ(images.size(), 2u);
        EXPECT_TRUE(images[0].valid());
        EXPECT_FALSE(images[1].valid());
        EXPECT_THROW(vsgUtil::readArrayImages({ "nothing.dds" }, mOptions, terrain(), true), std::runtime_error);
    }

    TEST_F(TextureCapArrayTest, loader_threads_reading_one_file_count_it_once)
    {
        ImageUse use;
        use.category = Category::Object;
        // The full image is read (and shared) first, so that every thread caps the same one.
        auto full = vsgUtil::readOptionalImage("chainA.dds", mOptions);
        ASSERT_TRUE(full.valid());
        std::vector<std::thread> threads;
        std::vector<vsg::ref_ptr<vsg::Data>> results(8);
        for (size_t t = 0; t < results.size(); ++t)
        {
            threads.emplace_back([&, t] { results[t] = vsgUtil::readOptionalImage("chainA.dds", mOptions, use); });
        }
        for (auto& thread : threads)
        {
            thread.join();
        }
        const auto report = vsgUtil::TextureResize::report().snapshot();
        std::set<const vsg::Data*> distinct;
        for (const auto& result : results)
        {
            distinct.insert(result.get());
        }
        EXPECT_EQ(distinct.size(), 1u); // every thread got the one kept copy
        EXPECT_EQ(report.dropped, 1u);
        for (const auto& result : results)
        {
            ASSERT_TRUE(result.valid());
            EXPECT_EQ(result->width(), 4u);
        }
    }

    TEST_F(TextureCapArrayTest, an_alpha_cutoff_in_the_use_keeps_a_cutouts_coverage)
    {
        ImageUse plain;
        plain.category = Category::Object;
        ImageUse tested = plain;
        tested.alphaCutoff = 0.5f;

        auto thinned = vsgUtil::readOptionalImage("leaf.tga", mOptions, plain);
        auto kept = vsgUtil::readOptionalImage("leaf.tga", mOptions, tested);
        ASSERT_TRUE(thinned.valid() && kept.valid());
        EXPECT_EQ(thinned->width(), 16u);
        EXPECT_EQ(kept->width(), 16u);
        EXPECT_NE(thinned.get(), kept.get()); // the cutoff is part of the cache key
        // 32 by 32 source: a quarter of its 1024 texels pass the test at 0.5 (256 texels), so 64 of 256 after.
        EXPECT_EQ(passing(*kept, 128), 64);
        EXPECT_EQ(passing(*thinned, 128), 16);
    }

    TEST(TextureCapBudgetSetting, off_auto_and_a_number_of_mib)
    {
        using vsgUtil::TextureCap::parseBudget;
        EXPECT_FALSE(parseBudget("off").autoBudget);
        EXPECT_FALSE(parseBudget("").autoBudget);
        EXPECT_TRUE(parseBudget("off").valid);
        const auto automatic = parseBudget("auto");
        EXPECT_TRUE(automatic.autoBudget);
        EXPECT_EQ(automatic.simulatedMiB, 0u);
        const auto card = parseBudget("6144");
        EXPECT_TRUE(card.autoBudget);
        EXPECT_TRUE(card.valid);
        EXPECT_EQ(card.simulatedMiB, 6144u);
    }

    TEST(TextureCapBudgetSetting, anything_else_is_not_valid_and_counts_as_off)
    {
        using vsgUtil::TextureCap::parseBudget;
        for (const char* text : { "on", "AUTO", "0", "12x", "-1", "1 2" })
        {
            const auto setting = parseBudget(text);
            EXPECT_FALSE(setting.valid) << text;
            EXPECT_FALSE(setting.autoBudget) << text;
        }
    }

    TEST(TextureCapBudgetSetting, a_simulated_card_picks_its_bands_caps)
    {
        using namespace vsgUtil::TextureCap;
        for (const auto& [mib, objects, terrain] :
            { std::tuple{ 4096u, 1024, 512 }, std::tuple{ 6144u, 2048, 1024 }, std::tuple{ 8192u, 2048, 1024 } })
        {
            Caps caps;
            caps.autoBudget = true;
            caps.deviceLocalMiB = parseBudget(std::to_string(mib)).simulatedMiB;
            EXPECT_EQ(effectiveCap(Category::Object, caps), objects) << mib;
            EXPECT_EQ(effectiveCap(Category::Terrain, caps), terrain) << mib;
        }
    }
}
