// Tests for reading images through the texture downscaler (task 0049, stage b): vsgUtil::readOptionalImage and
// readImage with an ImageUse. The "files" are generated DDS bytes served by a reader in memory; nothing touches the
// disk or a device. Written from the spec before the implementation.

#include <components/vsgutil/readimage.hpp>
#include <components/vsgutil/texturecap.hpp>
#include <components/vsgutil/textureresize.hpp>

#include "texturetestutil.hpp"

#include <vsg/io/ReaderWriter.h>
#include <vsg/utils/SharedObjects.h>

#include <gtest/gtest.h>

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace TextureTest;
    using vsgUtil::ImageUse;
    using vsgUtil::TextureCap::Category;

    // Serves generated DDS files by name.
    class MemoryFiles : public vsg::Inherit<vsg::ReaderWriter, MemoryFiles>
    {
    public:
        std::map<std::string, std::vector<uint8_t>> files;

        vsg::ref_ptr<vsg::Object> read(const vsg::Path& filename, vsg::ref_ptr<const vsg::Options>) const override
        {
            const auto found = files.find(filename.string());
            if (found == files.end())
                return {};
            return readDds(found->second);
        }
    };

    class TextureCapReadTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            mSaved = vsgUtil::TextureCap::getCaps();
            vsgUtil::TextureCap::setCaps({});
            vsgUtil::TextureResize::report().reset();
            auto files = MemoryFiles::create();
            files->files["wall.dds"] = makeDds(DdsFormat::Bc1Legacy, 64, 64, 7);
            files->files["plain.dds"] = makeDds(DdsFormat::Rgba8Legacy, 64, 64, 7);
            mOptions = vsg::Options::create();
            mOptions->readerWriters = { files };
            mOptions->sharedObjects = vsg::SharedObjects::create();
        }

        void TearDown() override
        {
            vsgUtil::TextureCap::setCaps(mSaved);
            vsgUtil::TextureResize::report().reset();
        }

        static void capAll(int size)
        {
            vsgUtil::TextureCap::Caps caps;
            caps.global = size;
            vsgUtil::TextureCap::setCaps(caps);
        }

        static ImageUse use(Category category)
        {
            ImageUse result;
            result.category = category;
            return result;
        }

        vsg::ref_ptr<vsg::Options> mOptions;
        vsgUtil::TextureCap::Caps mSaved;
    };

    TEST_F(TextureCapReadTest, a_read_without_a_use_is_never_capped)
    {
        capAll(16);
        auto data = vsgUtil::readOptionalImage("wall.dds", mOptions);
        ASSERT_TRUE(data.valid());
        EXPECT_EQ(data->width(), 16u); // 64 texels in blocks
        EXPECT_EQ(data->properties.mipLevels, 7);
        const auto report = vsgUtil::TextureResize::report().snapshot();
        EXPECT_EQ(report.dropped + report.shrunk + report.leftBig, 0u);
    }

    TEST_F(TextureCapReadTest, ui_effect_and_unnamed_categories_are_never_capped)
    {
        capAll(16);
        for (Category category : { Category::None, Category::Ui, Category::Effect })
        {
            auto data = vsgUtil::readOptionalImage("wall.dds", mOptions, use(category));
            ASSERT_TRUE(data.valid());
            EXPECT_EQ(data->width(), 16u);
        }
        const auto report = vsgUtil::TextureResize::report().snapshot();
        EXPECT_EQ(report.dropped + report.shrunk + report.leftBig, 0u);
    }

    TEST_F(TextureCapReadTest, with_no_cap_set_the_same_object_comes_back_as_an_uncapped_read)
    {
        auto plain = vsgUtil::readOptionalImage("wall.dds", mOptions);
        auto object = vsgUtil::readOptionalImage("wall.dds", mOptions, use(Category::Object));
        auto terrain = vsgUtil::readOptionalImage("wall.dds", mOptions, use(Category::Terrain));
        ASSERT_TRUE(plain.valid());
        EXPECT_EQ(object.get(), plain.get());
        EXPECT_EQ(terrain.get(), plain.get());
    }

    TEST_F(TextureCapReadTest, an_object_read_is_capped_by_dropping_levels)
    {
        capAll(16);
        auto data = vsgUtil::readOptionalImage("wall.dds", mOptions, use(Category::Object));
        ASSERT_TRUE(data.valid());
        EXPECT_EQ(data->width(), 4u); // 16 texels
        EXPECT_EQ(data->height(), 4u);
        EXPECT_EQ(data->properties.mipLevels, 5);
        EXPECT_EQ(data->properties.format, VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
        EXPECT_EQ(vsgUtil::TextureResize::report().snapshot().dropped, 1u);
    }

    TEST_F(TextureCapReadTest, an_uncompressed_image_with_a_chain_is_capped_too)
    {
        capAll(32);
        auto data = vsgUtil::readOptionalImage("plain.dds", mOptions, use(Category::Object));
        ASSERT_TRUE(data.valid());
        EXPECT_EQ(data->width(), 32u);
        EXPECT_EQ(data->height(), 32u);
        EXPECT_EQ(data->properties.format, VK_FORMAT_R8G8B8A8_UNORM);
    }

    TEST_F(TextureCapReadTest, the_category_picks_its_own_cap)
    {
        vsgUtil::TextureCap::Caps caps;
        caps.terrain = 16;
        caps.object = 32;
        vsgUtil::TextureCap::setCaps(caps);
        EXPECT_EQ(vsgUtil::readOptionalImage("wall.dds", mOptions, use(Category::Terrain))->width(), 4u);
        EXPECT_EQ(vsgUtil::readOptionalImage("wall.dds", mOptions, use(Category::Object))->width(), 8u);
    }

    TEST_F(TextureCapReadTest, the_floor_keeps_a_map_from_going_below_it)
    {
        capAll(16);
        ImageUse height = use(Category::Object);
        height.floor = 32;
        auto data = vsgUtil::readOptionalImage("wall.dds", mOptions, height);
        ASSERT_TRUE(data.valid());
        EXPECT_EQ(data->width(), 8u); // 32 texels
    }

    TEST_F(TextureCapReadTest, one_file_capped_for_one_category_is_shared_by_its_users)
    {
        capAll(16);
        auto a = vsgUtil::readOptionalImage("wall.dds", mOptions, use(Category::Object));
        auto b = vsgUtil::readOptionalImage("wall.dds", mOptions, use(Category::Object));
        ASSERT_TRUE(a.valid());
        EXPECT_EQ(a.get(), b.get());
        EXPECT_EQ(vsgUtil::TextureResize::report().snapshot().dropped, 1u); // capped once
    }

    TEST_F(TextureCapReadTest, caps_of_different_sizes_do_not_collide)
    {
        vsgUtil::TextureCap::Caps caps;
        caps.terrain = 16;
        caps.object = 32;
        vsgUtil::TextureCap::setCaps(caps);
        auto terrain = vsgUtil::readOptionalImage("wall.dds", mOptions, use(Category::Terrain));
        auto object = vsgUtil::readOptionalImage("wall.dds", mOptions, use(Category::Object));
        auto plain = vsgUtil::readOptionalImage("wall.dds", mOptions);
        EXPECT_NE(terrain.get(), object.get());
        EXPECT_NE(terrain.get(), plain.get());
        EXPECT_NE(object.get(), plain.get());
        EXPECT_EQ(plain->width(), 16u); // the full image is still there for an uncapped reader
    }

    TEST_F(TextureCapReadTest, the_full_image_is_not_changed_by_a_capped_read)
    {
        capAll(16);
        auto plain = vsgUtil::readOptionalImage("wall.dds", mOptions);
        const auto before = bytesFrom(*plain, 0);
        auto capped = vsgUtil::readOptionalImage("wall.dds", mOptions, use(Category::Object));
        ASSERT_TRUE(capped.valid());
        EXPECT_EQ(bytesFrom(*plain, 0), before);
        EXPECT_EQ(plain->properties.mipLevels, 7);
    }

    TEST_F(TextureCapReadTest, a_missing_file_is_still_null_and_readImage_still_throws)
    {
        capAll(16);
        EXPECT_FALSE(vsgUtil::readOptionalImage("nothing.dds", mOptions, use(Category::Object)).valid());
        EXPECT_THROW(vsgUtil::readImage("nothing.dds", mOptions, use(Category::Object)), std::runtime_error);
        auto data = vsgUtil::readImage("wall.dds", mOptions, use(Category::Object));
        ASSERT_TRUE(data.valid());
        EXPECT_EQ(data->width(), 4u);
    }

    TEST_F(TextureCapReadTest, without_shared_objects_each_read_is_capped_on_its_own)
    {
        mOptions->sharedObjects = nullptr;
        capAll(16);
        auto a = vsgUtil::readOptionalImage("wall.dds", mOptions, use(Category::Object));
        auto b = vsgUtil::readOptionalImage("wall.dds", mOptions, use(Category::Object));
        ASSERT_TRUE(a.valid() && b.valid());
        EXPECT_EQ(a->width(), 4u);
        EXPECT_EQ(b->width(), 4u);
    }
}
