#include <gtest/gtest.h>

#include <components/contentsource/plugin.hpp>
#include <components/contentsource/probe.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm4/reader.hpp>
#include <components/toutf8/toutf8.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "pluginbuilder.hpp"

// Task 0083, layer 1: header, masters, flags, raw records and groups of generated plugins, and what a hostile file
// gets. No game data.
namespace
{
    using namespace ContentTests;

    std::unique_ptr<Content::Plugin> openBytes(
        const Bytes& bytes, Content::Error& error, const Content::Limits& limits = Content::Limits())
    {
        return Content::Plugin::open(
            std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error, limits);
    }

    std::unique_ptr<Content::Plugin> openOk(const Bytes& bytes)
    {
        Content::Error error;
        std::unique_ptr<Content::Plugin> plugin = openBytes(bytes, error);
        EXPECT_NE(plugin, nullptr) << error.describe();
        return plugin;
    }

    struct Event
    {
        std::string mKind;
        std::string mType;
        std::uint32_t mFormId = 0;
        std::uint32_t mFlags = 0;
        std::uint64_t mOffset = 0;
        std::uint64_t mDataSize = 0;
        std::uint32_t mUncompressed = 0;
        std::uint32_t mDepth = 0;
        std::int32_t mGroupType = 0;
        std::string mId;
        std::string mBytes;

        bool operator==(const Event&) const = default;
    };

    class Recorder : public Content::Visitor
    {
    public:
        std::vector<Event> mEvents;
        std::size_t mStopAfter = 0;

        bool onGroupBegin(const Content::GroupInfo& group) override
        {
            Event e;
            e.mKind = "begin";
            e.mType = Content::fourCCToString(group.mLabel);
            e.mFormId = group.mLabel;
            e.mOffset = group.mOffset;
            e.mDataSize = group.mSize;
            e.mDepth = group.mDepth;
            e.mGroupType = group.mType;
            mEvents.push_back(e);
            return true;
        }

        bool onGroupEnd(const Content::GroupInfo& group) override
        {
            Event e;
            e.mKind = "end";
            e.mOffset = group.mOffset;
            e.mDepth = group.mDepth;
            e.mGroupType = group.mType;
            mEvents.push_back(e);
            return true;
        }

        bool onRecord(const Content::RawRecord& record) override
        {
            Event e;
            e.mKind = "record";
            e.mType = Content::fourCCToString(record.mType);
            e.mFormId = record.mFormId;
            e.mFlags = record.mFlags;
            e.mOffset = record.mOffset;
            e.mDataSize = record.mDataSize;
            e.mUncompressed = record.mUncompressedSize;
            e.mId = std::string(record.mId);
            e.mBytes.assign(reinterpret_cast<const char*>(record.mData.data()), record.mData.size());
            mEvents.push_back(e);
            return mStopAfter == 0 || mEvents.size() < mStopAfter;
        }
    };

    Bytes cellName(const std::string& name)
    {
        return sub3("NAME", Bytes().zstring(name));
    }

    // --- TES3 ---

    TEST(ContentPluginTes3, reads_header_masters_and_flags)
    {
        const Bytes file = tes3Header(1.3f, 1, "an author", "a description", 3,
            { Master{ "Morrowind.esm", 79837557 }, Master{ "Tribunal.esm", 4558811 } });
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        const Content::Header& h = plugin->header();
        EXPECT_EQ(h.mFamily, Content::Family::Tes3);
        EXPECT_EQ(h.mGame, Content::GameGuess::Morrowind);
        EXPECT_FLOAT_EQ(h.mVersion, 1.3f);
        EXPECT_EQ(h.mFlags, 1u);
        EXPECT_TRUE(h.isMaster());
        EXPECT_FALSE(h.isLight());
        EXPECT_FALSE(h.isLocalized());
        EXPECT_EQ(h.mRecordHeaderSize, 16u);
        EXPECT_EQ(h.mDeclaredRecords, 3u);
        EXPECT_EQ(h.mAuthor, "an author");
        EXPECT_EQ(h.mDescription, "a description");
        ASSERT_EQ(h.mMasters.size(), 2u);
        EXPECT_EQ(h.mMasters[0].mName, "Morrowind.esm");
        EXPECT_EQ(h.mMasters[0].mSize, 79837557u);
        EXPECT_EQ(h.mMasters[1].mName, "Tribunal.esm");
        EXPECT_EQ(h.mMasters[1].mSize, 4558811u);
        EXPECT_EQ(h.mFirstRecordOffset, file.size());
    }

    TEST(ContentPluginTes3, a_plugin_is_not_a_master)
    {
        const Bytes file = tes3Header(1.3f, 0, "", "", 0, {});
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        EXPECT_FALSE(plugin->header().isMaster());
        EXPECT_TRUE(plugin->header().mMasters.empty());
    }

    TEST(ContentPluginTes3, a_form_subrecord_before_hedr_is_skipped)
    {
        Bytes hedr;
        hedr.f32(1.3f).u32(1).fixed("a", 32).fixed("d", 256).u32(0);
        Bytes subs = sub3("FORM", Bytes().u32(1));
        subs.append(sub3("HEDR", hedr));
        const Bytes file = record3("TES3", 0, subs);
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        EXPECT_EQ(plugin->header().mAuthor, "a");
    }

