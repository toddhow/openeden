#include <components/worldlod/bakestatus.hpp>

#include "../modorganizer/tempfolder.hpp"

#include <gtest/gtest.h>

using namespace WorldLod;
using ModOrganizerTest::TempFolder;

namespace
{
    BakeStatus sample()
    {
        BakeStatus s;
        s.mToolVersion = "1.0";
        s.mPid = 4242;
        s.mProcessStartTime = "token-1";
        s.mState = BakerState::Building;
        s.mHeartbeat = 1730000000;
        s.mProfile = "Main";
        s.mFingerprint = std::string(64, 'a');
        s.mUnitsTotal = 13000;
        s.mUnitsDone = 5400;
        s.mUnitsFailed = 1;
        s.mLevel = 3;
        s.mLevelsTotal = 6;
        s.mActivity = "terrain";
        s.mLastCell = { "", -4, 7 };
        s.mEtaSeconds = 600;
        s.mCacheBytes = 123456;
        s.mErrors = { { "tile 3/1/2", "located message" } };
        s.mErrorsTotal = 1;
        s.mLastCommandId = 2;
        return s;
    }

    std::string replaceFirst(std::string text, const std::string& from, const std::string& to)
    {
        const auto at = text.find(from);
        EXPECT_NE(at, std::string::npos) << from;
        text.replace(at, from.size(), to);
        return text;
    }
}

TEST(WorldLodStatus, round_trips_byte_for_byte_for_every_state)
{
    for (BakerState state :
        { BakerState::Idle, BakerState::Building, BakerState::Paused, BakerState::Complete, BakerState::Failed })
    {
        BakeStatus s = sample();
        s.mState = state;
        const std::string text = writeStatus(s);
        StatusError error;
        const auto read = readStatus(text, "status.json", error);
        ASSERT_TRUE(read.has_value()) << error.format();
        EXPECT_EQ(*read, s);
        EXPECT_EQ(writeStatus(*read), text);
    }
}

TEST(WorldLodStatus, golden_state_names)
{
    const std::string text = writeStatus(sample());
    EXPECT_NE(text.find("\"format\": \"openeden.lod.status\""), std::string::npos);
    EXPECT_NE(text.find("\"state\": \"building\""), std::string::npos);
    EXPECT_NE(text.find("\"version\": 1"), std::string::npos);
    EXPECT_EQ(text.back(), '\n');
}

TEST(WorldLodStatus, a_newer_major_version_is_flagged_and_not_parsed)
{
    const std::string text = replaceFirst(writeStatus(sample()), "\"version\": 1", "\"version\": 2");
    StatusError error;
    EXPECT_FALSE(readStatus(text, "s", error).has_value());
    EXPECT_TRUE(error.mNewerVersion);
    // Even when the rest of the file is garbage for this reader.
    EXPECT_FALSE(readStatus("{\"format\":\"openeden.lod.status\",\"version\":9,\"state\":[1]}", "s", error));
    EXPECT_TRUE(error.mNewerVersion);
}

