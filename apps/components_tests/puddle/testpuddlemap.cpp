#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>

#include <components/puddle/puddlemap.hpp>

// Task 0095, stage W-b: the payload of one baked cell. The strict reader is the part that meets untrusted bytes.

namespace
{
    using namespace Puddle;

    CellMap sampleCell(std::uint32_t side = 8, bool pits = true)
    {
        CellMap c;
        c.mCellX = -3;
        c.mCellY = 7;
        c.mSide = side;
        c.mSpacing = 64.f;
        c.mHeightBase = -120.f;
        for (std::size_t i = 0; i < c.mParameterDigest.size(); ++i)
        {
            c.mParameterDigest[i] = static_cast<std::uint8_t>(i);
            c.mInputDigest[i] = static_cast<std::uint8_t>(200 - i);
        }
        const std::size_t n = static_cast<std::size_t>(side) * side;
        c.mTexelFlags.assign(n, 0);
        c.mTop.assign(n, 0);
        for (std::size_t i = 0; i < n; ++i)
        {
            c.mTop[i] = static_cast<std::uint16_t>(1000 + i * 3);
            c.mTexelFlags[i] = static_cast<std::uint8_t>(i % 9 == 8 ? texelSink : i % 8);
        }
        if (!pits)
        {
            c.mFlags = flagNoPits;
            return c;
        }
        c.mCapacity.assign(n, 0);
        c.mBasinMax.assign(n, 0);
        c.mCatchment.assign(n, 0);
        c.mBasinId.assign(n, noBasin);
        // Two pits: texels 10, 11 (pit 0) and 40 (pit 1).
        for (const std::size_t i : { 10u, 11u })
        {
            c.mBasinId[i] = 0;
            c.mCapacity[i] = static_cast<std::uint8_t>(40 + i);
            c.mBasinMax[i] = 60;
            c.mCatchment[i] = 33;
        }
        c.mBasinId[40] = 1;
        c.mCapacity[40] = 20;
        c.mBasinMax[40] = 20;
        c.mCatchment[40] = 100;
        c.mBasins = { BasinInfo{ 17, 60, 33 }, BasinInfo{ 16, 20, 100 } };
        return c;
    }

    std::vector<std::uint8_t> bytesOf(const CellMap& cell)
    {
        std::vector<std::uint8_t> bytes;
        EXPECT_FALSE(writeCell(cell, bytes)) << "the sample cell must be writable";
        return bytes;
    }

    TEST(PuddleMap, a_cell_round_trips_byte_for_byte)
    {
        for (const bool pits : { true, false })
        {
            const CellMap cell = sampleCell(8, pits);
            const std::vector<std::uint8_t> bytes = bytesOf(cell);
            CellMap back;
            ASSERT_FALSE(parseCell(bytes, back));
            EXPECT_EQ(back, cell);
            EXPECT_EQ(bytesOf(back), bytes);
        }
    }

    TEST(PuddleMap, the_size_follows_the_formula)
    {
        for (const bool pits : { true, false })
        {
            const CellMap cell = sampleCell(8, pits);
            const std::vector<std::uint8_t> bytes = bytesOf(cell);
            EXPECT_EQ(bytes.size(), payloadSize(8, cell.mFlags, static_cast<std::uint32_t>(cell.mBasins.size())));
        }
        // 64 texels: 108 + 64 flags + 128 top + (192 + 128 + 16) for the pits.
        EXPECT_EQ(payloadSize(8, 0, 2), 108u + 64 + 128 + 192 + 128 + 16);
        EXPECT_EQ(payloadSize(8, flagNoPits, 0), 108u + 64 + 128);
    }

    TEST(PuddleMap, the_header_has_the_documented_layout)
    {
        const std::vector<std::uint8_t> bytes = bytesOf(sampleCell());
        EXPECT_EQ(std::string(bytes.begin(), bytes.begin() + 7), "EDENPUD");
        EXPECT_EQ(bytes[7], 0);
        EXPECT_EQ(bytes[8], 1); // version
        EXPECT_EQ(bytes[12], 0xFD); // cell x = -3
        EXPECT_EQ(bytes[13], 0xFF);
        EXPECT_EQ(bytes[16], 7);
        EXPECT_EQ(bytes[20], 8); // side
        EXPECT_EQ(bytes[36], 2); // basin count
        EXPECT_EQ(bytes[44], 0); // first parameter digest byte
        EXPECT_EQ(bytes[45], 1);
        EXPECT_EQ(bytes[76], 200); // first input digest byte
        EXPECT_EQ(bytes.size(), payloadSize(8, 0, 2));
    }

