// Task 0104 (decision 0015, UI system U1): RmlUi's file interface over the game's virtual file system. The files are
// generated here, in a folder of their own under the test output directory.
#include <components/ui_rml/fileinterface.hpp>

#include "../vfs/vfstestutil.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>

namespace
{
    using namespace UiRml;
    using VFSTest::ScratchDir;
    using VFSTest::writeFile;

    std::unique_ptr<VFS::Manager> managerOf(std::initializer_list<std::filesystem::path> folders)
    {
        auto manager = std::make_unique<VFS::Manager>();
        for (const auto& folder : folders)
            manager->addArchive(std::make_unique<VFS::FileSystemArchive>(folder));
        manager->buildIndex();
        return manager;
    }

    TEST(UiRmlFileInterface, opens_reads_and_closes_a_file_of_the_vfs)
    {
        ScratchDir dir;
        writeFile(dir.path() / "fonts" / "sample.fnt", "0123456789");
        const auto vfs = managerOf({ dir.path() });
        VfsFileInterface files(*vfs);

        const Rml::FileHandle file = files.Open("fonts/sample.fnt");
        ASSERT_NE(file, 0u);
        EXPECT_EQ(files.Length(file), 10u);
        EXPECT_EQ(files.Tell(file), 0u);
        char buffer[16] = {};
        EXPECT_EQ(files.Read(buffer, 4, file), 4u);
        EXPECT_EQ(std::string(buffer, 4), "0123");
        EXPECT_EQ(files.Tell(file), 4u);
        EXPECT_EQ(files.openCount(), 1u);
        files.Close(file);
        EXPECT_EQ(files.openCount(), 0u);
    }

    TEST(UiRmlFileInterface, names_are_case_insensitive_and_take_either_slash)
    {
        ScratchDir dir;
        writeFile(dir.path() / "fonts" / "magic.fnt", "x");
        const auto vfs = managerOf({ dir.path() });
        VfsFileInterface files(*vfs);
        for (const char* spelling : { "Fonts/Magic.FNT", "fonts\\magic.fnt", "FONTS/MAGIC.fnt" })
        {
            const Rml::FileHandle file = files.Open(spelling);
            EXPECT_NE(file, 0u) << spelling;
            files.Close(file);
        }
    }

    TEST(UiRmlFileInterface, a_missing_file_gives_the_null_handle_and_is_not_looked_for_on_the_disk)
    {
        ScratchDir inVfs;
        writeFile(inVfs.path() / "a.txt", "a");
        ScratchDir other;
        writeFile(other.path() / "elsewhere.txt", "on disk but not in the vfs");
        const auto vfs = managerOf({ inVfs.path() });
        VfsFileInterface files(*vfs);
        EXPECT_EQ(files.Open("nope.txt"), 0u);
        EXPECT_EQ(files.Open((other.path() / "elsewhere.txt").generic_string()), 0u);
        EXPECT_EQ(files.openCount(), 0u);
    }

    TEST(UiRmlFileInterface, a_read_past_the_end_returns_what_is_left)
    {
        ScratchDir dir;
        writeFile(dir.path() / "a.txt", "abcdef");
        const auto vfs = managerOf({ dir.path() });
        VfsFileInterface files(*vfs);
        const Rml::FileHandle file = files.Open("a.txt");
        char buffer[32] = {};
        EXPECT_EQ(files.Read(buffer, 4, file), 4u);
        EXPECT_EQ(files.Read(buffer, 10, file), 2u);
        EXPECT_EQ(std::string(buffer, 2), "ef");
        EXPECT_EQ(files.Read(buffer, 10, file), 0u);
        files.Close(file);
    }