TEST(WorldLodStatus, hostile_files_are_refused_with_a_located_error)
{
    StatusError error;
    EXPECT_FALSE(readStatus("", "s", error));
    EXPECT_FALSE(error.mMessage.empty());
    EXPECT_FALSE(readStatus("{\"format\":", "s", error));
    EXPECT_FALSE(readStatus("[]", "s", error));
    EXPECT_FALSE(readStatus(std::string(sMaxStatusBytes + 1, ' '), "s", error));
    EXPECT_FALSE(readStatus(std::string(5000, '['), "s", error));

    const std::string good = writeStatus(sample());
    EXPECT_FALSE(readStatus(replaceFirst(good, "\"state\": \"building\"", "\"state\": \"flying\""), "s", error));
    EXPECT_EQ(error.mPointer, "/state");
    EXPECT_FALSE(readStatus(replaceFirst(good, "\"unitsDone\": 5400", "\"unitsDone\": \"5400\""), "s", error));
    EXPECT_FALSE(readStatus(replaceFirst(good, "\"unitsDone\": 5400", "\"unitsDone\": 54.5"), "s", error));
    EXPECT_FALSE(readStatus(replaceFirst(good, "\"unitsDone\": 5400", "\"unitsDone\": -1"), "s", error));
    EXPECT_FALSE(
        readStatus(replaceFirst(good, "\"unitsDone\": 5400", "\"unitsDone\": 99999999999999999999"), "s", error));
    EXPECT_FALSE(readStatus(replaceFirst(good, "\"unitsDone\": 5400,", ""), "s", error)) << "missing field";
    EXPECT_FALSE(
        readStatus(replaceFirst(good, "\"unitsDone\": 5400,", "\"unitsDone\": 5400, \"unitsDone\": 1,"), "s", error))
        << "duplicate key";
    EXPECT_FALSE(readStatus(replaceFirst(good, "\"pid\": 4242,", "\"pid\": 4242, \"extra\": 1,"), "s", error));
}

TEST(WorldLodStatus, huge_strings_and_too_many_errors_are_refused)
{
    BakeStatus s = sample();
    s.mActivity = std::string(sMaxTextBytes + 1, 'x');
    StatusError error;
    EXPECT_FALSE(readStatus(writeStatus(s), "s", error));
    s = sample();
    s.mErrors.assign(sMaxErrors + 1, BakeError{ "u", "m" });
    EXPECT_FALSE(readStatus(writeStatus(s), "s", error));
    s.mErrors.assign(sMaxErrors, BakeError{ "u", "m" });
    s.mErrorsTotal = 500;
    EXPECT_TRUE(readStatus(writeStatus(s), "s", error).has_value()) << "the rest is only counted";
}

TEST(WorldLodStatus, text_is_data_not_markup_or_a_path_to_open)
{
    BakeStatus s = sample();
    s.mLogPath = "C:/x/../../etc/passwd";
    s.mErrors = { { "<b>unit</b>", "<script>alert(1)</script>" } };
    StatusError error;
    const auto read = readStatus(writeStatus(s), "s", error);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->mErrors[0].mMessage, "<script>alert(1)</script>");
}

TEST(WorldLodCommand, round_trip_and_errors)
{
    for (CommandKind kind : { CommandKind::Pause, CommandKind::Resume, CommandKind::Stop })
    {
        const Command c{ 17, kind };
        StatusError error;
        const auto read = readCommand(writeCommand(c), "command.json", error);
        ASSERT_TRUE(read.has_value()) << error.format();
        EXPECT_EQ(*read, c);
        EXPECT_EQ(writeCommand(*read), writeCommand(c));
    }
    StatusError error;
    EXPECT_FALSE(readCommand(
        "{\"command\":\"explode\",\"format\":\"openeden.lod.command\",\"id\":1,\"version\":1}", "c", error));
    EXPECT_FALSE(
        readCommand("{\"command\":\"stop\",\"format\":\"openeden.lod.command\",\"id\":-1,\"version\":1}", "c", error));
    EXPECT_FALSE(readCommand("nope", "c", error));
    EXPECT_FALSE(
        readCommand("{\"command\":\"stop\",\"format\":\"openeden.lod.command\",\"id\":1,\"version\":3}", "c", error));
    EXPECT_TRUE(error.mNewerVersion);
}

namespace
{
    JudgeInput input(BakerState state, std::int64_t age = 1)
    {
        JudgeInput in;
        in.mStatusFileExists = true;
        in.mStatus = sample();
        in.mStatus->mState = state;
        in.mNow = in.mStatus->mHeartbeat + age;
        in.mActiveFingerprint = in.mStatus->mFingerprint;
        return in;
    }
}

