// OpenEden (task 0095, stage W-c): OMW::CaptureSource, the baker's source of static surfaces over the capture folder
// and the digests of the references. A fake provider stands in for the content files; generated captures only.

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <map>

#include "../../openmw/puddlestatics.hpp"

namespace
{
    constexpr std::uint32_t side = 8;
    constexpr float spacing = 64.f;

    class FakeRefs final : public OMW::RefsProvider
    {
    public:
        bool entry(std::int32_t cx, std::int32_t cy, Entry& out) override
        {
            ++mCalls;
            const auto it = mEntries.find({ cx, cy });
            if (it == mEntries.end())
                return false;
            out = it->second;
            return true;
        }
        std::map<std::pair<std::int32_t, std::int32_t>, Entry> mEntries;
        int mCalls = 0;
    };

    Puddle::Digest digestOf(std::uint8_t seed)
    {
        Puddle::Digest d{};
        d[0] = seed;
        return d;
    }

    Puddle::CaptureStore freshStore(const std::string& name)
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / ("capturesource_" + name);
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        return Puddle::CaptureStore(path);
    }

    void storeCapture(const Puddle::CaptureStore& store, int cx, int cy, const Puddle::Digest& digest, float top)
    {
        Puddle::StaticCapture c;
        c.mCellX = cx;
        c.mCellY = cy;
        c.mSide = side;
        c.mSpacing = spacing;
        c.mRefsDigest = digest;
        c.mTops.assign(side * side, std::nanf(""));
        c.mTops[10] = top;
        ASSERT_FALSE(store.store(c));
    }

    TEST(PuddleCaptureSource, the_digest_is_the_providers)
    {
        FakeRefs refs;
        refs.mEntries[{ 1, 2 }] = { digestOf(7), 3 };
        OMW::CaptureSource source(refs, freshStore("digest"), side, spacing);
        Puddle::Digest d{};
        ASSERT_TRUE(source.refsDigest(1, 2, d));
        EXPECT_EQ(d, digestOf(7));
        EXPECT_FALSE(source.refsDigest(5, 5, d));
    }

    TEST(PuddleCaptureSource, a_cell_without_references_needs_no_file_and_has_nothing)
    {
        FakeRefs refs;
        refs.mEntries[{ 0, 0 }] = { digestOf(1), 0 };
        OMW::CaptureSource source(refs, freshStore("empty"), side, spacing);
        std::vector<float> tops;
        ASSERT_TRUE(source.cellTops(0, 0, tops));
        ASSERT_EQ(tops.size(), side * side);
        for (const float t : tops)
            EXPECT_TRUE(std::isnan(t));
    }

    TEST(PuddleCaptureSource, a_cell_with_references_needs_a_capture_made_for_exactly_them)
    {
        FakeRefs refs;
        refs.mEntries[{ 0, 0 }] = { digestOf(1), 5 };
        const Puddle::CaptureStore store = freshStore("current");
        OMW::CaptureSource source(refs, store, side, spacing);
        std::vector<float> tops;
        EXPECT_FALSE(source.cellTops(0, 0, tops)); // nothing stored yet
        storeCapture(store, 0, 0, digestOf(1), 12.5f);
        ASSERT_TRUE(source.cellTops(0, 0, tops));
        EXPECT_FLOAT_EQ(tops[10], 12.5f);
        // The references change: the stored capture is stale and is not served, however recently it was cached.
        refs.mEntries[{ 0, 0 }] = { digestOf(2), 5 };
        OMW::CaptureSource later(refs, store, side, spacing);
        EXPECT_FALSE(later.cellTops(0, 0, tops));
    }

    TEST(PuddleCaptureSource, an_unreadable_cell_has_no_tops)
    {
        FakeRefs refs;
        OMW::CaptureSource source(refs, freshStore("unreadable"), side, spacing);
        std::vector<float> tops;
        EXPECT_FALSE(source.cellTops(3, 3, tops));
    }

    TEST(PuddleCaptureSource, a_cache_serves_a_cell_again_without_reading_the_file_and_is_bounded)
    {
        FakeRefs refs;
        const Puddle::CaptureStore store = freshStore("cache");
        for (int x = 0; x < 4; ++x)
        {
            refs.mEntries[{ x, 0 }] = { digestOf(static_cast<std::uint8_t>(x)), 2 };
            storeCapture(store, x, 0, digestOf(static_cast<std::uint8_t>(x)), static_cast<float>(x));
        }
        OMW::CaptureSource source(refs, store, side, spacing, 2);
        std::vector<float> tops;
        ASSERT_TRUE(source.cellTops(0, 0, tops));
        // Remove the file: a cached cell is still served, an uncached one is not.
        std::filesystem::remove(store.pathOf(0, 0));
        ASSERT_TRUE(source.cellTops(0, 0, tops));
        EXPECT_FLOAT_EQ(tops[10], 0.f);
        ASSERT_TRUE(source.cellTops(1, 0, tops));
        ASSERT_TRUE(source.cellTops(2, 0, tops)); // the cache holds two cells: (0, 0) is out now
        EXPECT_FALSE(source.cellTops(0, 0, tops));
    }
}