    TEST(ContentPluginTes3, walks_records_in_file_order_with_ids_flags_and_bytes)
    {
        Bytes file = tes3Header(1.3f, 1, "", "", 3, {});
        const std::uint64_t first = file.size();
        const Bytes a = record3("STAT", 0x400, cellName("barrel_01").append(sub3("MODL", Bytes().zstring("b.nif"))));
        const Bytes b = record3("CELL", 0, sub3("INAM", Bytes().u32(7)));
        const Bytes c = record3("DIAL", 0x20, cellName("greeting"));
        file.append(a).append(b).append(c);

        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Recorder recorder;
        EXPECT_FALSE(plugin->walk(recorder).has_value());
        ASSERT_EQ(recorder.mEvents.size(), 3u);
        EXPECT_EQ(recorder.mEvents[0].mType, "STAT");
        EXPECT_EQ(recorder.mEvents[0].mId, "barrel_01");
        EXPECT_EQ(recorder.mEvents[0].mFlags, 0x400u);
        EXPECT_EQ(recorder.mEvents[0].mOffset, first);
        EXPECT_EQ(recorder.mEvents[0].mDataSize, a.size() - 16);
        EXPECT_EQ(recorder.mEvents[0].mBytes, a.str().substr(16));
        EXPECT_EQ(recorder.mEvents[1].mType, "CELL");
        EXPECT_EQ(recorder.mEvents[1].mId, "");
        EXPECT_EQ(recorder.mEvents[1].mOffset, first + a.size());
        EXPECT_EQ(recorder.mEvents[2].mType, "DIAL");
        EXPECT_EQ(recorder.mEvents[2].mFlags, 0x20u);
        EXPECT_EQ(recorder.mEvents[2].mId, "greeting");
    }

    TEST(ContentPluginTes3, walking_twice_gives_the_same_events)
    {
        Bytes file = tes3Header(1.3f, 1, "", "", 2, {});
        file.append(record3("STAT", 0, cellName("a"))).append(record3("STAT", 0, cellName("b")));
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Recorder first;
        Recorder second;
        EXPECT_FALSE(plugin->walk(first).has_value());
        EXPECT_FALSE(plugin->walk(second).has_value());
        EXPECT_EQ(first.mEvents, second.mEvents);
    }

    TEST(ContentPluginTes3, a_walk_without_data_reports_the_same_records_and_no_bytes)
    {
        Bytes file = tes3Header(1.3f, 1, "", "", 2, {});
        file.append(record3("STAT", 0, cellName("a"))).append(record3("NPC_", 0, cellName("b")));
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Content::WalkOptions options;
        options.mReadData = false;
        Recorder recorder;
        EXPECT_FALSE(plugin->walk(recorder, options).has_value());
        ASSERT_EQ(recorder.mEvents.size(), 2u);
        EXPECT_EQ(recorder.mEvents[0].mType, "STAT");
        EXPECT_GT(recorder.mEvents[0].mDataSize, 0u);
        EXPECT_TRUE(recorder.mEvents[0].mBytes.empty());
        EXPECT_EQ(recorder.mEvents[1].mType, "NPC_");
    }

    TEST(ContentPluginTes3, a_truncated_record_is_a_located_error)
    {
        Bytes file = tes3Header(1.3f, 1, "", "", 2, {});
        file.append(record3("STAT", 0, cellName("a")));
        const std::uint64_t badOffset = file.size();
        Bytes bad = record3("NPC_", 0, cellName("a long name for a record that is cut"));
        bad.str().resize(bad.size() - 10);
        file.append(bad);

        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Recorder recorder;
        const std::optional<Content::Error> error = plugin->walk(recorder);
        ASSERT_TRUE(error.has_value());
        EXPECT_EQ(error->mFile, "test.esp");
        EXPECT_EQ(error->mOffset, badOffset);
        EXPECT_EQ(error->mRecordType, Content::fourCC("NPC_"));
        EXPECT_NE(error->describe().find("test.esp"), std::string::npos);
        EXPECT_NE(error->describe().find("NPC_"), std::string::npos);
        EXPECT_EQ(recorder.mEvents.size(), 1u);
    }

    TEST(ContentPluginTes3, a_record_header_cut_short_is_an_error)
    {
        Bytes file = tes3Header(1.3f, 1, "", "", 1, {});
        file.text("STAT\x04");
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Recorder recorder;
        EXPECT_TRUE(plugin->walk(recorder).has_value());
    }

    TEST(ContentPluginTes3, a_record_that_claims_four_gigabytes_is_refused_without_reading)
    {
        Bytes file = tes3Header(1.3f, 1, "", "", 1, {});
        file.fourCC("STAT").u32(0xFFFFFFFFu).u32(0).u32(0);
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Recorder recorder;
        const std::optional<Content::Error> error = plugin->walk(recorder);
        ASSERT_TRUE(error.has_value());
        EXPECT_EQ(error->mRecordType, Content::fourCC("STAT"));
        EXPECT_TRUE(recorder.mEvents.empty());
    }

    TEST(ContentPluginTes3, the_size_limit_is_enforced_before_the_bytes_are_read)
    {
        Bytes file = tes3Header(1.3f, 1, "", "", 1, {});
        file.append(record3("STAT", 0, cellName(std::string(5000, 'x'))));
        Content::Error error;
        Content::Limits limits;
        limits.mMaxRecordData = 1000;
        const std::unique_ptr<Content::Plugin> plugin = openBytes(file, error, limits);
        ASSERT_NE(plugin, nullptr);
        Recorder recorder;
        const std::optional<Content::Error> walkError = plugin->walk(recorder);
        ASSERT_TRUE(walkError.has_value());
        EXPECT_NE(walkError->mMessage.find("limit"), std::string::npos);
    }

