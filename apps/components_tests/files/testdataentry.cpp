// Task 0057: Files::isUsableDataEntry, the rule the launcher validatePaths uses to keep a data= line: a folder or a
// pack file. The launcher dropped pack lines before (the follow-up named in task 0064). Everything is generated in a
// temporary folder.

#include <components/files/collections.hpp>

#include "../modorganizer/tempfolder.hpp"

#include <gtest/gtest.h>

using ModOrganizerTest::TempFolder;

TEST(FilesDataEntry, a_folder_is_usable)
{
    TempFolder temp;
    temp.write("mod/a.esp");
    EXPECT_TRUE(Files::isUsableDataEntry(temp.path() / "mod"));
}

TEST(FilesDataEntry, a_pack_file_is_usable_in_any_case)
{
    TempFolder temp;
    temp.write("packs/a.edenpak", "EDENPAK");
    temp.write("packs/B.EdenPak", "EDENPAK");
    EXPECT_TRUE(Files::isUsableDataEntry(temp.path() / "packs" / "a.edenpak"));
    EXPECT_TRUE(Files::isUsableDataEntry(temp.path() / "packs" / "B.EdenPak"));
}

TEST(FilesDataEntry, other_files_and_missing_paths_are_not_usable)
{
    TempFolder temp;
    temp.write("x/readme.txt");
    temp.write("x/archive.bsa");
    EXPECT_FALSE(Files::isUsableDataEntry(temp.path() / "x" / "readme.txt"));
    EXPECT_FALSE(Files::isUsableDataEntry(temp.path() / "x" / "archive.bsa"));
    EXPECT_FALSE(Files::isUsableDataEntry(temp.path() / "missing"));
    EXPECT_FALSE(Files::isUsableDataEntry(temp.path() / "missing.edenpak"));
}

TEST(FilesDataEntry, a_folder_named_like_a_pack_is_a_usable_folder)
{
    TempFolder temp;
    temp.write("odd.edenpak/inside.txt");
    EXPECT_TRUE(Files::isUsableDataEntry(temp.path() / "odd.edenpak"));
}

// toddhow, 2026-11-10: an empty data= value keeps OpenMW's meaning. The launcher asked QDir("").exists(), which is
// true for the current folder, so the line counted; only the .edenpak case is new.
TEST(FilesDataEntry, an_empty_value_counts_as_the_current_folder_as_before)
{
    EXPECT_TRUE(Files::isUsableDataEntry({}));
}