    TEST(UiRmlFileInterface, seek_supports_set_current_and_end_and_refuses_positions_outside_the_file)
    {
        ScratchDir dir;
        writeFile(dir.path() / "a.txt", "0123456789");
        const auto vfs = managerOf({ dir.path() });
        VfsFileInterface files(*vfs);
        const Rml::FileHandle file = files.Open("a.txt");
        char c = 0;
        EXPECT_TRUE(files.Seek(file, 3, SEEK_SET));
        EXPECT_EQ(files.Read(&c, 1, file), 1u);
        EXPECT_EQ(c, '3');
        EXPECT_TRUE(files.Seek(file, 2, SEEK_CUR));
        files.Read(&c, 1, file);
        EXPECT_EQ(c, '6');
        EXPECT_TRUE(files.Seek(file, -1, SEEK_END));
        files.Read(&c, 1, file);
        EXPECT_EQ(c, '9');
        EXPECT_TRUE(files.Seek(file, 10, SEEK_SET)); // the end itself is a valid position
        EXPECT_EQ(files.Tell(file), 10u);
        EXPECT_FALSE(files.Seek(file, 11, SEEK_SET));
        EXPECT_FALSE(files.Seek(file, -1, SEEK_SET));
        EXPECT_FALSE(files.Seek(file, 0, 12345));
        EXPECT_EQ(files.Tell(file), 10u); // a refused seek changes nothing
        files.Close(file);
    }

    TEST(UiRmlFileInterface, two_handles_of_one_file_have_their_own_positions)
    {
        ScratchDir dir;
        writeFile(dir.path() / "a.txt", "abcdef");
        const auto vfs = managerOf({ dir.path() });
        VfsFileInterface files(*vfs);
        const Rml::FileHandle first = files.Open("a.txt");
        const Rml::FileHandle second = files.Open("a.txt");
        EXPECT_NE(first, second);
        char buffer[8] = {};
        files.Read(buffer, 3, first);
        EXPECT_EQ(files.Tell(first), 3u);
        EXPECT_EQ(files.Tell(second), 0u);
        files.Close(first);
        EXPECT_EQ(files.Length(second), 6u); // the other handle is unaffected
        files.Close(second);
    }

    TEST(UiRmlFileInterface, the_data_folder_registered_last_wins)
    {
        ScratchDir base, mod;
        writeFile(base.path() / "ui" / "panel.rml", "base");
        writeFile(mod.path() / "ui" / "panel.rml", "mod");
        const auto vfs = managerOf({ base.path(), mod.path() });
        VfsFileInterface files(*vfs);
        std::vector<char> content;
        ASSERT_TRUE(files.readAll("ui/panel.rml", content));
        EXPECT_EQ(std::string(content.begin(), content.end()), "mod");
    }

    TEST(UiRmlFileInterface, read_all_reports_a_missing_file_and_an_empty_one)
    {
        ScratchDir dir;
        writeFile(dir.path() / "empty.txt", "");
        const auto vfs = managerOf({ dir.path() });
        VfsFileInterface files(*vfs);
        std::vector<char> content{ 'x' };
        EXPECT_TRUE(files.readAll("empty.txt", content));
        EXPECT_TRUE(content.empty());
        EXPECT_FALSE(files.readAll("missing.txt", content));
    }

    TEST(UiRmlFileInterface, operations_on_a_handle_that_was_never_opened_or_is_closed_do_nothing)
    {
        ScratchDir dir;
        writeFile(dir.path() / "a.txt", "abc");
        const auto vfs = managerOf({ dir.path() });
        VfsFileInterface files(*vfs);
        char c = 0;
        EXPECT_EQ(files.Read(&c, 1, 12345), 0u);
        EXPECT_FALSE(files.Seek(12345, 0, SEEK_SET));
        EXPECT_EQ(files.Tell(12345), 0u);
        EXPECT_EQ(files.Length(12345), 0u);
        files.Close(12345); // no crash
        const Rml::FileHandle file = files.Open("a.txt");
        files.Close(file);
        EXPECT_EQ(files.Read(&c, 1, file), 0u);
        files.Close(file); // closed twice: no crash
    }

    TEST(UiRmlFileInterface, load_file_of_the_base_class_reads_the_whole_file_through_the_interface)
    {
        ScratchDir dir;
        writeFile(dir.path() / "doc.rml", "<rml/>");
        const auto vfs = managerOf({ dir.path() });
        VfsFileInterface files(*vfs);
        Rml::String text;
        EXPECT_TRUE(files.LoadFile("doc.rml", text));
        EXPECT_EQ(text, "<rml/>");
    }
}