    TEST(ContentPluginTes3, refuses_bad_headers_with_an_error_value)
    {
        const Bytes good = tes3Header(1.3f, 1, "a", "d", 0, { Master{ "m.esm", 1 } });
        {
            Content::Error error;
            EXPECT_EQ(openBytes(Bytes(), error), nullptr);
            EXPECT_FALSE(error.mMessage.empty());
        }
        {
            Bytes shortFile;
            shortFile.text(good.str().substr(0, 10));
            Content::Error error;
            EXPECT_EQ(openBytes(shortFile, error), nullptr);
            EXPECT_EQ(error.mFile, "test.esp");
        }
        {
            Bytes cut;
            cut.text(good.str().substr(0, good.size() - 5));
            Content::Error error;
            EXPECT_EQ(openBytes(cut, error), nullptr);
            EXPECT_EQ(error.mRecordType, Content::fourCC("TES3"));
        }
        {
            Bytes huge;
            huge.fourCC("TES3").u32(0xFFFFFFF0u).u32(0).u32(0);
            Content::Error error;
            EXPECT_EQ(openBytes(huge, error), nullptr);
        }
        {
            Bytes wrong;
            wrong.text("ABCD0000000000000000000000000000");
            Content::Error error;
            EXPECT_EQ(openBytes(wrong, error), nullptr);
        }
    }

    TEST(ContentPluginTes3, a_subrecord_longer_than_its_record_is_an_error)
    {
        Bytes subs;
        subs.fourCC("HEDR").u32(5000).u32(0);
        const Bytes file = record3("TES3", 0, subs);
        Content::Error error;
        EXPECT_EQ(openBytes(file, error), nullptr);
        EXPECT_EQ(error.mRecordType, Content::fourCC("TES3"));
    }

    TEST(ContentPluginTes3, too_many_masters_are_refused)
    {
        std::vector<Master> masters;
        for (int i = 0; i < 20; ++i)
        {
            masters.push_back(Master{ "m" + std::to_string(i) + ".esm", 1 });
        }
        const Bytes file = tes3Header(1.3f, 1, "", "", 0, masters);
        Content::Limits limits;
        limits.mMaxMasters = 10;
        Content::Error error;
        EXPECT_EQ(openBytes(file, error, limits), nullptr);
        EXPECT_NE(error.mMessage.find("master"), std::string::npos);
    }

    TEST(ContentPluginTes3, a_header_without_hedr_is_accepted_as_ESM3_reads_it)
    {
        // ESM3::Header::load treats HEDR as optional (components/esm3/loadtes3.cpp), so a file without one still loads.
        const Bytes file = record3("TES3", 0, Bytes());
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        EXPECT_EQ(plugin->header().mFamily, Content::Family::Tes3);
        EXPECT_TRUE(plugin->header().mMasters.empty());
        EXPECT_EQ(plugin->header().mDeclaredRecords, 0u);
    }

    TEST(ContentPluginTes4, a_declared_record_count_is_not_trusted)
    {
        // HEDR says four billion records; the walk counts what is there and allocates nothing by the claimed count.
        Bytes file = tes4Header(1.7f, 1, "", "", 0xFFFFFFFFu, {});
        file.append(record4("STAT", 0, 1, Bytes().text("x")));
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        EXPECT_EQ(plugin->header().mDeclaredRecords, 0xFFFFFFFFu);
        Content::Summary summary;
        EXPECT_FALSE(Content::summarise(*plugin, summary).has_value());
        EXPECT_EQ(summary.mRecords, 1u);
    }

    TEST(ContentPluginTes3, a_declared_record_count_is_not_trusted)
    {
        Bytes file = tes3Header(1.3f, 1, "", "", 0xFFFFFFFFu, {});
        file.append(record3("STAT", 0, cellName("a")));
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Content::Summary summary;
        EXPECT_FALSE(Content::summarise(*plugin, summary).has_value());
        EXPECT_EQ(summary.mRecords, 1u);
    }

    TEST(ContentPluginTes4, a_header_without_hedr_is_accepted_with_a_24_byte_record_header)
    {
        const Bytes file = record4("TES4", 0, 0, Bytes());
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        EXPECT_EQ(plugin->header().mRecordHeaderSize, 24u);
    }

    // --- TES4 family ---

    TEST(ContentPluginTes4, reads_a_24_byte_header_with_masters_and_flags)
    {
        const Bytes file = tes4Header(1.7f, Content::sRecMaster | Content::sRecLocalized, "me", "desc", 12,
            { Master{ "Skyrim.esm", 100 }, Master{ "Update.esm", 200 } }, 24, 44);
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        const Content::Header& h = plugin->header();
        EXPECT_EQ(h.mFamily, Content::Family::Tes4);
        EXPECT_EQ(h.mRecordHeaderSize, 24u);
        EXPECT_FLOAT_EQ(h.mVersion, 1.7f);
        EXPECT_EQ(h.mFormVersion, 44u);
        EXPECT_TRUE(h.isMaster());
        EXPECT_TRUE(h.isLocalized());
        EXPECT_FALSE(h.isLight());
        EXPECT_EQ(h.mDeclaredRecords, 12u);
        EXPECT_EQ(h.mAuthor, "me");
        EXPECT_EQ(h.mDescription, "desc");
        ASSERT_EQ(h.mMasters.size(), 2u);
        EXPECT_EQ(h.mMasters[1].mName, "Update.esm");
        EXPECT_EQ(h.mMasters[1].mSize, 200u);
        EXPECT_EQ(h.mFirstRecordOffset, file.size());
    }

    TEST(ContentPluginTes4, the_light_flag_is_read)
    {
        const Bytes file = tes4Header(1.7f, Content::sRecMaster | Content::sRecLight, "", "", 0, {});
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        EXPECT_TRUE(plugin->header().isLight());
        EXPECT_TRUE(plugin->header().isMaster());
    }

    TEST(ContentPluginTes4, a_20_byte_header_is_recognised_by_where_hedr_sits)
    {
        const Bytes file = tes4Header(1.34f, 1, "", "", 5, { Master{ "FalloutNV.esm", 9 } }, 20);
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        EXPECT_EQ(plugin->header().mRecordHeaderSize, 20u);
        EXPECT_EQ(plugin->header().mFormVersion, 0u);
        EXPECT_FLOAT_EQ(plugin->header().mVersion, 1.34f);
        ASSERT_EQ(plugin->header().mMasters.size(), 1u);
    }