TEST(WorldLodJudge, every_state_of_the_table)
{
    JudgeInput in = input(BakerState::Building);
    in.mFeatureEnabled = false;
    EXPECT_EQ(judge(in).mState, PageState::Off);

    in = JudgeInput();
    EXPECT_EQ(judge(in).mState, PageState::NotBuilt) << "enabled, no status file";

    EXPECT_EQ(judge(input(BakerState::Building)).mState, PageState::Building);
    EXPECT_EQ(judge(input(BakerState::Paused)).mState, PageState::Paused);
    EXPECT_EQ(judge(input(BakerState::Complete)).mState, PageState::UpToDate);

    in = input(BakerState::Complete);
    in.mActiveFingerprint = std::string(64, 'b');
    EXPECT_EQ(judge(in).mState, PageState::OutOfDate);

    EXPECT_EQ(judge(input(BakerState::Failed)).mState, PageState::Failed);

    in = JudgeInput();
    in.mStatusFileExists = true;
    in.mUnreadableReason = "truncated";
    const Judgement j = judge(in);
    EXPECT_EQ(j.mState, PageState::Unknown);
    EXPECT_NE(j.mReason.find("truncated"), std::string::npos);

    EXPECT_EQ(judge(input(BakerState::Idle)).mState, PageState::NotBuilt);
}

TEST(WorldLodJudge, a_stale_heartbeat_is_failed_never_building)
{
    JudgeInput in = input(BakerState::Building, 31);
    Judgement j = judge(in);
    EXPECT_EQ(j.mState, PageState::Failed);
    EXPECT_FALSE(j.mReason.empty());
    EXPECT_EQ(judge(input(BakerState::Building, 30)).mState, PageState::Building) << "30 s is the edge";
    EXPECT_EQ(judge(input(BakerState::Paused, 31)).mState, PageState::Failed);
    EXPECT_EQ(judge(input(BakerState::Complete, 100000)).mState, PageState::UpToDate) << "terminal states are trusted";
    in = input(BakerState::Building, 100);
    in.mHeartbeatTimeout = 200;
    EXPECT_EQ(judge(in).mState, PageState::Building) << "the timeout is a setting";
}

TEST(WorldLodJudge, a_heartbeat_from_the_future_is_not_fresh_forever)
{
    JudgeInput in = input(BakerState::Building, -100000);
    EXPECT_NE(judge(in).mState, PageState::Building);
}

TEST(WorldLodJudge, a_complete_file_without_a_fingerprint_is_out_of_date_not_up_to_date)
{
    JudgeInput in = input(BakerState::Complete);
    in.mStatus->mFingerprint.clear();
    EXPECT_EQ(judge(in).mState, PageState::OutOfDate);
}

TEST(WorldLodJudge, eta_is_hidden_until_enough_units_are_done)
{
    BakeStatus s = sample();
    EXPECT_EQ(shownEtaSeconds(s), 600);
    s.mUnitsDone = 10;
    EXPECT_FALSE(shownEtaSeconds(s).has_value());
    s = sample();
    s.mEtaSeconds = -1;
    EXPECT_FALSE(shownEtaSeconds(s).has_value());
    s.mEtaSeconds = 0;
    EXPECT_FALSE(shownEtaSeconds(s).has_value());
}

TEST(WorldLodFiles, atomic_write_and_capped_read)
{
    TempFolder t;
    std::string error;
    const auto path = t.path() / "status.json";
    ASSERT_TRUE(writeFileAtomically(path, "first", error)) << error;
    ASSERT_TRUE(writeFileAtomically(path, "second", error)) << error;
    EXPECT_EQ(TempFolder::read(path), "second");
    EXPECT_FALSE(std::filesystem::exists(t.path() / "status.json.tmp"));
    EXPECT_EQ(readSmallFile(path, 100, error), std::optional<std::string>("second"));
    EXPECT_FALSE(readSmallFile(path, 3, error).has_value());
    EXPECT_NE(error.find("larger"), std::string::npos);
    EXPECT_FALSE(readSmallFile(t.path() / "nope", 100, error).has_value());
}

