// Task 0107 (neutral world model stage N3a-2b): the session's content list as a game pack sees it. Written from the
// spec first. Generated plugin files only; no game data.
#include <components/extension/game_services.hpp>
#include <components/extension/session_content.hpp>
#include <openeden/game/content.hpp>
#include <openeden/game/world_source_service.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"
#include "sessionheaderreader.hpp"

namespace
{
    using namespace ContentTests;
    using openeden::extension::collectSessionContent;
    using openeden::extension::filterSessionContent;
    using openeden::extension::SessionContentEntry;
    using openeden::extension::WorldSourceServiceImpl;
    using openeden::sdk::ContentHeader;
    using openeden::sdk::game::SessionContentFile;
    namespace fs = std::filesystem;

    struct TempDir
    {
        fs::path mPath;
        TempDir()
        {
            static int counter = 0;
            mPath = fs::temp_directory_path()
                / ("oe_session_content_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
                    + "_" + std::to_string(counter++));
            fs::create_directories(mPath);
        }
        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(mPath, ec);
        }
        fs::path write(const std::string& name, const Bytes& bytes) const
        {
            const fs::path p = mPath / name;
            std::ofstream out(p, std::ios::binary);
            const std::string s = bytes.str();
            out.write(s.data(), static_cast<std::streamsize>(s.size()));
            return p;
        }
    };

    SessionContentEntry entry(const std::string& name, const std::string& tag, std::uint32_t version)
    {
        SessionContentEntry e;
        e.name = name;
        e.path = "/x/" + name;
        e.gameTag = tag;
        e.formatVersion = version;
        return e;
    }

    // What the Skyrim SE pack's content loader accepts, written out here so that the test does not link the pack.
    bool skyrimAccepts(const ContentHeader& h)
    {
        return h.gameTag == "TES4" && (h.formatVersion == 43 || h.formatVersion == 44);
    }

    // ---- the pure filter ----

    TEST(SessionContentFilter, keepsTheAcceptedEntriesInTheListOrder)
    {
        const std::vector<SessionContentEntry> all{ entry("morrowind.esm", "TES3", 0), entry("Skyrim.esm", "TES4", 44),
            entry("tribunal.esm", "TES3", 0), entry("Update.esm", "TES4", 44), entry("Old.esm", "TES4", 17),
            entry("Dawnguard.esm", "TES4", 43) };
        const auto kept = filterSessionContent(all, skyrimAccepts);
        ASSERT_EQ(kept.size(), 3u);
        EXPECT_EQ(kept[0].name, "Skyrim.esm");
        EXPECT_EQ(kept[1].name, "Update.esm");
        EXPECT_EQ(kept[2].name, "Dawnguard.esm");
    }

    TEST(SessionContentFilter, aMorrowindLoaderTakesOnlyTheMorrowindPlugins)
    {
        const std::vector<SessionContentEntry> all{ entry("morrowind.esm", "TES3", 0), entry("Skyrim.esm", "TES4", 44),
            entry("bloodmoon.esm", "TES3", 0) };
        const auto kept = filterSessionContent(all, [](const ContentHeader& h) { return h.gameTag == "TES3"; });
        ASSERT_EQ(kept.size(), 2u);
        EXPECT_EQ(kept[0].name, "morrowind.esm");
        EXPECT_EQ(kept[1].name, "bloodmoon.esm");
    }

    TEST(SessionContentFilter, emptyInputAndTheTwoExtremePredicates)
    {
        EXPECT_TRUE(filterSessionContent({}, skyrimAccepts).empty());
        const std::vector<SessionContentEntry> all{ entry("a.esm", "TES4", 44), entry("b.esm", "TES3", 0) };
        EXPECT_EQ(filterSessionContent(all, [](const ContentHeader&) { return true; }).size(), 2u);
        EXPECT_TRUE(filterSessionContent(all, [](const ContentHeader&) { return false; }).empty());
    }

    TEST(SessionContentFilter, noPredicateKeepsNothing)
    {
        const std::vector<SessionContentEntry> all{ entry("a.esm", "TES4", 44) };
        EXPECT_TRUE(filterSessionContent(all, nullptr).empty());
    }

    TEST(SessionContentFilter, thePredicateSeesTheEntryTagAndVersionAsWritten)
    {
        const std::vector<SessionContentEntry> all{ entry("a.esm", "TES4", 43) };
        std::string tag;
        std::uint32_t version = 0;
        (void)filterSessionContent(all, [&](const ContentHeader& h) {
            tag = std::string(h.gameTag);
            version = h.formatVersion;
            return true;
        });
        EXPECT_EQ(tag, "TES4");
        EXPECT_EQ(version, 43u);
    }

    // ---- reading the list ----

    TEST(SessionContentCollect, readsTheHeadersOfPluginsAndSkipsWhatIsNotAPlugin)
    {
        TempDir dir;
        dir.write("morrowind.esm", tes3Header(1.3f, 1, "a", "b", 0, {}));
        dir.write("Skyrim.esm", tes4Header(1.7f, 1, "a", "b", 0, {}, 24, 44));
        dir.write("Odd.esp", tes4Header(1.7f, 0, "a", "b", 0, {}, 24, 43));
        dir.write("builtin.omwscripts", Bytes().zstring("not a plugin"));
        const std::vector<std::string> names{ "builtin.omwscripts", "morrowind.esm", "Skyrim.esm", "Odd.esp",
            "x.omwscripts", "Missing.esm" };
        const auto result = collectSessionContent(names, [&](const std::string& name) -> std::optional<fs::path> {
            const fs::path p = dir.mPath / name;
            if (!fs::exists(p))
                return std::nullopt;
            return p;
        }, SessionContentTest::readHeader);
        ASSERT_EQ(result.entries.size(), 3u);
        EXPECT_EQ(result.entries[0].name, "morrowind.esm");
        EXPECT_EQ(result.entries[0].gameTag, "TES3");
        EXPECT_EQ(result.entries[0].formatVersion, 0u);
        EXPECT_EQ(result.entries[1].name, "Skyrim.esm");
        EXPECT_EQ(result.entries[1].gameTag, "TES4");
        EXPECT_EQ(result.entries[1].formatVersion, 44u);
        EXPECT_EQ(result.entries[2].name, "Odd.esp");
        EXPECT_EQ(result.entries[2].formatVersion, 43u);
        EXPECT_FALSE(result.entries[1].path.empty());
        ASSERT_EQ(result.missing.size(), 1u);
        EXPECT_EQ(result.missing[0], "Missing.esm");
        EXPECT_TRUE(result.unreadable.empty()) << "a script list is not a plugin and is not mentioned";
    }

    TEST(SessionContentCollect, aFileThatIsNotAPluginIsUnreadableWithItsNameInTheReason)
    {
        TempDir dir;
        dir.write("broken.esp", Bytes().zstring("this is not a plugin"));
        const auto result = collectSessionContent(
            { "broken.esp" }, [&](const std::string& name) -> std::optional<fs::path> { return dir.mPath / name; },
            SessionContentTest::readHeader);
        EXPECT_TRUE(result.entries.empty());
        ASSERT_EQ(result.unreadable.size(), 1u);
        EXPECT_NE(result.unreadable[0].find("broken.esp"), std::string::npos);
    }

    TEST(SessionContentCollect, theExtensionIsMatchedWithoutCaseAndNoResolverMeansMissing)
    {
        TempDir dir;
        dir.write("Upper.ESM", tes4Header(1.7f, 1, "a", "b", 0, {}, 24, 44));
        const auto found = collectSessionContent(
            { "Upper.ESM" }, [&](const std::string& name) -> std::optional<fs::path> { return dir.mPath / name; },
            SessionContentTest::readHeader);
        ASSERT_EQ(found.entries.size(), 1u);
        const auto none = collectSessionContent({ "Upper.ESM" }, nullptr, SessionContentTest::readHeader);
        EXPECT_TRUE(none.entries.empty());
        EXPECT_EQ(none.missing.size(), 1u);
    }

    // ---- the service ----

    TEST(WorldSourceServiceContent, theSdkDefaultReturnsNothingAndAppendsNothing)
    {
        struct Bare final : openeden::sdk::game::WorldSourceService
        {
            bool registerSource(openeden::sdk::game::WorldCellSource&, std::string&) override { return false; }
            void withdrawSource(openeden::sdk::game::WorldCellSource&) override {}
            bool consumerEnabled() const override { return true; }
        } bare;
        std::vector<SessionContentFile> out{ { "keep", "kept" } };
        EXPECT_EQ(bare.sessionContent(out), 0u);
        ASSERT_EQ(out.size(), 1u);
        EXPECT_EQ(out[0].name, "keep");
    }

    TEST(WorldSourceServiceContent, theImplementationReturnsTheFilteredListInOrder)
    {
        WorldSourceServiceImpl service;
        service.setSessionContent(
            { entry("morrowind.esm", "TES3", 0), entry("Skyrim.esm", "TES4", 44), entry("Update.esm", "TES4", 44) });
        service.setContentFilter(skyrimAccepts);
        std::vector<SessionContentFile> out;
        EXPECT_EQ(service.sessionContent(out), 2u);
        ASSERT_EQ(out.size(), 2u);
        EXPECT_EQ(out[0].name, "Skyrim.esm");
        EXPECT_EQ(out[0].path, "/x/Skyrim.esm");
        EXPECT_EQ(out[1].name, "Update.esm");
    }

    TEST(WorldSourceServiceContent, withNoFilterOrNoListThereIsNothing)
    {
        WorldSourceServiceImpl service;
        std::vector<SessionContentFile> out;
        EXPECT_EQ(service.sessionContent(out), 0u);
        service.setSessionContent({ entry("Skyrim.esm", "TES4", 44) });
        EXPECT_EQ(service.sessionContent(out), 0u) << "no content loader is asking, so no file is the pack's";
        EXPECT_TRUE(out.empty());
    }

    // The new function is the LAST virtual of WorldSourceService (a pack built against 1.0 calls its earlier slots
    // unchanged): the device that pins other contracts, a hand-written mirror called through a reinterpret_cast.
    struct MirrorService
    {
        virtual ~MirrorService() = default;
        virtual bool registerSource(void*, std::string&) = 0;
        virtual void withdrawSource(void*) = 0;
        virtual bool consumerEnabled() const = 0;
        virtual std::size_t sessionContent(std::vector<SessionContentFile>& out) const = 0; // the new slot
    };

    TEST(WorldSourceServiceContent, theNewFunctionIsTheLastSlot)
    {
        WorldSourceServiceImpl service;
        service.setConsumerEnabled(true);
        service.setSessionContent({ entry("Skyrim.esm", "TES4", 44) });
        service.setContentFilter(skyrimAccepts);
        openeden::sdk::game::WorldSourceService* base = &service;
        auto* mirror = reinterpret_cast<MirrorService*>(base);
        EXPECT_TRUE(mirror->consumerEnabled());
        std::vector<SessionContentFile> out;
        EXPECT_EQ(mirror->sessionContent(out), 1u);
    }
}