    TEST(ContentPluginTes4, walks_nested_groups_and_records)
    {
        Bytes file = tes4Header(1.7f, 1, "", "", 3, {});
        const std::uint64_t groupOffset = file.size();
        const Bytes npc = record4("NPC_", 0, 0x00000800, Bytes().text("abc"));
        const Bytes cell = record4("CELL", 0x400, 0x00000801, Bytes().text("xy"));
        const Bytes inner = group4(0x801, 6, record4("REFR", 0, 0x00000802, Bytes().text("r")));
        const Bytes outer = group4(Content::fourCC("CELL"), 0, Bytes().append(cell).append(inner));
        file.append(group4(Content::fourCC("NPC_"), 0, npc)).append(outer);
        (void)groupOffset;

        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Recorder recorder;
        EXPECT_FALSE(plugin->walk(recorder).has_value());
        std::vector<std::string> kinds;
        for (const Event& e : recorder.mEvents)
        {
            kinds.push_back(e.mKind + (e.mKind == "record" ? ":" + e.mType : ""));
        }
        const std::vector<std::string> expected
            = { "begin", "record:NPC_", "end", "begin", "record:CELL", "begin", "record:REFR", "end", "end" };
        EXPECT_EQ(kinds, expected);
        EXPECT_EQ(recorder.mEvents[0].mType, "NPC_");
        EXPECT_EQ(recorder.mEvents[0].mDepth, 0u);
        EXPECT_EQ(recorder.mEvents[1].mFormId, 0x800u);
        EXPECT_EQ(recorder.mEvents[1].mBytes, "abc");
        EXPECT_EQ(recorder.mEvents[4].mFlags, 0x400u);
        EXPECT_EQ(recorder.mEvents[5].mDepth, 1u);
        EXPECT_EQ(recorder.mEvents[5].mGroupType, 6);
        EXPECT_EQ(recorder.mEvents[5].mFormId, 0x801u);
        EXPECT_EQ(recorder.mEvents[6].mFormId, 0x802u);
    }

    TEST(ContentPluginTes4, an_empty_group_is_a_group_with_no_children)
    {
        Bytes file = tes4Header(1.7f, 1, "", "", 0, {});
        file.append(group4(Content::fourCC("STAT"), 0, Bytes()));
        file.append(group4(Content::fourCC("NPC_"), 0, record4("NPC_", 0, 1, Bytes())));
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Recorder recorder;
        EXPECT_FALSE(plugin->walk(recorder).has_value());
        ASSERT_EQ(recorder.mEvents.size(), 5u);
        EXPECT_EQ(recorder.mEvents[0].mKind, "begin");
        EXPECT_EQ(recorder.mEvents[1].mKind, "end");
    }

    TEST(ContentPluginTes4, a_20_byte_file_walks_with_20_byte_records_and_groups)
    {
        Bytes file = tes4Header(0.94f, 1, "", "", 1, {}, 20);
        file.append(group4(Content::fourCC("WEAP"), 0, record4("WEAP", 0, 0x10, Bytes().text("q"), 20), 20));
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Recorder recorder;
        EXPECT_FALSE(plugin->walk(recorder).has_value());
        ASSERT_EQ(recorder.mEvents.size(), 3u);
        EXPECT_EQ(recorder.mEvents[1].mType, "WEAP");
        EXPECT_EQ(recorder.mEvents[1].mBytes, "q");
    }

    TEST(ContentPluginTes4, a_compressed_record_reports_its_uncompressed_size_and_is_not_inflated)
    {
        Bytes file = tes4Header(1.7f, 1, "", "", 1, {});
        const Bytes payload = Bytes().u32(4096).text("zlibbytes");
        file.append(group4(Content::fourCC("STAT"), 0, record4("STAT", Content::sRecCompressed, 0x20, payload)));
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Recorder recorder;
        EXPECT_FALSE(plugin->walk(recorder).has_value());
        ASSERT_EQ(recorder.mEvents.size(), 3u);
        EXPECT_EQ(recorder.mEvents[1].mUncompressed, 4096u);
        EXPECT_EQ(recorder.mEvents[1].mDataSize, payload.size());
        EXPECT_EQ(recorder.mEvents[1].mBytes, payload.str());
    }

    TEST(ContentPluginTes4, a_compressed_record_that_claims_a_huge_inflated_size_is_refused)
    {
        Bytes file = tes4Header(1.7f, 1, "", "", 1, {});
        file.append(record4("STAT", Content::sRecCompressed, 0x20, Bytes().u32(0xFFFFFFFFu).text("x")));
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Recorder recorder;
        const std::optional<Content::Error> error = plugin->walk(recorder);
        ASSERT_TRUE(error.has_value());
        EXPECT_EQ(error->mRecordType, Content::fourCC("STAT"));
        EXPECT_NE(error->mMessage.find("limit"), std::string::npos);
        EXPECT_TRUE(recorder.mEvents.empty());
    }

    // zlib cannot expand data more than 1032 to 1 (task 0083, review of the upstream ESM audit, note 0051 section 4):
    // a claim beyond that is false and is refused before anything is sized from it.
    TEST(ContentPluginTes4, a_compressed_record_claiming_more_than_zlib_can_produce_is_refused)
    {
        // 2 bytes of compressed data: at most 2064 bytes can come out.
        Bytes file = tes4Header(1.7f, 1, "", "", 1, {});
        file.append(record4("STAT", Content::sRecCompressed, 0x20, Bytes().u32(2065).text("zz")));
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Recorder recorder;
        const std::optional<Content::Error> error = plugin->walk(recorder);
        ASSERT_TRUE(error.has_value());
        EXPECT_EQ(error->mRecordType, Content::fourCC("STAT"));
        EXPECT_NE(error->mMessage.find("1032"), std::string::npos);
        EXPECT_TRUE(recorder.mEvents.empty());
    }

