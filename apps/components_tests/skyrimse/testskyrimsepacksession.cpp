// Task 0107 (neutral world model stage N3a-2b): the Skyrim SE game pack builds its world view from the session's
// content list and registers its source over the real view. Written from the spec first. Generated plugins only; the
// host is the real HostImpl with a loose-file mount standing in for the engine's VFS. No game data.
#include <components/extension/host_impl.hpp>
#include <components/extension/lifecycle_manager.hpp>
#include <components/extension/manifest.hpp>
#include <components/extension/session_content.hpp>
#include <components/worldmodel/store.hpp>
#include <openeden/game/world_source_service.hpp>
#include <openeden/logging.hpp>

#include <game_skyrimse/actor_table.hpp>
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/skyrimse_game_pack.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"
#include "../extension/sessionheaderreader.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using openeden::extension::ExtensionManifest;
    using openeden::extension::HostImpl;
    using openeden::extension::LifecycleManager;
    using openeden::extension::WorldSourceServiceImpl;
    namespace fs = std::filesystem;

    constexpr std::uint32_t kGrupTop = 0, kGrupInteriorBlock = 2, kGrupInteriorSub = 3, kGrupCellChildren = 6,
                            kGrupTemporary = 9;

    ExtensionManifest packManifest()
    {
        ExtensionManifest m;
        m.version = 2;
        m.id = "skyrimse";
        m.name = "Skyrim SE";
        m.extensionVersion = "1.0.0";
        m.kind = "game";
        m.sdkVersion = openeden::sdk::kSdkVersion;
        m.toolchain = "test-toolchain";
        return m;
    }

    struct TempDir
    {
        fs::path mPath;
        TempDir()
        {
            static int counter = 0;
            mPath = fs::temp_directory_path()
                / ("oe_skyrim_packsession_"
                    + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_"
                    + std::to_string(counter++));
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
            fs::create_directories(p.parent_path());
            std::ofstream out(p, std::ios::binary);
            const std::string s = bytes.str();
            out.write(s.data(), static_cast<std::streamsize>(s.size()));
            return p;
        }
    };

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    // CELL top group > block > sub-block > an interior cell record with one (empty) temporary children group.
    Bytes interiorCell(std::uint32_t id, const std::string& editorId, bool localisedName, std::uint32_t nameId,
        const std::string& name)
    {
        Bytes subs;
        subs.append(edid(editorId));
        if (localisedName)
            subs.append(sub4("FULL", Bytes().u32(nameId)));
        else
            subs.append(sub4("FULL", Bytes().zstring(name)));
        subs.append(sub4("DATA", Bytes().u16(0x0001)));
        Bytes sub = record4("CELL", 0, id, subs);
        sub.append(group4(id, kGrupCellChildren, group4(id, kGrupTemporary, Bytes())));
        return group4(Content::fourCC("CELL"), kGrupTop,
            group4(id % 10, kGrupInteriorBlock, group4((id / 10) % 10, kGrupInteriorSub, sub)));
    }

    Bytes skyrimPlugin(std::uint32_t headerFlags, const Bytes& body)
    {
        Bytes bytes = tes4Header(1.7f, headerFlags, "a", "b", 1, {}, 24, 44);
        bytes.append(body);
        return bytes;
    }

    // A .STRINGS table: count, data size, (id, offset) directory, zero-terminated strings.
    Bytes stringsTable(const std::vector<std::pair<std::uint32_t, std::string>>& entries)
    {
        Bytes data;
        Bytes directory;
        for (const auto& [id, text] : entries)
        {
            directory.u32(id).u32(static_cast<std::uint32_t>(data.size()));
            data.zstring(text);
        }
        Bytes all;
        all.u32(static_cast<std::uint32_t>(entries.size())).u32(static_cast<std::uint32_t>(data.size()));
        all.append(directory).append(data);
        return all;
    }

    struct StoreListener final : WorldSourceServiceImpl::Listener
    {
        bool sourceRegistered(openeden::sdk::game::WorldCellSource& source, std::string& error) override
        {
            return mStore.addSource(source, error);
        }
        void sourceWithdrawn(std::string_view gameId) override { mStore.removeSource(gameId); }
        NeutralWorld::NeutralWorldStore mStore;
    };

    struct CaptureSink final : openeden::sdk::LogSink
    {
        void log(std::string_view, openeden::sdk::LogLevel level, std::string_view message) override
        {
            const std::lock_guard lock(mMutex);
            mLines.push_back({ level, std::string(message) });
        }
        bool has(openeden::sdk::LogLevel level, const std::string& needle) const
        {
            const std::lock_guard lock(mMutex);
            for (const auto& [l, text] : mLines)
                if (l == level && text.find(needle) != std::string::npos)
                    return true;
            return false;
        }
        mutable std::mutex mMutex;
        std::vector<std::pair<openeden::sdk::LogLevel, std::string>> mLines;
    };

    // The host as the engine makes it: sources consumed, the content filter pointed at the registered loaders, the
    // session's content list read from files, a mount standing in for the VFS.
    struct Session
    {
        TempDir dir;
        HostImpl host;
        StoreListener listener;
        std::shared_ptr<CaptureSink> log = std::make_shared<CaptureSink>();

        Session()
        {
            host.loggingService().addSink(log);
            host.worldSources().setConsumerEnabled(true);
            host.worldSources().setListener(&listener);
            host.worldSources().setContentFilter(
                [this](const openeden::sdk::ContentHeader& h) { return host.content().findLoader(h) != nullptr; });
            host.vfsService().mount("", dir.mPath);
        }

        void setContent(const std::vector<std::string>& names)
        {
            auto collected = openeden::extension::collectSessionContent(
                names, [this](const std::string& name) -> std::optional<fs::path> {
                    const fs::path p = dir.mPath / name;
                    if (!fs::exists(p))
                        return std::nullopt;
                    return p;
                },
                SessionContentTest::readHeader);
            host.worldSources().setSessionContent(std::move(collected.entries));
        }
    };

    TEST(SkyrimSePackSession, onlyTheSkyrimPluginsOfTheListReachTheViewAndTheSourceIsRegisteredOverIt)
    {
        Session s;
        s.dir.write("morrowind.esm", tes3Header(1.3f, 1, "a", "b", 0, {}));
        s.dir.write(
            "base.esm", skyrimPlugin(Content::sRecMaster, interiorCell(0x900, "TestHall", false, 0, "The Hall")));
        s.dir.write(
            "second.esm", skyrimPlugin(Content::sRecMaster, interiorCell(0x910, "SecondHall", false, 0, "Two")));
        s.setContent({ "builtin.omwscripts", "morrowind.esm", "base.esm", "second.esm" });

        LifecycleManager manager(s.host);
        manager.addExtension(packManifest(), createSkyrimGamePack());
        ASSERT_TRUE(manager.initialize());
        ASSERT_FALSE(manager.hasSessionError()) << manager.getSessionError();
        EXPECT_EQ(s.host.worldSources().sourceCount(), 1u);

        // The cells of both Skyrim plugins are in the store, the Morrowind plugin is not an input
        EXPECT_NE(s.listener.mStore.findCellByEditorId("skyrimse", "TestHall"), openeden::sdk::game::kNoWorldId);
        EXPECT_NE(s.listener.mStore.findCellByEditorId("skyrimse", "SecondHall"), openeden::sdk::game::kNoWorldId);
        EXPECT_TRUE(s.log->has(openeden::sdk::LogLevel::Info, "2 of 2 plugins, 2 cells"))
            << "an aggregate line says what the view was built from";
        EXPECT_FALSE(s.log->has(openeden::sdk::LogLevel::Warning, "plugin left out"));
        manager.shutdown();
    }

    TEST(SkyrimSePackSession, theLoadOrderIsTheListOrder)
    {
        Session s;
        // Both plugins define the same cell id; the later one in the list wins its name
        s.dir.write("a.esm", skyrimPlugin(Content::sRecMaster, interiorCell(0x900, "Hall", false, 0, "From A")));
        s.dir.write("b.esm", skyrimPlugin(Content::sRecMaster, interiorCell(0x900, "Hall", false, 0, "From B")));
        s.setContent({ "a.esm", "b.esm" });
        LifecycleManager manager(s.host);
        manager.addExtension(packManifest(), createSkyrimGamePack());
        ASSERT_TRUE(manager.initialize());
        const auto id = s.listener.mStore.findCellByEditorId("skyrimse", "Hall");
        ASSERT_NE(id, openeden::sdk::game::kNoWorldId);
        std::string error;
        const auto* cell = s.listener.mStore.cell("skyrimse", id, error);
        ASSERT_NE(cell, nullptr) << error;
        EXPECT_EQ(cell->name, "From B");
        manager.shutdown();
    }

    TEST(SkyrimSePackSession, aLocalisedNameResolvesThroughTheHostVfsInTheDefaultLanguage)
    {
        Session s;
        s.dir.write("loc.esm",
            skyrimPlugin(Content::sRecMaster | Content::sRecLocalized, interiorCell(0x900, "Named", true, 77, "")));
        s.dir.write("strings/loc_english.strings", stringsTable({ { 77, "Localised Hall" } }));
        s.dir.write("strings/loc_german.strings", stringsTable({ { 77, "Lokale Halle" } }));
        s.setContent({ "loc.esm" });
        LifecycleManager manager(s.host);
        manager.addExtension(packManifest(), createSkyrimGamePack());
        ASSERT_TRUE(manager.initialize());
        const auto id = s.listener.mStore.findCellByEditorId("skyrimse", "Named");
        ASSERT_NE(id, openeden::sdk::game::kNoWorldId);
        std::string error;
        const auto* cell = s.listener.mStore.cell("skyrimse", id, error);
        ASSERT_NE(cell, nullptr) << error;
        EXPECT_EQ(cell->name, "Localised Hall");
        manager.shutdown();
    }

    TEST(SkyrimSePackSession, theLanguageSettingChoosesTheTable)
    {
        Session s;
        s.dir.write("loc.esm",
            skyrimPlugin(Content::sRecMaster | Content::sRecLocalized, interiorCell(0x900, "Named", true, 77, "")));
        s.dir.write("strings/loc_english.strings", stringsTable({ { 77, "Localised Hall" } }));
        s.dir.write("strings/loc_german.strings", stringsTable({ { 77, "Lokale Halle" } }));
        s.host.settingsService().setString("skyrimse", std::string(kStringTableLanguageKey), "German");
        s.setContent({ "loc.esm" });
        LifecycleManager manager(s.host);
        manager.addExtension(packManifest(), createSkyrimGamePack());
        ASSERT_TRUE(manager.initialize());
        const auto id = s.listener.mStore.findCellByEditorId("skyrimse", "Named");
        ASSERT_NE(id, openeden::sdk::game::kNoWorldId);
        std::string error;
        const auto* cell = s.listener.mStore.cell("skyrimse", id, error);
        ASSERT_NE(cell, nullptr) << error;
        EXPECT_EQ(cell->name, "Lokale Halle");
        manager.shutdown();
    }

    TEST(SkyrimSePackSession, aBadLanguageSettingFallsBackToEnglishWithAWarning)
    {
        for (const std::string bad : { "../x", "e", "english1", "" })
        {
            Session s;
            s.dir.write("loc.esm",
                skyrimPlugin(Content::sRecMaster | Content::sRecLocalized, interiorCell(0x900, "Named", true, 77, "")));
            s.dir.write("strings/loc_english.strings", stringsTable({ { 77, "Localised Hall" } }));
            s.host.settingsService().setString("skyrimse", std::string(kStringTableLanguageKey), bad);
            s.setContent({ "loc.esm" });
            LifecycleManager manager(s.host);
            manager.addExtension(packManifest(), createSkyrimGamePack());
            ASSERT_TRUE(manager.initialize()) << bad;
            const auto id = s.listener.mStore.findCellByEditorId("skyrimse", "Named");
            ASSERT_NE(id, openeden::sdk::game::kNoWorldId) << bad;
            std::string error;
            const auto* cell = s.listener.mStore.cell("skyrimse", id, error);
            ASSERT_NE(cell, nullptr) << error;
            EXPECT_EQ(cell->name, "Localised Hall") << bad;
            if (!bad.empty()) // an empty value is "not set", not a mistake
            {
                EXPECT_TRUE(s.log->has(openeden::sdk::LogLevel::Warning, "string table language")) << bad;
            }
            manager.shutdown();
        }
    }

    TEST(SkyrimSePackSession, aPluginTheViewRefusesIsNamedAndLeftOut)
    {
        Session s;
        s.dir.write("good.esm", skyrimPlugin(Content::sRecMaster, interiorCell(0x900, "Good", false, 0, "G")));
        // form version 43 is accepted by the content probe but a header with another version is refused by the view?
        // A TES4 plugin of an older form version is not given to the pack at all; here the file is damaged after the
        // list was read, so that the view cannot open it.
        s.dir.write("gone.esm", skyrimPlugin(Content::sRecMaster, interiorCell(0x910, "Gone", false, 0, "X")));
        s.setContent({ "good.esm", "gone.esm" });
        fs::remove(s.dir.mPath / "gone.esm");
        LifecycleManager manager(s.host);
        manager.addExtension(packManifest(), createSkyrimGamePack());
        ASSERT_TRUE(manager.initialize());
        EXPECT_FALSE(manager.hasSessionError());
        EXPECT_NE(s.listener.mStore.findCellByEditorId("skyrimse", "Good"), openeden::sdk::game::kNoWorldId);
        EXPECT_EQ(s.listener.mStore.findCellByEditorId("skyrimse", "Gone"), openeden::sdk::game::kNoWorldId);
        EXPECT_TRUE(s.log->has(openeden::sdk::LogLevel::Warning, "gone.esm"));
        EXPECT_TRUE(s.log->has(openeden::sdk::LogLevel::Info, "1 of 2 plugins"));
        manager.shutdown();
    }

    TEST(SkyrimSePackSession, anEmptyContentListLeavesTheViewEmptyAsBefore)
    {
        Session s;
        LifecycleManager manager(s.host);
        manager.addExtension(packManifest(), createSkyrimGamePack());
        ASSERT_TRUE(manager.initialize());
        EXPECT_FALSE(manager.hasSessionError());
        EXPECT_EQ(s.host.worldSources().sourceCount(), 1u);
        EXPECT_EQ(s.listener.mStore.findCellByEditorId("skyrimse", "Anything"), openeden::sdk::game::kNoWorldId);
        EXPECT_FALSE(s.log->has(openeden::sdk::LogLevel::Info, "world view built"));
        manager.shutdown();
    }

    // With the setting off the pack asks for nothing: a host that does not consume sources is never asked for its
    // content, and the old refusal stays.
    TEST(SkyrimSePackSession, aHostThatDoesNotConsumeSourcesIsNeverAskedForContent)
    {
        TempDir dir;
        HostImpl host;
        dir.write("base.esm", skyrimPlugin(Content::sRecMaster, interiorCell(0x900, "TestHall", false, 0, "The Hall")));
        auto collected = openeden::extension::collectSessionContent(
            { "base.esm" }, [&](const std::string& name) -> std::optional<fs::path> { return dir.mPath / name; },
            SessionContentTest::readHeader);
        int asked = 0;
        host.worldSources().setSessionContent(std::move(collected.entries));
        host.worldSources().setContentFilter([&asked](const openeden::sdk::ContentHeader&) {
            ++asked;
            return true;
        });
        LifecycleManager manager(host);
        manager.addExtension(packManifest(), createSkyrimGamePack());
        manager.initialize();
        EXPECT_TRUE(manager.hasSessionError());
        EXPECT_EQ(asked, 0);
        manager.shutdown();
    }

    // ---- the language rule ----

    TEST(SkyrimSeLanguage, theRuleOfTheSetting)
    {
        bool usedDefault = true;
        EXPECT_EQ(sanitiseStringTableLanguage("german", usedDefault), "german");
        EXPECT_FALSE(usedDefault);
        EXPECT_EQ(sanitiseStringTableLanguage("English", usedDefault), "english");
        EXPECT_FALSE(usedDefault);
        EXPECT_EQ(sanitiseStringTableLanguage("", usedDefault), "english");
        EXPECT_FALSE(usedDefault);
        const std::vector<std::string> badValues{ "..", "../x", "a/b", "a\\b", "x", "english ", "en1",
            std::string(33, 'a'), std::string("a\0b", 3) };
        for (const std::string& bad : badValues)
        {
            EXPECT_EQ(sanitiseStringTableLanguage(bad, usedDefault), "english") << bad;
            EXPECT_TRUE(usedDefault) << bad;
        }
        EXPECT_EQ(sanitiseStringTableLanguage(std::string(32, 'z'), usedDefault), std::string(32, 'z'));
        EXPECT_FALSE(usedDefault);
    }

    // ---- the VFS adapter and the view's configure ----

    TEST(SkyrimSeVfsFileSource, readsThroughTheServiceWithUnifiedSeparatorsAndTreatsEmptyAsMissing)
    {
        TempDir dir;
        HostImpl host;
        host.vfsService().mount("", dir.mPath);
        dir.write("strings/a_english.strings", stringsTable({ { 1, "x" } }));
        dir.write("strings/empty_english.strings", Bytes());
        VfsFileSource source(host.vfsService());
        std::string bytes;
        EXPECT_TRUE(source.read("strings\\a_english.strings", bytes));
        EXPECT_FALSE(bytes.empty());
        EXPECT_FALSE(source.read("strings\\nothing.strings", bytes));
        EXPECT_FALSE(source.read("strings\\empty_english.strings", bytes));
    }

    TEST(SkyrimSeWorldViewConfigure, isRefusedOnceAPluginWasAdded)
    {
        TempDir dir;
        const fs::path p
            = dir.write("a.esm", skyrimPlugin(Content::sRecMaster, interiorCell(0x900, "Hall", false, 0, "H")));
        SkyrimWorldView view;
        SkyrimWorldView::Options options;
        options.mLanguage = "german";
        EXPECT_TRUE(view.configure(options, nullptr));
        EXPECT_EQ(view.language(), "german");
        std::string error;
        ASSERT_TRUE(view.addPlugin(p, error)) << error;
        options.mLanguage = "french";
        EXPECT_FALSE(view.configure(options, nullptr));
        EXPECT_EQ(view.language(), "german");
    }

    // Task 0135 stage S3: the pack setting `actor test walk` reaches the registered source's ActorRules.
    TEST(SkyrimSePackSession, theActorTestWalkSettingReachesTheRegisteredSourcesActorRules)
    {
        for (const std::string value : { "", "Bob" })
        {
            Session s;
            s.dir.write("base.esm", skyrimPlugin(Content::sRecMaster, interiorCell(0x900, "TestHall", false, 0, "H")));
            if (!value.empty())
                s.host.settingsService().setString("skyrimse", std::string(kActorTestWalkKey), value);
            s.setContent({ "base.esm" });
            LifecycleManager manager(s.host);
            manager.addExtension(packManifest(), createSkyrimGamePack());
            ASSERT_TRUE(manager.initialize()) << value;
            auto* rules = dynamic_cast<openeden::sdk::game::ActorRules*>(s.listener.mStore.source("skyrimse"));
            ASSERT_NE(rules, nullptr) << value;
            struct Writer final : openeden::sdk::game::ActorRequestWriter
            {
                bool setMovement(openeden::sdk::game::ActorHandle, float, float) override
                {
                    ++mCalls;
                    return true;
                }
                int mCalls = 0;
            } writer;
            openeden::sdk::game::ActorInfo info;
            info.handle = { 5, 1 };
            info.base = 1;
            info.editorId = "bob";
            rules->onActorAdded(info);
            rules->update(0.1f, writer);
            EXPECT_EQ(writer.mCalls, value.empty() ? 0 : 1) << value;
            manager.shutdown();
        }
    }
}
