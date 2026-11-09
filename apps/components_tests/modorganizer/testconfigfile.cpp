#include <components/modorganizer/configfile.hpp>
#include <components/modorganizer/processcheck.hpp>

#include "tempfolder.hpp"

#include <gtest/gtest.h>

using namespace ModOrganizer;
using ModOrganizerTest::TempFolder;

TEST(ModOrganizerConfig, owned_lines_are_the_five_keys_only)
{
    EXPECT_TRUE(isOwnedLine("data=\"x\""));
    EXPECT_TRUE(isOwnedLine("  content = a.esp"));
    EXPECT_TRUE(isOwnedLine("groundcover=g.esp"));
    EXPECT_TRUE(isOwnedLine("fallback-archive=x.bsa"));
    EXPECT_TRUE(isOwnedLine("extension=compat"));
    EXPECT_FALSE(isOwnedLine("data-local=\"x\""));
    EXPECT_FALSE(isOwnedLine("fallback=Foo,1"));
    EXPECT_FALSE(isOwnedLine("resources=x"));
    EXPECT_FALSE(isOwnedLine("replace=extension"));
    EXPECT_FALSE(isOwnedLine("# data=\"x\""));
    EXPECT_FALSE(isOwnedLine("##content=a.esp"));
    EXPECT_FALSE(isOwnedLine("data"));
}

TEST(ModOrganizerConfig, other_lines_and_comments_stay_and_the_new_lines_take_the_first_old_place)
{
    const std::string before = "# my config\n"
                               "encoding=win1252\n"
                               "# the data\n"
                               "data=\"C:/old\"\n"
                               "fallback=Foo,1\n"
                               "content=old.esp\n"
                               "data-local=\"C:/local\"\n"
                               "# trailing comment\n";
    const std::vector<std::string> wanted = { "data=\"C:/new\"", "content=new.esp" };
    const RewriteResult r = rewriteOwnedLines(before, wanted);
    EXPECT_TRUE(r.mChanged);
    EXPECT_EQ(r.mText,
        "# my config\n"
        "encoding=win1252\n"
        "# the data\n"
        "data=\"C:/new\"\n"
        "content=new.esp\n"
        "fallback=Foo,1\n"
        "data-local=\"C:/local\"\n"
        "# trailing comment\n");
}

TEST(ModOrganizerConfig, writing_the_same_lines_again_changes_nothing)
{
    const std::string before = "a=1\ndata=\"C:/x\"\ncontent=a.esp\nb=2\n";
    const std::vector<std::string> wanted = { "data=\"C:/x\"", "content=a.esp" };
    const RewriteResult r = rewriteOwnedLines(before, wanted);
    EXPECT_FALSE(r.mChanged);
    EXPECT_EQ(r.mText, before);
}

TEST(ModOrganizerConfig, no_old_owned_lines_means_append_at_the_end)
{
    const RewriteResult r = rewriteOwnedLines("a=1", { "content=a.esp" });
    EXPECT_EQ(r.mText, "a=1\ncontent=a.esp");
    EXPECT_TRUE(rewriteOwnedLines("", { "content=a.esp" }).mChanged);
    EXPECT_EQ(rewriteOwnedLines("", { "content=a.esp" }).mText, "content=a.esp\n");
}

TEST(ModOrganizerConfig, crlf_files_keep_crlf)
{
    const RewriteResult r = rewriteOwnedLines("a=1\r\ncontent=old\r\nb=2\r\n", { "content=new" });
    EXPECT_EQ(r.mText, "a=1\r\ncontent=new\r\nb=2\r\n");
}

TEST(ModOrganizerConfig, removing_all_owned_lines_when_the_profile_is_empty)
{
    const RewriteResult r = rewriteOwnedLines("a=1\ncontent=old\nb=2\n", {});
    EXPECT_EQ(r.mText, "a=1\nb=2\n");
}

TEST(ModOrganizerConfig, hand_edit_detection)
{
    const std::vector<std::string> expected = { "data=\"C:/x\"", "content=a.esp" };
    EXPECT_FALSE(differsFromProfile("a=1\ndata=\"C:/x\"\ncontent=a.esp\n", expected));
    EXPECT_TRUE(differsFromProfile("a=1\ndata=\"C:/x\"\ncontent=b.esp\n", expected));
    EXPECT_TRUE(differsFromProfile("a=1\n", expected));
    EXPECT_FALSE(differsFromProfile("  data=\"C:/x\"  \ncontent=a.esp", expected)) << "blanks do not count";
    EXPECT_FALSE(differsFromProfile("a=1\nb=2\n", {})) << "comments and other keys are not owned";
}