    TEST(ContentPluginTes4, a_compressed_record_at_exactly_the_zlib_ratio_is_accepted)
    {
        Bytes file = tes4Header(1.7f, 1, "", "", 1, {});
        file.append(record4("STAT", Content::sRecCompressed, 0x20, Bytes().u32(2064).text("zz")));
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Recorder recorder;
        EXPECT_FALSE(plugin->walk(recorder).has_value());
        ASSERT_EQ(recorder.mEvents.size(), 1u);
        EXPECT_EQ(recorder.mEvents[0].mUncompressed, 2064u);
    }

    TEST(ContentPluginTes4, a_zip_bomb_record_within_the_absolute_limit_is_refused_by_the_ratio)
    {
        // 400 MiB claimed from 100 bytes: under the 512 MiB absolute cap, far over what zlib can make of 100 bytes.
        Bytes file = tes4Header(1.7f, 1, "", "", 1, {});
        file.append(record4(
            "STAT", Content::sRecCompressed, 0x20, Bytes().u32(400u * 1024u * 1024u).text(std::string(100, 'q'))));
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Recorder recorder;
        const std::optional<Content::Error> error = plugin->walk(recorder);
        ASSERT_TRUE(error.has_value());
        EXPECT_NE(error->mMessage.find("1032"), std::string::npos);
        EXPECT_TRUE(recorder.mEvents.empty());
    }

    TEST(ContentPluginTes4, a_compressed_record_with_no_data_after_the_prefix_can_only_claim_nothing)
    {
        {
            Bytes file = tes4Header(1.7f, 1, "", "", 1, {});
            file.append(record4("STAT", Content::sRecCompressed, 0x20, Bytes().u32(1)));
            const std::unique_ptr<Content::Plugin> plugin = openOk(file);
            ASSERT_NE(plugin, nullptr);
            Recorder recorder;
            EXPECT_TRUE(plugin->walk(recorder).has_value());
        }
        {
            Bytes file = tes4Header(1.7f, 1, "", "", 1, {});
            file.append(record4("STAT", Content::sRecCompressed, 0x20, Bytes().u32(0)));
            const std::unique_ptr<Content::Plugin> plugin = openOk(file);
            ASSERT_NE(plugin, nullptr);
            Recorder recorder;
            EXPECT_FALSE(plugin->walk(recorder).has_value());
        }
    }

    // The header version to game rule against upstream's getGameFormat (note 0051 section 4): 1.70 and 1.71 are
    // Skyrim SE; 0.94 is ambiguous between Fallout 3 and Skyrim LE upstream and neither is named here, so it is never
    // Skyrim SE whatever the form version says.
    TEST(ContentPluginTes4, the_ambiguous_header_version_0_94_is_never_taken_for_skyrim_se)
    {
        const auto guess = [](float version, std::uint32_t headerSize, std::uint16_t formVersion) {
            const std::unique_ptr<Content::Plugin> plugin
                = openOk(tes4Header(version, 1, "", "", 0, {}, headerSize, formVersion));
            return plugin != nullptr ? plugin->header().mGame : Content::GameGuess::Unknown;
        };
        EXPECT_EQ(guess(0.94f, 24, 43), Content::GameGuess::Unknown);
        EXPECT_EQ(guess(0.94f, 24, 40), Content::GameGuess::Unknown);
        EXPECT_EQ(guess(0.94f, 24, 15), Content::GameGuess::Unknown);
        EXPECT_EQ(guess(0.94f, 20, 0), Content::GameGuess::Unknown);
        EXPECT_EQ(guess(1.7f, 24, 43), Content::GameGuess::SkyrimSE);
        EXPECT_EQ(guess(1.71f, 24, 44), Content::GameGuess::SkyrimSE);
    }

    TEST(ContentPluginTes4, a_compressed_record_too_short_for_its_size_prefix_is_an_error)
    {
        Bytes file = tes4Header(1.7f, 1, "", "", 1, {});
        file.append(record4("STAT", Content::sRecCompressed, 0x20, Bytes().u8(1).u8(2)));
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Recorder recorder;
        EXPECT_TRUE(plugin->walk(recorder).has_value());
    }

    TEST(ContentPluginTes4, a_truncated_record_is_a_located_error)
    {
        Bytes file = tes4Header(1.7f, 1, "", "", 1, {});
        const std::uint64_t offset = file.size();
        Bytes record = record4("NPC_", 0, 0x800, Bytes().text("0123456789"));
        record.str().resize(record.size() - 4);
        file.append(record);
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Recorder recorder;
        const std::optional<Content::Error> error = plugin->walk(recorder);
        ASSERT_TRUE(error.has_value());
        EXPECT_EQ(error->mOffset, offset);
        EXPECT_EQ(error->mRecordType, Content::fourCC("NPC_"));
    }

    TEST(ContentPluginTes4, a_record_larger_than_its_group_is_an_error)
    {
        Bytes file = tes4Header(1.7f, 1, "", "", 1, {});
        Bytes record = record4("NPC_", 0, 0x800, Bytes().text("0123456789"));
        Bytes group = group4(Content::fourCC("NPC_"), 0, record);
        group.str().resize(group.size() - 6);
        // The group claims its size minus six bytes, so the record inside overruns it while the file continues.
        Bytes fixed = group;
        std::uint32_t size = static_cast<std::uint32_t>(fixed.size());
        std::memcpy(&fixed.str()[4], &size, 4);
        fixed.text("123456");
        file.append(fixed);
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Recorder recorder;
        const std::optional<Content::Error> error = plugin->walk(recorder);
        ASSERT_TRUE(error.has_value());
        EXPECT_EQ(error->mRecordType, Content::fourCC("NPC_"));
    }

