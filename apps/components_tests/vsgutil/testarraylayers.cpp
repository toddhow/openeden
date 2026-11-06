// Task 0089: which layers a terrain array can take (arrayLayerConvertible), and the parallel read of an array's
// images (readArrayImages with several threads). All data is generated; nothing needs a device or a file.

#include <components/vsgutil/arraytexture.hpp>
#include <components/vsgutil/readimage.hpp>

#include "texturetestutil.hpp"

#include <vsg/io/ReaderWriter.h>
#include <vsg/utils/SharedObjects.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace TextureTest;
    using vsgUtil::LayerShape;

    LayerShape rgba(uint32_t size, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM)
    {
        return LayerShape{ format, false, 4, size, size };
    }

    LayerShape block(VkFormat format, uint32_t size)
    {
        return LayerShape{ format, true, 0, size, size };
    }

    TEST(ArrayLayerConvertible, an_identical_layer_is_convertible)
    {
        EXPECT_TRUE(vsgUtil::arrayLayerConvertible(rgba(2048), rgba(2048)));
        EXPECT_TRUE(vsgUtil::arrayLayerConvertible(
            block(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 2048), block(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 2048)));
    }

    TEST(ArrayLayerConvertible, an_rgba8_layer_of_another_size_is_resampled)
    {
        EXPECT_TRUE(vsgUtil::arrayLayerConvertible(rgba(1024), rgba(2048)));
        EXPECT_TRUE(vsgUtil::arrayLayerConvertible(rgba(4096), rgba(2048)));
    }

    TEST(ArrayLayerConvertible, an_rgba8_array_decodes_bc1_bc2_and_bc3_layers_of_any_size)
    {
        for (const VkFormat format : { VK_FORMAT_BC1_RGB_UNORM_BLOCK, VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
                 VK_FORMAT_BC2_UNORM_BLOCK, VK_FORMAT_BC3_UNORM_BLOCK, VK_FORMAT_BC3_SRGB_BLOCK })
        {
            EXPECT_TRUE(vsgUtil::arrayLayerConvertible(block(format, 256), rgba(2048))) << format;
            EXPECT_TRUE(vsgUtil::arrayLayerConvertible(block(format, 2048), rgba(2048))) << format;
        }
    }

    TEST(ArrayLayerConvertible, bc7_and_other_block_formats_are_not_decoded)
    {
        EXPECT_FALSE(vsgUtil::arrayLayerConvertible(block(VK_FORMAT_BC7_UNORM_BLOCK, 4096), rgba(2048)));
        EXPECT_FALSE(vsgUtil::arrayLayerConvertible(block(VK_FORMAT_BC5_UNORM_BLOCK, 2048), rgba(2048)));
    }

    TEST(ArrayLayerConvertible, a_block_compressed_array_takes_its_own_size_and_format_and_bc3_of_its_size_in_bc1)
    {
        const auto bc1 = block(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 2048);
        EXPECT_FALSE(vsgUtil::arrayLayerConvertible(block(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 256), bc1));
        EXPECT_FALSE(vsgUtil::arrayLayerConvertible(rgba(2048), bc1));
        EXPECT_FALSE(vsgUtil::arrayLayerConvertible(block(VK_FORMAT_BC7_UNORM_BLOCK, 2048), bc1));
        EXPECT_TRUE(vsgUtil::arrayLayerConvertible(block(VK_FORMAT_BC3_UNORM_BLOCK, 2048), bc1));
        EXPECT_FALSE(vsgUtil::arrayLayerConvertible(block(VK_FORMAT_BC3_UNORM_BLOCK, 1024), bc1));
        EXPECT_FALSE(vsgUtil::arrayLayerConvertible(
            block(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 2048), block(VK_FORMAT_BC3_UNORM_BLOCK, 2048)));
    }

    TEST(ArrayLayerConvertible, an_rgba8_array_does_not_take_another_uncompressed_format_or_a_one_byte_layer)
    {
        EXPECT_FALSE(
            vsgUtil::arrayLayerConvertible(LayerShape{ VK_FORMAT_R8_UNORM, false, 1, 2048, 2048 }, rgba(2048)));
        EXPECT_FALSE(
            vsgUtil::arrayLayerConvertible(LayerShape{ VK_FORMAT_R8G8B8_UNORM, false, 3, 2048, 2048 }, rgba(2048)));
    }

    TEST(ArrayLayerConvertible, a_layer_is_described_by_its_texels_not_its_blocks)
    {
        const auto data = readDds(makeDds(DdsFormat::Bc1Legacy, 64, 32, 1));
        ASSERT_TRUE(data.valid());
        const LayerShape shape = vsgUtil::layerShapeOf(*data);
        EXPECT_TRUE(shape.blockCompressed);
        EXPECT_EQ(shape.width, 64u);
        EXPECT_EQ(shape.height, 32u);
        const auto rgbaData = readDds(makeDds(DdsFormat::Rgba8Legacy, 64, 32, 1));
        ASSERT_TRUE(rgbaData.valid());
        const LayerShape rgbaShape = vsgUtil::layerShapeOf(*rgbaData);
        EXPECT_FALSE(rgbaShape.blockCompressed);
        EXPECT_EQ(rgbaShape.bytesPerTexel, 4u);
        EXPECT_EQ(rgbaShape.width, 64u);
        EXPECT_EQ(rgbaShape.height, 32u);
    }

    TEST(ArrayReadThreadCount, is_never_zero_and_never_more_than_the_images)
    {
        EXPECT_EQ(vsgUtil::arrayReadThreadCount(0, 4), 1u);
        EXPECT_EQ(vsgUtil::arrayReadThreadCount(1, 4), 1u);
        EXPECT_EQ(vsgUtil::arrayReadThreadCount(3, 8), 3u);
        EXPECT_EQ(vsgUtil::arrayReadThreadCount(100, 4), 4u);
        EXPECT_GE(vsgUtil::arrayReadThreadCount(100, 0), 1u);
        EXPECT_LE(vsgUtil::arrayReadThreadCount(100, 0), 8u);
    }

    // Serves a DDS for any name "<size>_<n>.dds" (a square RGBA8 image of that size), sleeping a little and counting
    // how many reads are in progress at once.
    class SlowFiles : public vsg::Inherit<vsg::ReaderWriter, SlowFiles>
    {
    public:
        mutable std::atomic<int> active{ 0 };
        mutable std::atomic<int> maxActive{ 0 };

        vsg::ref_ptr<vsg::Object> read(const vsg::Path& filename, vsg::ref_ptr<const vsg::Options>) const override
        {
            const std::string name = filename.string();
            if (name.rfind("missing", 0) == 0)
                return {};
            const uint32_t size = static_cast<uint32_t>(std::stoul(name.substr(0, name.find('_'))));
            const int now = ++active;
            int seen = maxActive.load();
            while (now > seen && !maxActive.compare_exchange_weak(seen, now))
            {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            --active;
            return readDds(makeDds(DdsFormat::Rgba8Legacy, size, size, 1));
        }
    };

    class ArrayReadThreadsTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            mFiles = SlowFiles::create();
            mOptions = vsg::Options::create();
            mOptions->readerWriters = { mFiles };
            mOptions->sharedObjects = vsg::SharedObjects::create();
        }

        vsg::ref_ptr<SlowFiles> mFiles;
        vsg::ref_ptr<vsg::Options> mOptions;
    };

    TEST_F(ArrayReadThreadsTest, several_threads_read_at_the_same_time)
    {
        std::vector<std::string> paths;
        for (int n = 0; n < 16; ++n)
            paths.push_back("8_" + std::to_string(n) + ".dds");
        const auto images = vsgUtil::readArrayImages(paths, mOptions, {}, true, 4);
        ASSERT_EQ(images.size(), 16u);
        EXPECT_GE(mFiles->maxActive.load(), 2);
        EXPECT_LE(mFiles->maxActive.load(), 4);
    }

    TEST_F(ArrayReadThreadsTest, the_result_keeps_the_order_of_the_paths)
    {
        std::vector<std::string> paths;
        for (uint32_t n = 0; n < 12; ++n)
            paths.push_back(std::to_string(4 + 4 * n) + "_x.dds");
        const auto images = vsgUtil::readArrayImages(paths, mOptions, {}, true, 4);
        ASSERT_EQ(images.size(), paths.size());
        for (uint32_t n = 0; n < 12; ++n)
        {
            ASSERT_TRUE(images[n].valid());
            EXPECT_EQ(images[n]->width(), 4 + 4 * n) << n;
        }
    }

    TEST_F(ArrayReadThreadsTest, one_thread_reads_one_at_a_time)
    {
        std::vector<std::string> paths{ "8_a.dds", "8_b.dds", "8_c.dds" };
        vsgUtil::readArrayImages(paths, mOptions, {}, true, 1);
        EXPECT_EQ(mFiles->maxActive.load(), 1);
    }

    TEST_F(ArrayReadThreadsTest, the_first_failure_in_path_order_is_the_one_that_throws)
    {
        std::vector<std::string> paths{ "8_a.dds", "missing_two.dds", "8_b.dds", "missing_four.dds" };
        try
        {
            vsgUtil::readArrayImages(paths, mOptions, {}, true, 4);
            FAIL() << "nothing thrown";
        }
        catch (const std::runtime_error& e)
        {
            EXPECT_NE(std::string(e.what()).find("missing_two.dds"), std::string::npos) << e.what();
        }
    }

    TEST_F(ArrayReadThreadsTest, a_missing_optional_image_is_null_and_the_others_are_read)
    {
        std::vector<std::string> paths{ "8_a.dds", "missing_two.dds", "8_b.dds" };
        const auto images = vsgUtil::readArrayImages(paths, mOptions, {}, false, 4);
        ASSERT_EQ(images.size(), 3u);
        EXPECT_TRUE(images[0].valid());
        EXPECT_FALSE(images[1].valid());
        EXPECT_TRUE(images[2].valid());
    }

    TEST_F(ArrayReadThreadsTest, no_paths_gives_no_images)
    {
        EXPECT_TRUE(vsgUtil::readArrayImages({}, mOptions, {}, true, 4).empty());
    }
}
