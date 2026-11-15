// Task 0177 (neutral world saves, stage N6, stage 3): the Skyrim SE pack's "loadorder" save chunk. Written first; the
// handler was a stub that accepted everything when these ran red.
#include <game_skyrimse/load_order_stamp.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

namespace
{
    using game_skyrimse::LoadOrderStamp;

    class MemoryWriter final : public openeden::sdk::ChunkWriter
    {
    public:
        void write(const void* data, std::size_t size) override
        {
            const auto* bytes = static_cast<const unsigned char*>(data);
            mBytes.insert(mBytes.end(), bytes, bytes + size);
        }
        std::vector<unsigned char> mBytes;
    };

    class MemoryReader final : public openeden::sdk::ChunkReader
    {
    public:
        explicit MemoryReader(const std::vector<unsigned char>& bytes)
            : mBytes(bytes)
        {
        }
        bool read(void* data, std::size_t size) override
        {
            if (size > remaining())
                return false;
            std::memcpy(data, mBytes.data() + mPos, size);
            mPos += size;
            return true;
        }
        std::size_t remaining() const override { return mBytes.size() - mPos; }

    private:
        const std::vector<unsigned char>& mBytes;
        std::size_t mPos = 0;
    };

    const std::vector<std::string> kOrder = { "Skyrim.esm", "Update.esm", "Dawnguard.esm" };

    std::vector<unsigned char> written(const std::vector<std::string>& names)
    {
        LoadOrderStamp stamp;
        stamp.setLoadOrder(names);
        MemoryWriter writer;
        stamp.write(writer);
        return writer.mBytes;
    }

    TEST(SkyrimLoadOrderStamp, hasAFixedNameAndVersion)
    {
        LoadOrderStamp stamp;
        EXPECT_EQ(stamp.chunkName(), "loadorder");
        EXPECT_EQ(stamp.chunkVersion(), 1u);
    }

    TEST(SkyrimLoadOrderStamp, aSaveReadsBackOverTheSameLoadOrder)
    {
        const auto bytes = written(kOrder);
        LoadOrderStamp now;
        now.setLoadOrder(kOrder);
        MemoryReader reader(bytes);
        EXPECT_TRUE(now.read(reader, 1)) << now.refusal();
    }

    TEST(SkyrimLoadOrderStamp, theNamesAreComparedWithoutRegardToCase)
    {
        const auto bytes = written(kOrder);
        LoadOrderStamp now;
        now.setLoadOrder({ "SKYRIM.ESM", "update.esm", "Dawnguard.ESM" });
        MemoryReader reader(bytes);
        EXPECT_TRUE(now.read(reader, 1)) << now.refusal();
    }

    TEST(SkyrimLoadOrderStamp, theSamePluginsInAnotherOrderAreRefusedAndTheReasonNamesThePosition)
    {
        const auto bytes = written(kOrder);
        LoadOrderStamp now;
        now.setLoadOrder({ "Skyrim.esm", "Dawnguard.esm", "Update.esm" });
        MemoryReader reader(bytes);
        EXPECT_FALSE(now.read(reader, 1));
        EXPECT_NE(now.refusal().find("position 2"), std::string::npos) << now.refusal();
    }

    TEST(SkyrimLoadOrderStamp, anExtraOrMissingPluginIsRefused)
    {
        const auto bytes = written(kOrder);
        {
            LoadOrderStamp now;
            now.setLoadOrder({ "Skyrim.esm", "Update.esm" });
            MemoryReader reader(bytes);
            EXPECT_FALSE(now.read(reader, 1));
        }
        {
            LoadOrderStamp now;
            now.setLoadOrder({ "Skyrim.esm", "Update.esm", "Dawnguard.esm", "Mod.esp" });
            MemoryReader reader(bytes);
            EXPECT_FALSE(now.read(reader, 1));
        }
    }

    TEST(SkyrimLoadOrderStamp, everyTruncationIsRefusedNotCrashed)
    {
        const auto bytes = written(kOrder);
        for (std::size_t n = 0; n < bytes.size(); ++n)
        {
            const std::vector<unsigned char> cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n));
            LoadOrderStamp now;
            now.setLoadOrder(kOrder);
            MemoryReader reader(cut);
            EXPECT_FALSE(now.read(reader, 1)) << n;
        }
    }

    TEST(SkyrimLoadOrderStamp, aHugeCountOrTrailingBytesAreRefused)
    {
        std::vector<unsigned char> bytes = written(kOrder);
        {
            std::vector<unsigned char> huge = bytes;
            huge[3] = 0x7F; // count = 0x7F...
            LoadOrderStamp now;
            now.setLoadOrder(kOrder);
            MemoryReader reader(huge);
            EXPECT_FALSE(now.read(reader, 1));
        }
        bytes.push_back(0);
        LoadOrderStamp now;
        now.setLoadOrder(kOrder);
        MemoryReader reader(bytes);
        EXPECT_FALSE(now.read(reader, 1));
    }

    TEST(SkyrimLoadOrderStamp, anotherVersionIsRefused)
    {
        const auto bytes = written(kOrder);
        LoadOrderStamp now;
        now.setLoadOrder(kOrder);
        MemoryReader reader(bytes);
        EXPECT_FALSE(now.read(reader, 2));
    }

    TEST(SkyrimLoadOrderStamp, aRefusalIsReportedToTheSinkOnceWithItsReason)
    {
        const auto bytes = written(kOrder);
        LoadOrderStamp now;
        now.setLoadOrder({ "Skyrim.esm", "Dawnguard.esm", "Update.esm" });
        std::vector<std::string> told;
        now.setRefusalSink([&told](const std::string& reason) { told.push_back(reason); });
        MemoryReader reader(bytes);
        EXPECT_FALSE(now.read(reader, 1));
        ASSERT_EQ(told.size(), 1u);
        EXPECT_EQ(told[0], now.refusal());
        // an accepted read tells nothing
        LoadOrderStamp same;
        same.setLoadOrder(kOrder);
        same.setRefusalSink([&told](const std::string& reason) { told.push_back(reason); });
        MemoryReader again(bytes);
        EXPECT_TRUE(same.read(again, 1));
        EXPECT_EQ(told.size(), 1u);
    }

    TEST(SkyrimLoadOrderStamp, anEmptyLoadOrderRoundTrips)
    {
        const auto bytes = written({});
        LoadOrderStamp now;
        MemoryReader reader(bytes);
        EXPECT_TRUE(now.read(reader, 1));
    }
}