    TEST(PuddleMap, every_truncation_is_refused)
    {
        const std::vector<std::uint8_t> bytes = bytesOf(sampleCell());
        CellMap out;
        for (std::size_t length = 0; length < bytes.size(); ++length)
            EXPECT_TRUE(parseCell(bytes.data(), length, out)) << "length " << length;
        EXPECT_TRUE(parseCell(bytes.data(), 0, out));
    }

    TEST(PuddleMap, trailing_bytes_are_refused)
    {
        std::vector<std::uint8_t> bytes = bytesOf(sampleCell());
        bytes.push_back(0);
        CellMap out;
        const Error error = parseCell(bytes, out);
        ASSERT_TRUE(error);
        EXPECT_EQ(error.mField, "size");
    }

    TEST(PuddleMap, header_fields_are_checked_with_their_names)
    {
        const std::vector<std::uint8_t> good = bytesOf(sampleCell());
        auto with = [&](std::size_t offset, std::uint8_t value) {
            std::vector<std::uint8_t> bytes = good;
            bytes[offset] = value;
            CellMap out;
            return parseCell(bytes, out);
        };
        EXPECT_EQ(with(0, 'X').mField, "magic");
        EXPECT_EQ(with(8, 2).mField, "layoutVersion");
        EXPECT_EQ(with(20, 4).mField, "side");
        EXPECT_EQ(with(21, 8).mField, "side"); // 8 + 2048 texels
        EXPECT_EQ(with(32, 4).mField, "flags"); // an unknown bit
        EXPECT_EQ(with(40, 1).mField, "reserved");
        EXPECT_EQ(with(37, 1).mField, "basinCount");
        // Floats: NaN spacing.
        std::vector<std::uint8_t> bytes = good;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        std::memcpy(&bytes[24], &nan, 4);
        CellMap out;
        EXPECT_EQ(parseCell(bytes, out).mField, "spacing");
        bytes = good;
        std::memcpy(&bytes[28], &nan, 4);
        EXPECT_EQ(parseCell(bytes, out).mField, "heightBase");
        bytes = good;
        const float zero = 0.f;
        std::memcpy(&bytes[24], &zero, 4);
        EXPECT_EQ(parseCell(bytes, out).mField, "spacing");
    }

    TEST(PuddleMap, inconsistent_channels_are_refused)
    {
        const std::vector<std::uint8_t> good = bytesOf(sampleCell());
        const std::size_t n = 64;
        const std::size_t capacity = 108;
        const std::size_t basinMax = capacity + n;
        const std::size_t flags = capacity + 3 * n;
        const std::size_t ids = flags + n + 2 * n;
        CellMap out;
        {
            // Capacity on a texel that is in no pit.
            std::vector<std::uint8_t> bytes = good;
            bytes[capacity + 5] = 9;
            EXPECT_EQ(parseCell(bytes, out).mField, "basinId");
        }
        {
            // A pit texel with no capacity.
            std::vector<std::uint8_t> bytes = good;
            bytes[capacity + 40] = 0;
            EXPECT_EQ(parseCell(bytes, out).mField, "capacity");
        }
        {
            // The pit maximum below the texel's capacity.
            std::vector<std::uint8_t> bytes = good;
            bytes[basinMax + 40] = 19;
            EXPECT_EQ(parseCell(bytes, out).mField, "basinMax");
        }
        {
            // Unknown texel flag bits.
            std::vector<std::uint8_t> bytes = good;
            bytes[flags + 3] = 0x20;
            EXPECT_EQ(parseCell(bytes, out).mField, "texelFlags");
        }
        {
            // A pit id beyond the count.
            std::vector<std::uint8_t> bytes = good;
            bytes[ids + 2 * 40] = 5;
            EXPECT_EQ(parseCell(bytes, out).mField, "basinId");
        }
        {
            // Ids must be numbered in order of first use: swap the two.
            std::vector<std::uint8_t> bytes = good;
            bytes[ids + 2 * 10] = 1;
            bytes[ids + 2 * 11] = 1;
            bytes[ids + 2 * 40] = 0;
            EXPECT_EQ(parseCell(bytes, out).mField, "basinId");
        }
        {
            // Basin table reserved bytes.
            std::vector<std::uint8_t> bytes = good;
            bytes[bytes.size() - 1] = 1;
            EXPECT_EQ(parseCell(bytes, out).mField, "reserved");
        }
    }