TEST(ModOrganizerConfig, apply_writes_a_backup_first_and_keeps_the_last_five)
{
    TempFolder t;
    const auto cfg = t.write("openmw.cfg", "a=1\ncontent=v0\n");
    ApplyOptions options;
    for (int i = 1; i <= 8; ++i)
    {
        const ApplyResult r = applyToConfigFile(cfg, { "content=v" + std::to_string(i) }, options);
        ASSERT_TRUE(r.mOk) << r.mError;
        EXPECT_TRUE(r.mWritten);
        ASSERT_FALSE(r.mBackup.empty());
        EXPECT_EQ(TempFolder::read(r.mBackup), "a=1\ncontent=v" + std::to_string(i - 1) + "\n");
    }
    EXPECT_EQ(TempFolder::read(cfg), "a=1\ncontent=v8\n");
    EXPECT_EQ(listBackups(cfg).size(), 5u);
    EXPECT_FALSE(std::filesystem::exists(cfg.string() + ".tmp"));
}

TEST(ModOrganizerConfig, nothing_changed_means_no_write_and_no_backup)
{
    TempFolder t;
    const auto cfg = t.write("openmw.cfg", "a=1\ncontent=v0\n");
    const auto stamp = std::filesystem::last_write_time(cfg);
    const ApplyResult r = applyToConfigFile(cfg, { "content=v0" }, ApplyOptions());
    EXPECT_TRUE(r.mOk);
    EXPECT_FALSE(r.mWritten);
    EXPECT_TRUE(listBackups(cfg).empty());
    EXPECT_EQ(std::filesystem::last_write_time(cfg), stamp);
}

TEST(ModOrganizerConfig, the_global_file_is_never_written)
{
    TempFolder t;
    const auto cfg = t.write("global/openmw.cfg", "content=g\n");
    ApplyOptions options;
    options.mGlobalConfig = t.path() / "global" / ".." / "global" / "openmw.cfg";
    const ApplyResult r = applyToConfigFile(cfg, { "content=other" }, options);
    EXPECT_FALSE(r.mOk);
    EXPECT_EQ(TempFolder::read(cfg), "content=g\n");
}

TEST(ModOrganizerConfig, refuses_while_the_game_runs)
{
    TempFolder t;
    const auto cfg = t.write("openmw.cfg", "content=g\n");
    ApplyOptions options;
    options.mGameRunning = [] { return true; };
    const ApplyResult r = applyToConfigFile(cfg, { "content=other" }, options);
    EXPECT_FALSE(r.mOk);
    EXPECT_FALSE(r.mWritten);
    EXPECT_EQ(TempFolder::read(cfg), "content=g\n");
    EXPECT_TRUE(listBackups(cfg).empty());
}

TEST(ModOrganizerConfig, a_missing_file_is_created_without_a_backup)
{
    TempFolder t;
    const auto cfg = t.path() / "sub" / "openmw.cfg";
    const ApplyResult r = applyToConfigFile(cfg, { "content=a.esp" }, ApplyOptions());
    ASSERT_TRUE(r.mOk) << r.mError;
    EXPECT_TRUE(r.mWritten);
    EXPECT_TRUE(r.mBackup.empty());
    EXPECT_EQ(TempFolder::read(cfg), "content=a.esp\n");
}

TEST(ModOrganizerConfig, a_wanted_line_that_is_not_owned_or_has_a_newline_is_refused)
{
    TempFolder t;
    const auto cfg = t.write("openmw.cfg", "a=1\n");
    EXPECT_FALSE(applyToConfigFile(cfg, { "resources=/etc" }, ApplyOptions()).mOk);
    EXPECT_FALSE(applyToConfigFile(cfg, { "content=a\nreplace=data" }, ApplyOptions()).mOk);
    EXPECT_EQ(TempFolder::read(cfg), "a=1\n");
}

TEST(ModOrganizerProcess, the_current_process_is_found_and_a_made_up_one_is_not)
{
    const std::string self = currentProcessName();
    ASSERT_FALSE(self.empty());
    EXPECT_TRUE(isProcessRunning(self));
    std::string upper = self;
    for (char& c : upper)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    EXPECT_TRUE(isProcessRunning(upper));
    EXPECT_FALSE(isProcessRunning("no-such-program-oe-0057"));
    EXPECT_FALSE(isProcessRunning(""));
}