TEST(WorldLodFiles, a_kill_mid_write_leaves_the_previous_status_readable)
{
    TempFolder t;
    std::string error;
    const auto path = t.path() / "status.json";
    ASSERT_TRUE(writeFileAtomically(path, writeStatus(sample()), error));
    // A baker killed while writing leaves only the temporary name half written.
    t.write("status.json.tmp", "{\"format\": \"openeden.lod");
    StatusError e;
    const auto text = readSmallFile(path, sMaxStatusBytes, error);
    ASSERT_TRUE(text.has_value());
    EXPECT_TRUE(readStatus(*text, "status.json", e).has_value());
}

TEST(WorldLodCache, only_a_folder_named_openeden_lod_counts)
{
    EXPECT_TRUE(isLodCacheFolder("C:/Users/x/cache/openeden/lod"));
    EXPECT_TRUE(isLodCacheFolder(std::filesystem::path("/home/x/.cache/openeden/lod/")));
    EXPECT_FALSE(isLodCacheFolder("C:/Users/x/Documents"));
    EXPECT_FALSE(isLodCacheFolder("C:/Users/x/cache/openeden"));
    EXPECT_FALSE(isLodCacheFolder("openeden/lod/.."));
    EXPECT_FALSE(isLodCacheFolder(""));
}

TEST(WorldLodCache, listing_and_deleting_touch_only_the_caches_own_files)
{
    TempFolder t;
    const auto cache = t.path() / "openeden" / "lod";
    t.write("openeden/lod/status.json", "12345");
    t.write("openeden/lod/gen-1/tile.edenpak", "123");
    t.write("openeden/lod/gen-1/notes.txt", "mine");
    t.write("openeden/lod/baker.lock", "1");
    const auto outside = t.write("outside/precious.json", "keep");
    std::error_code ec;
    std::filesystem::create_directory_symlink(t.path() / "outside", cache / "link", ec);

    const CacheListing listing = listCacheFiles(cache);
    EXPECT_EQ(listing.mFiles.size(), 3u);
    EXPECT_EQ(listing.mBytes, 5u + 3u + 1u);
    EXPECT_FALSE(listing.mSkipped.empty()) << "notes.txt is not of the cache's naming";

    EXPECT_NE(deleteCache(cache, listing, true, false), "") << "a baker is alive";
    EXPECT_NE(deleteCache(cache, listing, false, true), "") << "the game is running";
    EXPECT_NE(deleteCache(t.path(), listing, false, false), "") << "not the cache folder";
    EXPECT_TRUE(std::filesystem::exists(cache / "status.json"));

    EXPECT_EQ(deleteCache(cache, listing, false, false), "");
    EXPECT_FALSE(std::filesystem::exists(cache / "status.json"));
    EXPECT_FALSE(std::filesystem::exists(cache / "gen-1" / "tile.edenpak"));
    EXPECT_TRUE(std::filesystem::exists(cache / "gen-1" / "notes.txt"));
    EXPECT_TRUE(std::filesystem::exists(outside));
}

TEST(WorldLodCache, a_listing_naming_a_file_outside_the_folder_is_refused)
{
    TempFolder t;
    const auto cache = t.path() / "openeden" / "lod";
    t.write("openeden/lod/status.json", "1");
    const auto victim = t.write("victim.json", "keep");
    CacheListing forged;
    forged.mFiles = { victim };
    EXPECT_NE(deleteCache(cache, forged, false, false), "");
    EXPECT_TRUE(std::filesystem::exists(victim));
    forged.mFiles = { cache / ".." / ".." / "victim.json" };
    EXPECT_NE(deleteCache(cache, forged, false, false), "");
    EXPECT_TRUE(std::filesystem::exists(victim));
}