    TEST(ContentPluginTes4, a_group_larger_than_its_parent_or_the_file_is_an_error)
    {
        Bytes file = tes4Header(1.7f, 1, "", "", 1, {});
        Bytes group = group4(Content::fourCC("NPC_"), 0, record4("NPC_", 0, 1, Bytes()));
        const std::uint32_t big = 0x7FFFFFF0u;
        std::memcpy(&group.str()[4], &big, 4);
        file.append(group);
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Recorder recorder;
        const std::optional<Content::Error> error = plugin->walk(recorder);
        ASSERT_TRUE(error.has_value());
        EXPECT_EQ(error->mRecordType, Content::fourCC("GRUP"));
    }

    TEST(ContentPluginTes4, a_group_smaller_than_its_own_header_is_an_error)
    {
        Bytes file = tes4Header(1.7f, 1, "", "", 0, {});
        Bytes group = group4(Content::fourCC("NPC_"), 0, Bytes());
        const std::uint32_t tiny = 8;
        std::memcpy(&group.str()[4], &tiny, 4);
        file.append(group);
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Recorder recorder;
        EXPECT_TRUE(plugin->walk(recorder).has_value());
    }

    TEST(ContentPluginTes4, groups_nested_beyond_the_limit_are_refused)
    {
        Bytes inner = record4("STAT", 0, 1, Bytes());
        for (int i = 0; i < 12; ++i)
        {
            inner = group4(static_cast<std::uint32_t>(i), 6, inner);
        }
        Bytes file = tes4Header(1.7f, 1, "", "", 0, {});
        file.append(inner);
        Content::Limits limits;
        limits.mMaxGroupDepth = 5;
        Content::Error error;
        const std::unique_ptr<Content::Plugin> plugin = openBytes(file, error, limits);
        ASSERT_NE(plugin, nullptr);
        Recorder recorder;
        const std::optional<Content::Error> walkError = plugin->walk(recorder);
        ASSERT_TRUE(walkError.has_value());
        EXPECT_NE(walkError->mMessage.find("deep"), std::string::npos);
    }

    TEST(ContentPluginTes4, the_visitor_can_stop_the_walk)
    {
        Bytes file = tes4Header(1.7f, 1, "", "", 3, {});
        file.append(record4("GMST", 0, 1, Bytes()))
            .append(record4("GMST", 0, 2, Bytes()))
            .append(record4("GMST", 0, 3, Bytes()));
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Recorder recorder;
        recorder.mStopAfter = 2;
        EXPECT_FALSE(plugin->walk(recorder).has_value());
        EXPECT_EQ(recorder.mEvents.size(), 2u);
    }

    TEST(ContentPluginTes4, summarise_counts_records_groups_compressed_and_deleted)
    {
        Bytes file = tes4Header(1.7f, 1, "", "", 4, {});
        Bytes children;
        children.append(record4("STAT", 0, 1, Bytes()));
        children.append(record4("STAT", 0x20, 2, Bytes()));
        children.append(record4("STAT", Content::sRecCompressed, 3, Bytes().u32(10).u8(1)));
        file.append(group4(Content::fourCC("STAT"), 0, children));
        file.append(group4(Content::fourCC("NPC_"), 0, record4("NPC_", 0, 4, Bytes())));
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Content::Summary summary;
        EXPECT_FALSE(Content::summarise(*plugin, summary).has_value());
        EXPECT_EQ(summary.mRecords, 4u);
        EXPECT_EQ(summary.mGroups, 2u);
        EXPECT_EQ(summary.mCompressed, 1u);
        EXPECT_EQ(summary.mDeleted, 1u);
        EXPECT_EQ(summary.mRecordsByType.at(Content::fourCC("STAT")), 3u);
        EXPECT_EQ(summary.mRecordsByType.at(Content::fourCC("NPC_")), 1u);
        EXPECT_EQ(summary.mGroupsByType.at(0), 2u);
    }

    TEST(ContentPluginTes4, a_record_nobody_decodes_is_still_a_record)
    {
        Bytes file = tes4Header(1.7f, 1, "", "", 1, {});
        file.append(record4("ZZZZ", 0, 9, Bytes().text("opaque")));
        const std::unique_ptr<Content::Plugin> plugin = openOk(file);
        ASSERT_NE(plugin, nullptr);
        Recorder recorder;
        EXPECT_FALSE(plugin->walk(recorder).has_value());
        ASSERT_EQ(recorder.mEvents.size(), 1u);
        EXPECT_EQ(recorder.mEvents[0].mType, "ZZZZ");
    }

    TEST(ContentPluginTes4, guesses_the_game_only_from_measured_header_facts)
    {
        const auto guess = [](float version, std::uint32_t headerSize, std::uint16_t formVersion) {
            const std::unique_ptr<Content::Plugin> plugin
                = openOk(tes4Header(version, 1, "", "", 0, {}, headerSize, formVersion));
            return plugin != nullptr ? plugin->header().mGame : Content::GameGuess::Unknown;
        };
        EXPECT_EQ(guess(1.7f, 24, 44), Content::GameGuess::SkyrimSE);
        EXPECT_EQ(guess(1.71f, 24, 44), Content::GameGuess::SkyrimSE);
        EXPECT_EQ(guess(1.7f, 24, 43), Content::GameGuess::SkyrimSE);
        EXPECT_EQ(guess(0.95f, 24, 131), Content::GameGuess::Fallout4);
        EXPECT_EQ(guess(1.0f, 24, 131), Content::GameGuess::Fallout4);
        EXPECT_EQ(guess(1.34f, 24, 15), Content::GameGuess::FalloutNV);
        EXPECT_EQ(guess(1.34f, 24, 2), Content::GameGuess::FalloutNV);
        EXPECT_EQ(guess(0.8f, 20, 0), Content::GameGuess::Unknown);
        EXPECT_EQ(guess(1.7f, 24, 40), Content::GameGuess::Unknown);
        EXPECT_EQ(guess(1.7f, 20, 44), Content::GameGuess::Unknown);
    }