    TEST(PuddleMap, the_writer_refuses_an_inconsistent_cell)
    {
        std::vector<std::uint8_t> out;
        {
            CellMap c = sampleCell();
            c.mCapacity.pop_back();
            EXPECT_TRUE(writeCell(c, out));
            EXPECT_TRUE(out.empty());
        }
        {
            CellMap c = sampleCell();
            c.mBasins.push_back(BasinInfo{ 1, 1, 1 }); // a pit with no texel
            EXPECT_TRUE(writeCell(c, out));
        }
        {
            CellMap c = sampleCell();
            c.mFlags = 8;
            EXPECT_EQ(writeCell(c, out).mField, "flags");
        }
        {
            CellMap c = sampleCell(8, false);
            c.mBasins.push_back(BasinInfo{});
            EXPECT_TRUE(writeCell(c, out));
        }
        {
            CellMap c = sampleCell();
            c.mSpacing = std::numeric_limits<float>::infinity();
            EXPECT_EQ(writeCell(c, out).mField, "spacing");
        }
        {
            CellMap c = sampleCell();
            c.mSide = 7;
            EXPECT_EQ(writeCell(c, out).mField, "side");
        }
    }

    TEST(PuddleMap, a_mutated_payload_is_refused_or_is_canonical)
    {
        // Deterministic fuzz: flip bytes of a valid payload; whatever parses must write back to the same bytes.
        const std::vector<std::uint8_t> good = bytesOf(sampleCell());
        std::uint64_t state = 0x9E3779B97F4A7C15ull;
        auto next = [&state]() {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            return state;
        };
        std::size_t accepted = 0;
        for (int round = 0; round < 40000; ++round)
        {
            std::vector<std::uint8_t> bytes = good;
            const int flips = 1 + static_cast<int>(next() % 3);
            for (int f = 0; f < flips; ++f)
                bytes[next() % bytes.size()] ^= static_cast<std::uint8_t>(1u << (next() % 8));
            CellMap parsed;
            if (parseCell(bytes, parsed))
                continue;
            ++accepted;
            std::vector<std::uint8_t> again;
            ASSERT_FALSE(writeCell(parsed, again));
            ASSERT_EQ(again, bytes);
        }
        EXPECT_GT(accepted, 0u);
    }

    TEST(PuddleMap, hostile_headers_never_allocate_beyond_the_bytes_given)
    {
        // A header claiming the largest cell with the smallest payload: refused by the size check before any channel.
        std::vector<std::uint8_t> bytes = bytesOf(sampleCell());
        bytes[20] = 0x00;
        bytes[21] = 0x04; // side 1024
        bytes[36] = 0xFE;
        bytes[37] = 0xFF; // 65534 basins
        CellMap out;
        EXPECT_TRUE(parseCell(bytes, out));
        EXPECT_TRUE(out.mCapacity.empty());
    }

    TEST(PuddleMap, cell_keys_round_trip_and_reject_the_unrepresentable)
    {
        for (const auto& [x, y] : std::vector<std::pair<int, int>>{
                 { 0, 0 }, { -1, -1 }, { -17, 23 }, { 2097151, -2097152 }, { -2097152, 2097151 } })
        {
            std::uint64_t key = 0;
            ASSERT_TRUE(packCellKey(x, y, key));
            EXPECT_EQ(key >> 56, 0x14u);
            std::int32_t rx = 99;
            std::int32_t ry = 99;
            ASSERT_TRUE(unpackCellKey(key, rx, ry));
            EXPECT_EQ(rx, x);
            EXPECT_EQ(ry, y);
        }
        std::uint64_t key = 0;
        EXPECT_FALSE(packCellKey(2097152, 0, key));
        EXPECT_FALSE(packCellKey(0, -2097153, key));
        std::int32_t x = 0;
        std::int32_t y = 0;
        EXPECT_FALSE(unpackCellKey(0x1000000000000000ull, x, y)); // another tag
        EXPECT_FALSE(unpackCellKey((0x14ull << 56) | (1ull << 50), x, y)); // bits that must be zero
    }

    TEST(PuddleMap, keys_of_different_cells_differ_and_sort_by_x_then_y)
    {
        std::uint64_t a = 0;
        std::uint64_t b = 0;
        std::uint64_t c = 0;
        ASSERT_TRUE(packCellKey(1, 5, a));
        ASSERT_TRUE(packCellKey(1, 6, b));
        ASSERT_TRUE(packCellKey(2, 0, c));
        EXPECT_LT(a, b);
        EXPECT_LT(b, c);
    }
}
