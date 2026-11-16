// Task 0215: the Lua halves: the level mirror (the load list shows the progression's level) and Morrowind's music
// script giving way in a session whose pack owns the music.
#include "uiluafixture.hpp"

#include <filesystem>
#include <string>

namespace
{
    using UiTest::UiLuaFixture;

    struct SkyrimPresentationLua : UiLuaFixture
    {
        static std::string musicScriptPath()
        {
            return (std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files" / "data-mw" / "scripts" / "omw"
                / "music" / "music.lua")
                .generic_string();
        }

        // fakes of what music.lua requires; `pack` is the Lua value of core.skyrimse.presentation (empty: not declared)
        bool loadMusic(const std::string& pack)
        {
            const std::string fakes = R"(
                package.loaded['core.skyrimse.presentation'] = nil
                package.loaded['openmw.ambient'] = { streamMusic = function() end, stopMusic = function() end,
                                                     isMusicPlaying = function() return false end }
                package.loaded['openmw.core'] = { sound = { isEnabled = function() return true end } }
                package.loaded['openmw.self'] = {}
                package.loaded['openmw.storage'] = {
                  playerSection = function() return { get = function() return false end } end }
                package.loaded['openmw.types'] = { Actor = { isDead = function() return false end } }
                package.loaded['scripts.omw.music.helpers'] = {
                  getStoredTracksOrder = function() return {} end, initMissingPlaylistFields = function() end,
                  initTracksOrder = function() return {} end, setStoredTracksOrder = function() end,
                  getActivePlaylistByPriority = function() return nil end, isInCombat = function() return false end }
            )";
            const std::string declare
                = pack.empty() ? std::string() : "package.loaded['core.skyrimse.presentation'] = " + pack + "\n";
            return run(fakes + declare + "local f = assert(loadfile('" + musicScriptPath() + "')); result = f()");
        }
    };

    TEST_F(SkyrimPresentationLua, theMirrorWritesTheProgressionLevelWhenTheyDiffer)
    {
        ASSERT_TRUE(run(R"(
            M = require('openeden.skyrimse.level_mirror')
            stat, level, ready = 1, 7, true
            m = M.create{ ready = function() return ready end, level = function() return level end,
                          getStat = function() return stat end, setStat = function(n) stat = n end }
        )"));
        EXPECT_TRUE(get<bool>("m.update()"));
        EXPECT_EQ(get<int>("stat"), 7);
        EXPECT_FALSE(get<bool>("m.update()")); // equal now: nothing written
        EXPECT_EQ(get<int>("m.writes"), 1);
        ASSERT_TRUE(run("level = 8"));
        EXPECT_TRUE(get<bool>("m.update()"));
        EXPECT_EQ(get<int>("stat"), 8);
    }

    TEST_F(SkyrimPresentationLua, theMirrorWritesNothingWithoutDataOrForAnUnknownLevel)
    {
        ASSERT_TRUE(run(R"(
            M = require('openeden.skyrimse.level_mirror')
            stat, level, ready = 1, 7, false
            m = M.create{ ready = function() return ready end, level = function() return level end,
                          getStat = function() return stat end, setStat = function(n) stat = n end }
        )"));
        EXPECT_FALSE(get<bool>("m.update()"));
        EXPECT_EQ(get<int>("stat"), 1);
        ASSERT_TRUE(run("ready = true; level = 0"));
        EXPECT_FALSE(get<bool>("m.update()"));
        ASSERT_TRUE(run("level = nil"));
        EXPECT_FALSE(get<bool>("m.update()"));
        EXPECT_EQ(get<int>("stat"), 1);
    }

    TEST_F(SkyrimPresentationLua, morrowindsMusicScriptRunsWhenNoPackDeclaresThePresentationNamespace)
    {
        ASSERT_TRUE(loadMusic(""));
        EXPECT_TRUE(get<bool>("result.engineHandlers ~= nil and result.engineHandlers.onFrame ~= nil"));
    }

    TEST_F(SkyrimPresentationLua, morrowindsMusicScriptGivesWayWhenThePackOwnsTheMusic)
    {
        ASSERT_TRUE(loadMusic("{ owns_music = function() return true end }"));
        EXPECT_TRUE(get<bool>("result.engineHandlers == nil and result.eventHandlers == nil"));
    }

    TEST_F(SkyrimPresentationLua, morrowindsMusicScriptRunsWhenThePackDoesNotOwnTheMusic)
    {
        ASSERT_TRUE(loadMusic("{ owns_music = function() return false end }"));
        EXPECT_TRUE(get<bool>("result.engineHandlers ~= nil"));
    }
}