    // --- hostile input ---

    TEST(ContentPluginFuzz, mutated_plugins_never_throw_or_overrun)
    {
        Bytes tes3 = tes3Header(1.3f, 1, "a", "d", 2, { Master{ "Morrowind.esm", 1 } });
        tes3.append(record3("STAT", 0, cellName("one"))).append(record3("NPC_", 0x20, cellName("two")));
        Bytes tes4 = tes4Header(1.7f, 1, "a", "d", 3, { Master{ "Skyrim.esm", 1 } });
        tes4.append(group4(Content::fourCC("STAT"), 0,
            Bytes()
                .append(record4("STAT", 0, 1, Bytes().text("abcd")))
                .append(record4("STAT", Content::sRecCompressed, 2, Bytes().u32(100).text("zz")))
                .append(group4(5, 6, record4("REFR", 0, 3, Bytes().text("ref"))))));

        Content::Limits limits;
        limits.mMaxHeaderRecord = 1 << 20;
        limits.mMaxRecordData = 1 << 20;
        limits.mMaxUncompressed = 1 << 20;
        std::uint64_t state = 0x9E3779B97F4A7C15ull;
        const auto next = [&state]() {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            return state;
        };
        std::size_t opened = 0;
        std::size_t walked = 0;
        for (const Bytes* original : { &tes3, &tes4 })
        {
            for (int iteration = 0; iteration < 4000; ++iteration)
            {
                Bytes mutated = *original;
                const int changes = 1 + static_cast<int>(next() % 4);
                for (int c = 0; c < changes; ++c)
                {
                    const std::size_t at = static_cast<std::size_t>(next() % mutated.size());
                    switch (next() % 3)
                    {
                        case 0:
                            mutated.str()[at] = static_cast<char>(next());
                            break;
                        case 1:
                            mutated.str()[at] = static_cast<char>(0xFF);
                            break;
                        default:
                            mutated.str().resize(at + 1);
                            break;
                    }
                }
                Content::Error error;
                std::unique_ptr<Content::Plugin> plugin;
                ASSERT_NO_THROW(plugin = openBytes(mutated, error, limits));
                if (plugin == nullptr)
                {
                    EXPECT_FALSE(error.mMessage.empty());
                    continue;
                }
                ++opened;
                Recorder recorder;
                std::optional<Content::Error> walkError;
                ASSERT_NO_THROW(walkError = plugin->walk(recorder));
                ++walked;
                for (const Event& e : recorder.mEvents)
                {
                    EXPECT_LE(e.mBytes.size(), mutated.size());
                }
                if (walkError.has_value())
                {
                    EXPECT_FALSE(walkError->mMessage.empty());
                    EXPECT_LE(walkError->mOffset, mutated.size());
                }
            }
        }
        EXPECT_GT(opened, 1000u);
        EXPECT_GT(walked, 1000u);
    }

    // Task 0083 stage 5: the launcher's content list reads author, description, version and masters from
    // Content::probe instead of the typed readers. The typed readers stay here as the oracle.
    Content::PluginInfo probeBytes(const std::string& bytes, const Content::TextConverter& convert)
    {
        Content::Error error;
        const std::unique_ptr<Content::Plugin> plugin
            = Content::Plugin::open(std::make_unique<std::istringstream>(bytes, std::ios::binary), "probe.esp", error);
        EXPECT_NE(plugin, nullptr) << error.describe();
        if (plugin == nullptr)
            return {};
        return Content::probe(plugin->header(), convert);
    }

    void expectSameAsTypedReader(const std::string& bytes, bool tes3);

    // win1252 bytes of e acute, u umlaut, euro and trade mark, which the converter must turn into UTF-8
    const std::string sAccents = "Caf\xe9 \xfc \x80 \x99";

    TEST(ContentProbe, copies_the_header_facts_of_a_tes3_plugin)
    {
        const std::string bytes
            = tes3Header(1.2f, 0, "Someone", "A description", 0, { { "A.esm", 5 }, { "B.esm", 7 } }).str();
        const Content::PluginInfo info = probeBytes(bytes, {});
        EXPECT_EQ(info.mFamily, Content::Family::Tes3);
        EXPECT_FLOAT_EQ(info.mVersion, 1.2f);
        EXPECT_EQ(info.mAuthor, "Someone");
        EXPECT_EQ(info.mDescription, "A description");
        EXPECT_EQ(info.mMasters, (std::vector<std::string>{ "A.esm", "B.esm" }));
    }

    TEST(ContentProbe, copies_the_header_facts_of_a_tes4_plugin)
    {
        const std::string bytes
            = tes4Header(1.7f, 0, "Modder", "A mod", 0, { { "Skyrim.esm", 0 }, { "X.esl", 0 } }).str();
        const Content::PluginInfo info = probeBytes(bytes, {});
        EXPECT_EQ(info.mFamily, Content::Family::Tes4);
        EXPECT_FLOAT_EQ(info.mVersion, 1.7f);
        EXPECT_EQ(info.mAuthor, "Modder");
        EXPECT_EQ(info.mDescription, "A mod");
        EXPECT_EQ(info.mMasters, (std::vector<std::string>{ "Skyrim.esm", "X.esl" }));
    }

    TEST(ContentProbe, a_plugin_with_no_masters_lists_none)
    {
        EXPECT_TRUE(probeBytes(tes3Header(1.3f, 1, "a", "d", 0, {}).str(), {}).mMasters.empty());
        EXPECT_TRUE(probeBytes(tes4Header(1.7f, 0, "a", "d", 0, {}).str(), {}).mMasters.empty());
    }

