// Tests for task 0074d, the review of task 0074b (finding 1): an actor's equipment change or rebuild compiles the
// nodes it attached also when the update throws (MWRender::runEquipmentUpdate), and such a throw is reachable in the
// game: a part's model whose read fails in the VFS (an Eden Archive entry that fails its check, an I/O error) reaches
// the caller of vsgUtil::readNode through the game's node options (the VFS reader, then Resource::Fallback), while a
// model the VFS does not have gets the fallback node. Written from the task's test specification before the
// implementation and run red against its stub.

#include "apps/openmw/mwrender/equipmentupdate.hpp"

#include <components/files/istreamptr.hpp>
#include <components/resource/fallback.hpp>
#include <components/vfs/archive.hpp>
#include <components/vfs/file.hpp>
#include <components/vfs/filemap.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vsgadapters/vfs.hpp>
#include <components/vsgutil/readnode.hpp>

#include <vsg/core/ref_ptr.h>
#include <vsg/io/Options.h>
#include <vsg/nodes/Node.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    // What a part's model read throws when its archive entry fails its check.
    struct PartReadError : public std::runtime_error
    {
        PartReadError(const std::string& what, int part)
            : std::runtime_error(what)
            , mPart(part)
        {
        }
        int mPart;
    };

    // A thrown value of a type that is not a std::exception.
    struct OtherError
    {
        int mCode;
    };

    TEST(EquipmentUpdateTest, compile_runs_once_after_an_update_that_returns)
    {
        std::vector<std::string> calls;
        MWRender::runEquipmentUpdate([&] { calls.emplace_back("update"); }, [&] { calls.emplace_back("compile"); });
        EXPECT_EQ(calls, (std::vector<std::string>{ "update", "compile" }));
    }

    TEST(EquipmentUpdateTest, compile_runs_once_when_the_update_throws_and_the_same_exception_reaches_the_caller)
    {
        // Read from data, so the compiler does not see an update that always throws (MSVC's C4702 on the stub's
        // compile after it).
        const std::vector<int> brokenParts{ 7 };
        std::vector<std::string> calls;
        bool caught = false;
        try
        {
            MWRender::runEquipmentUpdate(
                [&] {
                    calls.emplace_back("update");
                    if (!brokenParts.empty())
                        throw PartReadError("meshes/b/broken.nif: the entry failed its check", brokenParts.front());
                },
                [&] { calls.emplace_back("compile"); });
        }
        catch (const PartReadError& error)
        {
            caught = true;
            EXPECT_EQ(error.mPart, 7);
            EXPECT_STREQ(error.what(), "meshes/b/broken.nif: the entry failed its check");
        }
        catch (...)
        {
            ADD_FAILURE() << "another exception than the update's reached the caller";
        }
        EXPECT_TRUE(caught) << "the update's exception did not reach the caller";
        EXPECT_EQ(calls, (std::vector<std::string>{ "update", "compile" }));
    }

    TEST(EquipmentUpdateTest, compile_runs_once_when_the_update_throws_something_that_is_not_a_std_exception)
    {
        const std::vector<int> codes{ 42 };
        std::vector<std::string> calls;
        bool caught = false;
        try
        {
            MWRender::runEquipmentUpdate(
                [&] {
                    calls.emplace_back("update");
                    if (!codes.empty())
                        throw OtherError{ codes.front() };
                },
                [&] { calls.emplace_back("compile"); });
        }
        catch (const OtherError& error)
        {
            caught = true;
            EXPECT_EQ(error.mCode, 42);
        }
        catch (...)
        {
            ADD_FAILURE() << "another exception than the update's reached the caller";
        }
        EXPECT_TRUE(caught) << "the update's exception did not reach the caller";
        EXPECT_EQ(calls, (std::vector<std::string>{ "update", "compile" }));
    }

    // What an Eden Archive entry that fails its check (or an I/O error) throws when it is opened.
    struct EntryCheckFailed : public std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    constexpr std::string_view sBrokenModel = "meshes/b/broken.nif";
    constexpr std::string_view sBrokenMessage = "meshes/b/broken.nif: the entry failed its check";

    class BrokenEntry : public VFS::File
    {
    public:
        Files::IStreamPtr open() override { throw EntryCheckFailed(std::string(sBrokenMessage)); }

        std::filesystem::file_time_type getLastModified() const override { return {}; }

        std::string getStem() const override { return "broken"; }
    };

    // An archive with one entry, which fails its check when it is opened.
    class BrokenArchive : public VFS::Archive
    {
    public:
        void listResources(VFS::FileMap& out) override { out[VFS::Path::Normalized(sBrokenModel)] = &mEntry; }

        bool contains(VFS::Path::NormalizedView file) const override { return file.value() == sBrokenModel; }

        std::string getDescription() const override { return "test archive with a broken entry"; }

    private:
        BrokenEntry mEntry;
    };

    // The end of the game's node options: the VFS reader, then the fallback for missing or erroneous files.
    vsg::ref_ptr<const vsg::Options> nodeOptions(const VFS::Manager& vfs, vsg::ref_ptr<vsg::Node> fallbackNode)
    {
        auto options = vsg::Options::create();
        options->readerWriters.push_back(vsg::ref_ptr<vsgAdapters::vfs>(new vsgAdapters::vfs(vfs)));
        vsg::ref_ptr<Resource::Fallback> fallback(new Resource::Fallback);
        fallback->fallbackObject = fallbackNode;
        options->readerWriters.push_back(fallback);
        return options;
    }

    TEST(EquipmentUpdateTest, a_model_whose_archive_entry_fails_its_check_throws_to_the_reader)
    {
        VFS::Manager vfs;
        vfs.addArchive(std::make_unique<BrokenArchive>());
        vfs.buildIndex();
        const vsg::ref_ptr<const vsg::Options> options = nodeOptions(vfs, vsg::Node::create());

        bool caught = false;
        try
        {
            vsgUtil::readNode(std::string(sBrokenModel), options);
        }
        catch (const EntryCheckFailed& error)
        {
            caught = true;
            EXPECT_EQ(std::string_view(error.what()), sBrokenMessage);
        }
        catch (const std::exception& error)
        {
            ADD_FAILURE() << "another exception than the entry's reached the caller: " << error.what();
        }
        EXPECT_TRUE(caught) << "the read of " << sBrokenModel << " did not throw the entry's error";
    }

    TEST(EquipmentUpdateTest, a_model_the_vfs_does_not_have_gets_the_fallback_node)
    {
        VFS::Manager vfs;
        vfs.addArchive(std::make_unique<BrokenArchive>());
        vfs.buildIndex();
        const vsg::ref_ptr<vsg::Node> fallbackNode = vsg::Node::create();
        const vsg::ref_ptr<const vsg::Options> options = nodeOptions(vfs, fallbackNode);

        vsg::ref_ptr<vsg::Node> node;
        EXPECT_NO_THROW(node = vsgUtil::readNode("meshes/b/missing.nif", options));
        EXPECT_EQ(node.get(), fallbackNode.get());
    }
}
