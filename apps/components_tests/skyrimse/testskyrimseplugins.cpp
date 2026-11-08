// Task 0086 stage c: generated Skyrim SE plugins. Header, masters, flags, a compressed record, a localised-string
// record, and truncated variants that must fail with located errors. No game data.
#include <game_skyrimse/pack_core.hpp>

#include <gtest/gtest.h>

#include <zlib.h>

#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    struct Captured
    {
        std::uint32_t mType = 0;
        std::uint32_t mFlags = 0;
        std::uint32_t mUncompressedSize = 0;
        std::uint64_t mOffset = 0;
        std::vector<std::byte> mData;
    };

    class Collect : public Content::Visitor
    {
    public:
        bool onRecord(const Content::RawRecord& record) override
        {
            Captured c;
            c.mType = record.mType;
            c.mFlags = record.mFlags;
            c.mUncompressedSize = record.mUncompressedSize;
            c.mOffset = record.mOffset;
            c.mData.assign(record.mData.begin(), record.mData.end());
            mRecords.push_back(std::move(c));
            return true;
        }
        std::vector<Captured> mRecords;
    };

    std::string zlibOf(const std::string& plain)
    {
        uLongf size = compressBound(static_cast<uLong>(plain.size()));
        std::string out(size, '\0');
        EXPECT_EQ(compress2(reinterpret_cast<Bytef*>(out.data()), &size, reinterpret_cast<const Bytef*>(plain.data()),
                      static_cast<uLong>(plain.size()), 6),
            Z_OK);
        out.resize(size);
        return out;
    }

    // A compressed record body: u32 inflated size, then the zlib stream.
    Bytes compressedBody(const std::string& plain)
    {
        Bytes body;
        body.u32(static_cast<std::uint32_t>(plain.size())).text(zlibOf(plain));
        return body;
    }

    std::string weaponSubrecords()
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("IronSword")));
        subs.append(sub4("DATA", Bytes().u32(25).f32(9.0f)));
        return subs.str();
    }

    Bytes headerBytes(std::uint32_t flags = 0, const std::vector<Master>& masters = { { "Skyrim.esm", 0 } })
    {
        return tes4Header(1.7f, flags, "Test Author", "A test plugin", 2, masters, 24, 44);
    }

    std::unique_ptr<Content::Plugin> open(const Bytes& bytes, Content::Error& error)
    {
        return Content::Plugin::open(
            std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
    }

    TEST(SkyrimSePlugins, headerMastersAndFlagsAreRead)
    {
        const Bytes bytes = headerBytes(Content::sRecMaster | Content::sRecLocalized | Content::sRecLight,
            { { "Skyrim.esm", 0 }, { "Update.esm", 0 } });
        Content::Error error;
        const auto plugin = open(bytes, error);
        ASSERT_NE(plugin, nullptr) << error.describe();
        const Content::Header& h = plugin->header();
        EXPECT_TRUE(isSkyrimSeHeader(h));
        EXPECT_TRUE(h.isMaster());
        EXPECT_TRUE(h.isLocalized());
        EXPECT_TRUE(h.isLight());
        EXPECT_EQ(h.mFormVersion, 44u);
        EXPECT_EQ(h.mAuthor, "Test Author");
        ASSERT_EQ(h.mMasters.size(), 2u);
        EXPECT_EQ(h.mMasters[0].mName, "Skyrim.esm");
        EXPECT_EQ(h.mMasters[1].mName, "Update.esm");
    }

    TEST(SkyrimSePlugins, aCompressedRecordInflatesToItsOriginalBytes)
    {
        const std::string plain = weaponSubrecords();
        Bytes bytes = headerBytes();
        bytes.append(group4(
            Content::fourCC("WEAP"), 0, record4("WEAP", Content::sRecCompressed, 0x1234, compressedBody(plain))));
        Content::Error error;
        const auto plugin = open(bytes, error);
        ASSERT_NE(plugin, nullptr) << error.describe();
        Collect collect;
        ASSERT_FALSE(plugin->walk(collect).has_value());
        ASSERT_EQ(collect.mRecords.size(), 1u);
        EXPECT_EQ(collect.mRecords[0].mUncompressedSize, plain.size());

        Content::RawRecord record;
        record.mType = collect.mRecords[0].mType;
        record.mFlags = collect.mRecords[0].mFlags;
        record.mOffset = collect.mRecords[0].mOffset;
        record.mUncompressedSize = collect.mRecords[0].mUncompressedSize;
        record.mDataSize = collect.mRecords[0].mData.size();
        record.mData = collect.mRecords[0].mData;
        std::vector<std::byte> out;
        ASSERT_TRUE(inflateRecord(record, "test.esp", out, error)) << error.describe();
        ASSERT_EQ(out.size(), plain.size());
        EXPECT_EQ(std::memcmp(out.data(), plain.data(), plain.size()), 0);
    }

    Content::RawRecord recordWith(const std::vector<std::byte>& data, std::uint32_t declaredSize)
    {
        Content::RawRecord record;
        record.mType = Content::fourCC("WEAP");
        record.mFlags = Content::sRecCompressed;
        record.mOffset = 0x40;
        record.mUncompressedSize = declaredSize;
        record.mDataSize = data.size();
        record.mData = data;
        return record;
    }

    std::vector<std::byte> toBytes(const Bytes& b)
    {
        std::vector<std::byte> out(b.size());
        std::memcpy(out.data(), b.str().data(), b.size());
        return out;
    }

    TEST(SkyrimSePlugins, aDamagedCompressedRecordFailsWithALocatedError)
    {
        const std::string plain = weaponSubrecords();
        Bytes body = compressedBody(plain);
        std::string damaged = body.str();
        damaged[8] = static_cast<char>(damaged[8] ^ 0x5A);
        damaged[9] = static_cast<char>(damaged[9] ^ 0xA5);
        Bytes b;
        b.text(damaged);
        std::vector<std::byte> out;
        Content::Error error;
        EXPECT_FALSE(
            inflateRecord(recordWith(toBytes(b), static_cast<std::uint32_t>(plain.size())), "test.esp", out, error));
        EXPECT_EQ(error.mFile, "test.esp");
        EXPECT_EQ(error.mOffset, 0x40u);
        EXPECT_EQ(error.mRecordType, Content::fourCC("WEAP"));
        EXPECT_FALSE(error.mMessage.empty());
    }

    TEST(SkyrimSePlugins, aCompressedRecordWhoseSizesDisagreeOrIsCutShortFails)
    {
        const std::string plain = weaponSubrecords();
        std::vector<std::byte> out;
        Content::Error error;
        // the prefix says one thing, the record header another
        EXPECT_FALSE(inflateRecord(recordWith(toBytes(compressedBody(plain)), 5), "test.esp", out, error));
        EXPECT_NE(error.mMessage.find("size"), std::string::npos) << error.describe();
        // no room for the prefix
        Bytes tiny;
        tiny.u8(1).u8(2);
        EXPECT_FALSE(inflateRecord(recordWith(toBytes(tiny), 4), "test.esp", out, error));
        EXPECT_EQ(error.mFile, "test.esp");
        // a stream cut in half
        Bytes full = compressedBody(plain);
        std::string half = full.str().substr(0, full.size() / 2);
        Bytes cut;
        cut.text(half);
        EXPECT_FALSE(
            inflateRecord(recordWith(toBytes(cut), static_cast<std::uint32_t>(plain.size())), "test.esp", out, error));
        EXPECT_EQ(error.mOffset, 0x40u);
    }

    TEST(SkyrimSePlugins, aTinyRecordClaimingHugeInflatedSizeIsRefusedBeforeAnythingIsAllocated)
    {
        // Review 4, finding 3: zlib cannot expand by more than 1032 to 1, so 20 bytes cannot hold 500 MiB.
        Bytes b;
        b.u32(500u * 1024 * 1024);
        for (int i = 0; i < 20; ++i)
            b.u8(0x55);
        std::vector<std::byte> out;
        Content::Error error;
        EXPECT_FALSE(inflateRecord(recordWith(toBytes(b), 500u * 1024 * 1024), "test.esp", out, error));
        EXPECT_EQ(error.mOffset, 0x40u);
        EXPECT_LT(out.capacity(), 1024u * 1024) << "the output buffer was sized from the claim";
    }

    TEST(SkyrimSePlugins, aCompressedRecordOfZeroBytesInflatesToNothing)
    {
        Bytes b;
        b.u32(0);
        b.text(zlibOf(""));
        std::vector<std::byte> out{ std::byte{ 1 } };
        Content::Error error;
        EXPECT_TRUE(inflateRecord(recordWith(toBytes(b), 0), "test.esp", out, error)) << error.describe();
        EXPECT_TRUE(out.empty());
    }

    TEST(SkyrimSePlugins, aRecordClaimingZeroBytesButHoldingDataFails)
    {
        const std::string plain = weaponSubrecords();
        Bytes body = compressedBody(plain);
        std::string text = body.str();
        text.replace(0, 4, std::string(4, '\0'));
        Bytes b;
        b.text(text);
        std::vector<std::byte> out;
        Content::Error error;
        EXPECT_FALSE(inflateRecord(recordWith(toBytes(b), 0), "test.esp", out, error));
        EXPECT_FALSE(error.mMessage.empty());
    }

    TEST(SkyrimSePlugins, aLocalisedStringRecordHoldsAnIdNotText)
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("Sword")));
        subs.append(sub4("FULL", Bytes().u32(0x00012345)));
        Bytes bytes = headerBytes(Content::sRecLocalized);
        bytes.append(group4(Content::fourCC("WEAP"), 0, record4("WEAP", 0, 0x77, subs)));
        Content::Error error;
        const auto plugin = open(bytes, error);
        ASSERT_NE(plugin, nullptr) << error.describe();
        ASSERT_TRUE(plugin->header().isLocalized());
        Collect collect;
        ASSERT_FALSE(plugin->walk(collect).has_value());
        ASSERT_EQ(collect.mRecords.size(), 1u);
        std::uint32_t id = 0;
        EXPECT_TRUE(readLocalizedStringId(collect.mRecords[0].mData, Content::fourCC("FULL"), id));
        EXPECT_EQ(id, 0x00012345u);
        EXPECT_FALSE(readLocalizedStringId(collect.mRecords[0].mData, Content::fourCC("DESC"), id));
        // the EDID subrecord is text of 6 bytes, not an id
        EXPECT_FALSE(readLocalizedStringId(collect.mRecords[0].mData, Content::fourCC("EDID"), id));
        // data cut in the middle of a subrecord
        auto cut = collect.mRecords[0].mData;
        cut.resize(cut.size() - 2);
        EXPECT_FALSE(readLocalizedStringId(cut, Content::fourCC("FULL"), id));
    }

    TEST(SkyrimSePlugins, everyTruncationOfAPluginFailsOrScansWithoutCrashing)
    {
        Bytes bytes = headerBytes();
        bytes.append(group4(Content::fourCC("WEAP"), 0,
            record4("WEAP", Content::sRecCompressed, 0x1234, compressedBody(weaponSubrecords()))));
        const std::string whole = bytes.str();
        std::size_t failures = 0;
        for (std::size_t length = 0; length < whole.size(); ++length)
        {
            RecordCensus census;
            Content::Header header;
            Content::Error error;
            if (!scanPluginBytes(whole.substr(0, length), "cut.esp", census, header, error))
            {
                ++failures;
                EXPECT_EQ(error.mFile, "cut.esp") << "length " << length;
                EXPECT_FALSE(error.mMessage.empty()) << "length " << length;
            }
        }
        // every cut inside the header or inside a record's bytes is an error; a cut exactly between records is not
        EXPECT_GT(failures, whole.size() / 2);
        RecordCensus census;
        Content::Header header;
        Content::Error error;
        EXPECT_TRUE(scanPluginBytes(whole, "cut.esp", census, header, error)) << error.describe();
    }
}