    TEST(ContentProbe, the_converter_is_applied_to_author_description_and_every_master)
    {
        const std::string bytes = tes3Header(1.3f, 0, "au", "de", 0, { { "m1", 1 }, { "m2", 2 } }).str();
        const Content::PluginInfo info
            = probeBytes(bytes, [](std::string_view text) { return "<" + std::string(text) + ">"; });
        EXPECT_EQ(info.mAuthor, "<au>");
        EXPECT_EQ(info.mDescription, "<de>");
        EXPECT_EQ(info.mMasters, (std::vector<std::string>{ "<m1>", "<m2>" }));
    }

    TEST(ContentProbe, without_a_converter_the_text_stays_as_stored)
    {
        const Content::PluginInfo info = probeBytes(tes3Header(1.3f, 0, sAccents, "d", 0, {}).str(), {});
        EXPECT_EQ(info.mAuthor, sAccents);
    }

    TEST(ContentProbe, a_tes3_header_without_hedr_gives_empty_text_and_the_typed_readers_default_version)
    {
        Bytes subs = sub3("MAST", Bytes().zstring("Only.esm"));
        subs.append(sub3("DATA", Bytes().u64(1)));
        const Content::PluginInfo info = probeBytes(record3("TES3", 0, subs).str(), {});
        EXPECT_EQ(info.mAuthor, "");
        EXPECT_EQ(info.mDescription, "");
        // ESM3::Header::clear sets version 1.3 (VER_130, components/esm3/loadtes3.cpp) and HEDR may be missing
        EXPECT_FLOAT_EQ(info.mVersion, 1.3f);
        EXPECT_EQ(info.mMasters, (std::vector<std::string>{ "Only.esm" }));
        expectSameAsTypedReader(record3("TES3", 0, subs).str(), true);
    }

    struct ExpectedFromTypedReader
    {
        float mVersion = 0;
        std::string mAuthor, mDescription;
        std::vector<std::string> mMasters;
    };

    ExpectedFromTypedReader typedTes3(const std::string& bytes, ToUTF8::FromType encoding)
    {
        ToUTF8::Utf8Encoder encoder(encoding);
        ESM::ESMReader reader;
        reader.setEncoder(&encoder);
        reader.open(std::make_unique<std::istringstream>(bytes, std::ios::binary), std::filesystem::path("oracle.esp"));
        ExpectedFromTypedReader out;
        out.mVersion = reader.esmVersionF();
        out.mAuthor = reader.getAuthor();
        out.mDescription = reader.getDesc();
        for (const auto& master : reader.getGameFiles())
            out.mMasters.push_back(master.name);
        return out;
    }

    ExpectedFromTypedReader typedTes4(const std::string& bytes, ToUTF8::FromType encoding)
    {
        ToUTF8::StatelessUtf8Encoder encoder(encoding);
        ESM4::Reader reader(std::make_unique<std::istringstream>(bytes, std::ios::binary),
            std::filesystem::path("oracle.esp"), nullptr, &encoder, true);
        ExpectedFromTypedReader out;
        out.mVersion = reader.esmVersionF();
        out.mAuthor = reader.getAuthor();
        out.mDescription = reader.getDesc();
        for (const auto& master : reader.getGameFiles())
            out.mMasters.push_back(master.name);
        return out;
    }

    Content::TextConverter converterFor(ToUTF8::FromType encoding)
    {
        auto encoder = std::make_shared<ToUTF8::Utf8Encoder>(encoding);
        return [encoder](std::string_view stored) { return std::string(encoder->getUtf8(stored)); };
    }

    void expectSameAsTypedReader(const std::string& bytes, bool tes3)
    {
        for (const ToUTF8::FromType encoding : { ToUTF8::WINDOWS_1250, ToUTF8::WINDOWS_1251, ToUTF8::WINDOWS_1252 })
        {
            const ExpectedFromTypedReader expected = tes3 ? typedTes3(bytes, encoding) : typedTes4(bytes, encoding);
            const Content::PluginInfo got = probeBytes(bytes, converterFor(encoding));
            EXPECT_FLOAT_EQ(got.mVersion, expected.mVersion) << "encoding " << encoding;
            EXPECT_EQ(got.mAuthor, expected.mAuthor) << "encoding " << encoding;
            EXPECT_EQ(got.mDescription, expected.mDescription) << "encoding " << encoding;
            EXPECT_EQ(got.mMasters, expected.mMasters) << "encoding " << encoding;
        }
    }

    TEST(ContentProbe, equals_the_typed_tes3_reader_on_ascii_and_legacy_code_pages)
    {
        expectSameAsTypedReader(tes3Header(1.3f, 1, "Bethesda", "Master file", 0, {}).str(), true);
        expectSameAsTypedReader(
            tes3Header(1.2f, 0, "Auth\xe9r", sAccents, 0, { { "Alpha.esm", 123456 }, { "Caf\xe9.esm", 5 } }).str(),
            true);
        expectSameAsTypedReader(
            tes3Header(0.0f, 0, std::string(32, 'x'), std::string(256, 'y'), 0, { { "Alpha.esm", 1 } }).str(), true);
        expectSameAsTypedReader(tes3Header(1.3f, 0, "", "", 0, {}).str(), true);
    }

    TEST(ContentProbe, equals_the_typed_tes4_reader_on_ascii_and_legacy_code_pages)
    {
        expectSameAsTypedReader(tes4Header(1.7f, 0x81, "Beth \xe9", sAccents, 0, {}).str(), false);
        expectSameAsTypedReader(
            tes4Header(1.71f, 0, "Modder", "A mod", 0, { { "Skyrimlike.esm", 0 }, { "Caf\xe9.esm", 0 } }).str(), false);
        expectSameAsTypedReader(
            tes4Header(1.0f, 0, "Old", "Short header", 0, { { "Skyrimlike.esm", 0 } }, 20).str(), false);
        expectSameAsTypedReader(tes4Header(1.7f, 0, "", "", 0, {}).str(), false);
    }
}
