// Task 0173 (stage b): the game pack rules both hosts share (components/gamesession/gamesession.hpp). Written from the
// spec and the header's comments, before the code. The reason texts and the search order are those of the game's
// ExtensionHost (apps/openmw/extensionhost.cpp), which these rules were moved out of.
#include <components/gamesession/gamesession.hpp>

#include <components/files/collections.hpp>
#include <components/worldmodel/store.hpp>

#include <openeden/game/world_source.hpp>
#include <openeden/pack_kind.hpp>

#include "../contentsource/pluginbuilder.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace GameSession;
    using openeden::extension::DiscoveredExtension;

    DiscoveredExtension discovered(const std::string& id, const std::string& kind, const std::string& folder = "x")
    {
        DiscoveredExtension item;
        item.manifest.id = id;
        item.manifest.kind = kind;
        item.directory = fs::path(folder) / id;
        return item;
    }

    // ---- searchDirectories ----

    TEST(GameSessionSearch, withGamePacksTheOrderIsLocalUserLocalPacksUserPacks)
    {
        const std::vector<fs::path> dirs = searchDirectories("/local", "/user", true);
        ASSERT_EQ(dirs.size(), 4u);
        EXPECT_EQ(dirs[0], fs::path("/local") / "extensions");
        EXPECT_EQ(dirs[1], fs::path("/user") / "extensions");
        EXPECT_EQ(dirs[2], fs::path("/local") / "game_packs");
        EXPECT_EQ(dirs[3], fs::path("/user") / "game_packs");
    }

    TEST(GameSessionSearch, withoutGamePacksOnlyTheExtensionFoldersAreSearched)
    {
        const std::vector<fs::path> dirs = searchDirectories("/local", "/user", false);
        ASSERT_EQ(dirs.size(), 2u);
        EXPECT_EQ(dirs[0], fs::path("/local") / "extensions");
        EXPECT_EQ(dirs[1], fs::path("/user") / "extensions");
    }

    TEST(GameSessionSearch, theMorrowindIdIsMorrowind)
    {
        EXPECT_EQ(sMorrowindId, "morrowind");
    }

    // ---- isEnabled ----

    TEST(GameSessionEnabled, theGamePackIsEnabledWithoutAnExtensionLine)
    {
        Rules rules;
        rules.mGamePackId = "skyrimse";
        EXPECT_TRUE(isEnabled(rules, "skyrimse"));
    }

    TEST(GameSessionEnabled, anotherIdMustBeInTheEnableList)
    {
        Rules rules;
        rules.mGamePackId = "skyrimse";
        EXPECT_FALSE(isEnabled(rules, "alpha"));
        rules.mEnabled = { "alpha" };
        EXPECT_TRUE(isEnabled(rules, "alpha"));
        EXPECT_FALSE(isEnabled(rules, "beta"));
    }

    TEST(GameSessionEnabled, withNoGamePackOnlyTheListCounts)
    {
        Rules rules;
        rules.mEnabled = { "alpha" };
        EXPECT_TRUE(isEnabled(rules, "alpha"));
        EXPECT_FALSE(isEnabled(rules, "skyrimse"));
    }

    // ---- refusedGamePackReason ----

    TEST(GameSessionReason, noGamePackChosen)
    {
        EXPECT_EQ(refusedGamePackReason(""), "no game= line chooses a game pack");
    }

    TEST(GameSessionReason, namesTheSessionsPack)
    {
        EXPECT_EQ(refusedGamePackReason("skyrimse"), "this session's game pack is 'skyrimse'");
    }

    // ---- classify ----

    TEST(GameSessionClassify, oneVerdictPerExtensionInTheSameOrder)
    {
        Rules rules;
        rules.mEnabled = { "b" };
        const auto verdicts
            = classify({ discovered("a", "feature"), discovered("b", "feature"), discovered("c", "feature") }, rules);
        ASSERT_EQ(verdicts.size(), 3u);
        EXPECT_EQ(verdicts[0].mVerdict, Verdict::NotEnabled);
        EXPECT_EQ(verdicts[1].mVerdict, Verdict::Load);
        EXPECT_EQ(verdicts[2].mVerdict, Verdict::NotEnabled);
        for (const ItemVerdict& verdict : verdicts)
        {
            EXPECT_TRUE(verdict.mReason.empty());
        }
    }

    TEST(GameSessionClassify, anEmptyListGivesAnEmptyAnswer)
    {
        EXPECT_TRUE(classify({}, Rules{}).empty());
    }

    TEST(GameSessionClassify, aSecondExtensionWithTheSameIdIsADuplicate)
    {
        Rules rules;
        rules.mEnabled = { "a" };
        const auto verdicts
            = classify({ discovered("a", "feature", "first"), discovered("a", "feature", "second") }, rules);
        ASSERT_EQ(verdicts.size(), 2u);
        EXPECT_EQ(verdicts[0].mVerdict, Verdict::Load);
        EXPECT_EQ(verdicts[1].mVerdict, Verdict::Duplicate);
    }

    TEST(GameSessionClassify, aDuplicateIsJudgedBeforeTheEnableRule)
    {
        // Not enabled, and also a duplicate: the duplicate verdict wins (the game logs it before it checks enabling).
        const auto verdicts = classify({ discovered("a", "feature"), discovered("a", "feature") }, Rules{});
        ASSERT_EQ(verdicts.size(), 2u);
        EXPECT_EQ(verdicts[0].mVerdict, Verdict::NotEnabled);
        EXPECT_EQ(verdicts[1].mVerdict, Verdict::Duplicate);
    }

    TEST(GameSessionClassify, theChosenGamePackLoadsWithoutAnExtensionLine)
    {
        Rules rules;
        rules.mGamePackId = "skyrimse";
        const auto verdicts = classify({ discovered("skyrimse", std::string(openeden::sdk::kKindGame)) }, rules);
        ASSERT_EQ(verdicts.size(), 1u);
        EXPECT_EQ(verdicts[0].mVerdict, Verdict::Load);
        EXPECT_TRUE(verdicts[0].mReason.empty());
    }

    TEST(GameSessionClassify, anotherGamePackIsRefusedWithTheSessionsReason)
    {
        Rules rules;
        rules.mGamePackId = "skyrimse";
        rules.mEnabled = { "fallout4" };
        const auto verdicts = classify({ discovered("fallout4", std::string(openeden::sdk::kKindGame)) }, rules);
        ASSERT_EQ(verdicts.size(), 1u);
        EXPECT_EQ(verdicts[0].mVerdict, Verdict::RefusedGamePack);
        EXPECT_EQ(verdicts[0].mReason, "this session's game pack is 'skyrimse'");
    }

    TEST(GameSessionClassify, aGamePackWithNoGameLineIsRefusedWithTheNoChoiceReason)
    {
        Rules rules;
        rules.mEnabled = { "skyrimse" };
        const auto verdicts = classify({ discovered("skyrimse", std::string(openeden::sdk::kKindGame)) }, rules);
        ASSERT_EQ(verdicts.size(), 1u);
        EXPECT_EQ(verdicts[0].mVerdict, Verdict::RefusedGamePack);
        EXPECT_EQ(verdicts[0].mReason, "no game= line chooses a game pack");
    }

    TEST(GameSessionClassify, aGamePackThatIsNotEnabledIsNotEnabledAndNotRefused)
    {
        Rules rules;
        rules.mGamePackId = "skyrimse";
        const auto verdicts = classify({ discovered("fallout4", std::string(openeden::sdk::kKindGame)) }, rules);
        ASSERT_EQ(verdicts.size(), 1u);
        EXPECT_EQ(verdicts[0].mVerdict, Verdict::NotEnabled);
        EXPECT_TRUE(verdicts[0].mReason.empty());
    }

    TEST(GameSessionClassify, anEnabledFeatureExtensionIsNeverRefusedAsAGamePack)
    {
        Rules rules;
        rules.mGamePackId = "skyrimse";
        rules.mEnabled = { "alpha" };
        const auto verdicts = classify({ discovered("alpha", "feature"), discovered("skyrimse", "game") }, rules);
        ASSERT_EQ(verdicts.size(), 2u);
        EXPECT_EQ(verdicts[0].mVerdict, Verdict::Load);
        EXPECT_EQ(verdicts[1].mVerdict, Verdict::Load);
    }

    // ---- collectContent ----

    class GameSessionContentTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            static std::atomic<int> counter{ 0 };
            mDir = fs::temp_directory_path()
                / ("oe_gamesession_" + std::to_string(counter++) + "_"
                    + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
            fs::remove_all(mDir);
            fs::create_directories(mDir);
        }
        void TearDown() override
        {
            std::error_code ec;
            fs::remove_all(mDir, ec);
        }

        void write(const std::string& name, const std::string& bytes)
        {
            std::ofstream out(mDir / name, std::ios::binary);
            out << bytes;
        }

        fs::path mDir;
    };

    TEST_F(GameSessionContentTest, readsTheHeaderTagOfEachPluginInTheListsOrder)
    {
        write("Skyrim.esm", ContentTests::tes4Header(1.7f, 1, "a", "d", 0, {}).str());
        write("Old.esm", ContentTests::tes3Header(1.3f, 1, "a", "d", 0, {}).str());
        const Files::Collections collections({ mDir });
        const auto collected = collectContent({ "Old.esm", "Skyrim.esm" }, collections);
        ASSERT_EQ(collected.entries.size(), 2u);
        EXPECT_EQ(collected.entries[0].name, "Old.esm");
        EXPECT_EQ(collected.entries[0].gameTag, "TES3");
        EXPECT_EQ(collected.entries[1].name, "Skyrim.esm");
        EXPECT_EQ(collected.entries[1].gameTag, "TES4");
        EXPECT_EQ(collected.entries[1].formatVersion, 44u);
        EXPECT_FALSE(collected.entries[1].path.empty());
        EXPECT_TRUE(collected.missing.empty());
        EXPECT_TRUE(collected.unreadable.empty());
    }

    TEST_F(GameSessionContentTest, aNameNotInAnyFolderIsMissingAndAnUnreadablePluginIsNamed)
    {
        write("Skyrim.esm", ContentTests::tes4Header(1.7f, 1, "a", "d", 0, {}).str());
        write("Broken.esp", "not a plugin at all");
        const Files::Collections collections({ mDir });
        const auto collected = collectContent({ "Skyrim.esm", "Gone.esp", "Broken.esp" }, collections);
        ASSERT_EQ(collected.entries.size(), 1u);
        EXPECT_EQ(collected.entries[0].name, "Skyrim.esm");
        ASSERT_EQ(collected.missing.size(), 1u);
        EXPECT_EQ(collected.missing[0], "Gone.esp");
        ASSERT_EQ(collected.unreadable.size(), 1u);
        EXPECT_NE(collected.unreadable[0].find("Broken.esp"), std::string::npos) << collected.unreadable[0];
    }

    TEST_F(GameSessionContentTest, namesWithAnotherExtensionAreSkippedWithoutAMention)
    {
        write("builtin.omwscripts", "x");
        const Files::Collections collections({ mDir });
        const auto collected = collectContent({ "builtin.omwscripts", "other.omwscripts" }, collections);
        EXPECT_TRUE(collected.entries.empty());
        EXPECT_TRUE(collected.missing.empty());
        EXPECT_TRUE(collected.unreadable.empty());
    }

    TEST_F(GameSessionContentTest, theLaterFolderWinsLikeTheGamesCollections)
    {
        fs::create_directories(mDir / "a");
        fs::create_directories(mDir / "b");
        std::ofstream(mDir / "a" / "P.esm", std::ios::binary)
            << ContentTests::tes3Header(1.3f, 1, "a", "d", 0, {}).str();
        std::ofstream(mDir / "b" / "P.esm", std::ios::binary)
            << ContentTests::tes4Header(1.7f, 1, "a", "d", 0, {}).str();
        const Files::Collections collections({ mDir / "a", mDir / "b" });
        const auto collected = collectContent({ "P.esm" }, collections);
        ASSERT_EQ(collected.entries.size(), 1u);
        EXPECT_EQ(collected.entries[0].gameTag, "TES4");
    }

    // ---- StoreListener ----

    class NamedSource final : public openeden::sdk::game::WorldCellSource
    {
    public:
        explicit NamedSource(std::string game)
            : mGame(std::move(game))
        {
        }
        std::string_view gameId() const override { return mGame; }
        std::size_t cellCount() const override { return 0; }
        std::size_t listCells(std::size_t, std::size_t, std::vector<openeden::sdk::game::WorldId>&) const override
        {
            return 0;
        }
        bool buildCell(openeden::sdk::game::WorldId, openeden::sdk::game::WorldCell&, std::string& error) override
        {
            error = "none";
            return false;
        }

    private:
        std::string mGame;
    };

    TEST(GameSessionStoreListener, aRegisteredSourceGoesIntoTheStore)
    {
        NeutralWorld::NeutralWorldStore store;
        StoreListener listener(store);
        NamedSource source("fakegame");
        std::string error;
        EXPECT_TRUE(listener.sourceRegistered(source, error)) << error;
        EXPECT_EQ(store.sourceCount(), 1u);
        EXPECT_EQ(store.source("fakegame"), &source);
    }

    TEST(GameSessionStoreListener, aSecondSourceWithTheSameGameIdIsRefusedWithAReason)
    {
        NeutralWorld::NeutralWorldStore store;
        StoreListener listener(store);
        NamedSource first("fakegame");
        NamedSource second("fakegame");
        std::string error;
        ASSERT_TRUE(listener.sourceRegistered(first, error));
        error.clear();
        EXPECT_FALSE(listener.sourceRegistered(second, error));
        EXPECT_FALSE(error.empty());
        EXPECT_EQ(store.sourceCount(), 1u);
        EXPECT_EQ(store.source("fakegame"), &first);
    }

    TEST(GameSessionStoreListener, aWithdrawnSourceLeavesTheStoreByGameId)
    {
        NeutralWorld::NeutralWorldStore store;
        StoreListener listener(store);
        NamedSource source("fakegame");
        std::string error;
        ASSERT_TRUE(listener.sourceRegistered(source, error));
        listener.sourceWithdrawn("fakegame");
        EXPECT_EQ(store.sourceCount(), 0u);
        EXPECT_EQ(store.source("fakegame"), nullptr);
    }

    TEST(GameSessionStoreListener, withdrawingAnUnknownGameDoesNothing)
    {
        NeutralWorld::NeutralWorldStore store;
        StoreListener listener(store);
        NamedSource source("fakegame");
        std::string error;
        ASSERT_TRUE(listener.sourceRegistered(source, error));
        listener.sourceWithdrawn("another");
        EXPECT_EQ(store.sourceCount(), 1u);
    }
}
