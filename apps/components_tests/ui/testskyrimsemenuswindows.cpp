// Task 0207: the Skyrim SE main menu and pause menu (files/data/openeden/skyrimse/menus.lua) over the headless UI
// backend and fakes of the engine's `openmw.menu` package, the engine-side switches and the pack's
// `core.skyrimse.menus`.
#include "uiluafixture.hpp"

#include <string>
#include <vector>

namespace
{
    using Ui::ElementId;
    using Ui::EventKind;
    using UiTest::UiLuaFixture;

    struct SkyrimMenusLua : UiLuaFixture
    {
        void SetUp() override
        {
            ASSERT_TRUE(run(R"(
                ui = require('openeden.ui')
                M = require('openeden.skyrimse.menus')
                state = 0 -- NoGame
                saveWrites = true -- the fake engine writes the save (false: it refuses, a session with no neutral saves)
                calls = {}
                saves = {
                  Hero = {
                    ['Quicksave.omwsave'] = { description = 'quick', playerName = 'Dovah', playerLevel = 7,
                      playerCell = 'Whiterun', timePlayed = 3725, creationTime = 1000,
                      contentFiles = { 'skyrim.esm', 'update.esm' } },
                    ['Save1.omwsave'] = { description = 'older', playerName = 'Dovah', playerLevel = 3,
                      playerCell = 'Riverwood', timePlayed = 100, creationTime = 500,
                      contentFiles = { 'skyrim.esm' } },
                    ['Morrowind.omwsave'] = { description = 'mw', playerName = 'Nerevar', playerLevel = 9,
                      playerCell = 'Balmora', timePlayed = 10, creationTime = 9000,
                      contentFiles = { 'morrowind.esm' } },
                  },
                }
                menu = {
                  STATE = { NoGame = 0, Running = 1, Ended = 2 },
                  getState = function() return state end,
                  getAllSaves = function() return saves end,
                  loadGame = function(d, s) calls[#calls + 1] = 'load:' .. d .. '/' .. s end,
                  saveGame = function(desc, slot)
                    calls[#calls + 1] = 'save:' .. desc .. '|' .. tostring(slot)
                    if saveWrites then
                      saves.Hero[slot or ('New' .. #calls .. '.omwsave')] = { description = desc, playerName = 'Dovah',
                        playerLevel = 7, playerCell = 'Whiterun', timePlayed = 1, creationTime = 20000 + #calls,
                        contentFiles = { 'skyrim.esm' } }
                    end
                  end,
                  newGame = function() calls[#calls + 1] = 'new' end,
                  quit = function() calls[#calls + 1] = 'quit' end,
                }
                eng = { replaced = false, replacedCalls = 0, open = false, closed = 0, settings = false,
                        toggled = 0, tip = nil, tips = {} }
                engine = {
                  setReplaced = function(b) eng.replaced = b; eng.replacedCalls = eng.replacedCalls + 1 end,
                  mainMenuOpen = function() return eng.open end,
                  closeMainMenu = function() eng.closed = eng.closed + 1; eng.open = false end,
                  settingsVisible = function() return eng.settings end,
                  toggleSettings = function() eng.toggled = eng.toggled + 1 end,
                  setTip = function(t) eng.tip = t; eng.tips[#eng.tips + 1] = t end,
                }
                pack = { ready = false }
                menus = { ready = function() return pack.ready end, tip = function(seed) return 'tip' .. seed end }
                clock = 5000
                view = M.create{ ui = ui, menu = menu, engine = engine, menus = menus,
                                 date = function(t) return 'D' .. t end, now = function() return clock end }
            )"));
        }

        void frame() { ASSERT_TRUE(run("view.update(); ui.update()")); }

        /// The engine's menu mode closes and opens again (a new visit of the menu).
        void reopen()
        {
            ASSERT_TRUE(run("eng.open = false"));
            frame();
            ASSERT_TRUE(run("eng.open = true"));
            frame();
        }

        void openMenu(int menuState)
        {
            ASSERT_TRUE(run("state = " + std::to_string(menuState) + "; eng.open = true"));
            frame();
        }

        std::size_t documents() const
        {
            std::size_t n = 0;
            for (const auto& [id, node] : backend.nodes())
                if (node.mDocument && node.mName == "skyrim-menu")
                    ++n;
            return n;
        }

        std::vector<std::string> buttonTexts() const
        {
            std::vector<std::string> out;
            for (const ElementId id : withClass("button"))
                out.push_back(backend.node(id)->mText);
            return out;
        }

        bool hasButton(const std::string& text) const
        {
            for (const std::string& t : buttonTexts())
                if (t == text)
                    return true;
            return false;
        }

        bool click(const std::string& text)
        {
            for (const ElementId id : withClass("button"))
                if (backend.node(id)->mText == text)
                {
                    if (!backend.emit(id, EventKind::Click))
                        return false;
                    frame(); // the handler runs in ui.update
                    frame(); // the page is rebuilt in the next update
                    return true;
                }
            ADD_FAILURE() << "no button " << text;
            return false;
        }

        bool anyTextContains(const std::string& part) const
        {
            for (const auto& [id, node] : backend.nodes())
                if (node.mText.find(part) != std::string::npos)
                    return true;
            return false;
        }

        std::string calls() { return get<std::string>("table.concat(calls, ',')"); }
    };

    // ---- the helpers ---------------------------------------------------------------------------------------------

    TEST_F(SkyrimMenusLua, thePlayedTimeIsHoursAndMinutes)
    {
        EXPECT_EQ(get<std::string>("M.playedText(3725)"), "1:02");
        EXPECT_EQ(get<std::string>("M.playedText(59)"), "0:00");
        EXPECT_EQ(get<std::string>("M.playedText(36000)"), "10:00");
        EXPECT_EQ(get<std::string>("M.playedText(-5)"), "0:00");
        EXPECT_EQ(get<std::string>("M.playedText(nil)"), "0:00");
    }

    // ---- the replacement switch ----------------------------------------------------------------------------------

    TEST_F(SkyrimMenusLua, theEnginesMenuIsReplacedOnceAtTheFirstUpdate)
    {
        EXPECT_EQ(get<int>("eng.replacedCalls"), 0);
        frame();
        frame();
        EXPECT_TRUE(get<bool>("eng.replaced"));
        EXPECT_EQ(get<int>("eng.replacedCalls"), 1);
    }

    TEST_F(SkyrimMenusLua, noWindowWhileTheEnginesMainMenuIsNotOpen)
    {
        frame();
        EXPECT_EQ(documents(), 0u);
        EXPECT_FALSE(get<bool>("view.isOpen()"));
    }

    // ---- the main menu -------------------------------------------------------------------------------------------

    TEST_F(SkyrimMenusLua, theMainMenuOffersNewGameLoadSettingsAndQuitAndContinueWhenASaveExists)
    {
        openMenu(0);
        EXPECT_EQ(documents(), 1u);
        EXPECT_EQ(get<std::string>("view.page()"), "main");
        EXPECT_EQ(buttonTexts(), (std::vector<std::string>{ "Continue", "New Game", "Load Game", "Settings", "Quit" }));
    }

    TEST_F(SkyrimMenusLua, theMainMenuCoversTheScreenOpaquelyAndThePauseMenuDoesNot)
    {
        // with no game running the engine's own menu background is gone: this page must hide what lies behind
        openMenu(0);
        std::string background;
        for (const auto& [id, node] : backend.nodes())
            if (node.mDocument && node.mName == "skyrim-menu")
            {
                const auto it = node.mProperties.find("background-color");
                background = it == node.mProperties.end() ? std::string() : it->second;
            }
        EXPECT_EQ(background, "#000000");
        // a page opened from the main menu (the load list) is just as opaque
        ASSERT_TRUE(click("Load Game"));
        background.clear();
        for (const auto& [id, node] : backend.nodes())
        {
            if (node.mDocument && node.mName == "skyrim-menu")
            {
                const auto it = node.mProperties.find("background-color");
                background = it == node.mProperties.end() ? std::string() : it->second;
            }
        }
        EXPECT_EQ(background, "#000000");
        ASSERT_TRUE(click("Back"));
        ASSERT_TRUE(run("state = 1"));
        frame();
        for (const auto& [id, node] : backend.nodes())
        {
            if (node.mDocument && node.mName == "skyrim-menu")
            {
                EXPECT_TRUE(node.mProperties.find("background-color") == node.mProperties.end()
                    || node.mProperties.find("background-color")->second != "#000000");
            }
        }
    }

    TEST_F(SkyrimMenusLua, continueIsMissingWithNoSavesOfThisGame)
    {
        ASSERT_TRUE(run("saves = { Hero = { ['Morrowind.omwsave'] = saves.Hero['Morrowind.omwsave'] } }"));
        openMenu(0);
        EXPECT_FALSE(hasButton("Continue"));
        EXPECT_TRUE(hasButton("New Game"));
    }

    TEST_F(SkyrimMenusLua, continueLoadsTheNewestSaveOfThisGameAndNotAMorrowindOne)
    {
        openMenu(0);
        ASSERT_TRUE(click("Continue"));
        EXPECT_EQ(calls(), "load:Hero/Quicksave.omwsave");
    }

    TEST_F(SkyrimMenusLua, newGameStartsOneAndQuitLeavesAtOnceFromTheMainMenu)
    {
        openMenu(0);
        ASSERT_TRUE(click("New Game"));
        EXPECT_EQ(calls(), "new");
        reopen();
        ASSERT_TRUE(click("Quit"));
        EXPECT_EQ(calls(), "new,quit");
    }

    TEST_F(SkyrimMenusLua, anEndedGameShowsTheMainMenuNotThePauseMenu)
    {
        openMenu(2);
        EXPECT_EQ(get<std::string>("view.page()"), "main");
        EXPECT_FALSE(hasButton("Resume"));
    }

    // ---- the pause menu ------------------------------------------------------------------------------------------

    TEST_F(SkyrimMenusLua, thePauseMenuOffersResumeSaveLoadSettingsAndQuit)
    {
        openMenu(1);
        EXPECT_EQ(get<std::string>("view.page()"), "pause");
        EXPECT_EQ(buttonTexts(), (std::vector<std::string>{ "Resume", "Save Game", "Load Game", "Settings", "Quit" }));
    }

    TEST_F(SkyrimMenusLua, resumeClosesTheEnginesMainMenu)
    {
        openMenu(1);
        ASSERT_TRUE(click("Resume"));
        EXPECT_EQ(get<int>("eng.closed"), 1);
        frame();
        EXPECT_EQ(documents(), 0u);
    }

    TEST_F(SkyrimMenusLua, quitFromThePauseMenuAsksFirstAndNoGoesBack)
    {
        openMenu(1);
        ASSERT_TRUE(click("Quit"));
        EXPECT_EQ(get<std::string>("view.page()"), "confirm");
        EXPECT_TRUE(hasButton("Yes"));
        EXPECT_TRUE(hasButton("No"));
        EXPECT_EQ(calls(), "");
        ASSERT_TRUE(click("No"));
        EXPECT_EQ(get<std::string>("view.page()"), "pause");
        EXPECT_EQ(calls(), "");
    }

    TEST_F(SkyrimMenusLua, quitFromThePauseMenuAfterYesLeaves)
    {
        openMenu(1);
        ASSERT_TRUE(click("Quit"));
        ASSERT_TRUE(click("Yes"));
        EXPECT_EQ(calls(), "quit");
    }

    TEST_F(SkyrimMenusLua, closingTheEnginesMenuTakesTheWindowDownAndForgetsTheSubPage)
    {
        openMenu(1);
        ASSERT_TRUE(click("Load Game"));
        EXPECT_EQ(get<std::string>("view.page()"), "load");
        ASSERT_TRUE(run("eng.open = false"));
        frame();
        EXPECT_EQ(documents(), 0u);
        ASSERT_TRUE(run("eng.open = true"));
        frame();
        EXPECT_EQ(get<std::string>("view.page()"), "pause");
    }

    // ---- loading ------------------------------------------------------------------------------------------------

    TEST_F(SkyrimMenusLua, theLoadPageListsOnlyThisGamesSavesNewestFirstWithTheirDetails)
    {
        openMenu(1);
        ASSERT_TRUE(click("Load Game"));
        EXPECT_EQ(get<std::string>("view.page()"), "load");
        EXPECT_TRUE(anyTextContains("Dovah"));
        EXPECT_TRUE(anyTextContains("Level 7"));
        EXPECT_TRUE(anyTextContains("Whiterun"));
        EXPECT_TRUE(anyTextContains("1:02"));
        EXPECT_TRUE(anyTextContains("D1000"));
        EXPECT_TRUE(anyTextContains("Riverwood"));
        EXPECT_FALSE(anyTextContains("Balmora"));
        const std::vector<std::string> rows = get<std::vector<std::string>>("view.saveRowTexts()");
        ASSERT_EQ(rows.size(), 2u);
        EXPECT_NE(rows[0].find("Whiterun"), std::string::npos);
        EXPECT_NE(rows[1].find("Riverwood"), std::string::npos);
    }

    TEST_F(SkyrimMenusLua, aClickOnASaveLoadsItAndBackReturns)
    {
        openMenu(0);
        ASSERT_TRUE(click("Load Game"));
        ASSERT_TRUE(run("view.clickSave(2)")); // the older one
        EXPECT_EQ(calls(), "load:Hero/Save1.omwsave");
    }

    TEST_F(SkyrimMenusLua, backLeavesTheLoadPage)
    {
        openMenu(0);
        ASSERT_TRUE(click("Load Game"));
        ASSERT_TRUE(click("Back"));
        EXPECT_EQ(get<std::string>("view.page()"), "main");
        EXPECT_TRUE(hasButton("New Game"));
    }

    TEST_F(SkyrimMenusLua, noSavesShowsAMessageInsteadOfAList)
    {
        ASSERT_TRUE(run("saves = {}"));
        openMenu(0);
        ASSERT_TRUE(click("Load Game"));
        EXPECT_TRUE(anyTextContains("No saved games"));
        EXPECT_EQ(get<int>("#view.saveRowTexts()"), 0);
    }

    TEST_F(SkyrimMenusLua, aLoadAndANewGameTakeThePageDownAtOnceBecauseTheLoadingScreenFollows)
    {
        openMenu(0);
        ASSERT_TRUE(click("New Game"));
        EXPECT_EQ(documents(), 0u);
        ASSERT_TRUE(run("eng.open = false"));
        frame();
        ASSERT_TRUE(run("eng.open = true"));
        frame();
        EXPECT_EQ(documents(), 1u); // a new visit of the menu mode shows it again
        ASSERT_TRUE(click("Load Game"));
        ASSERT_TRUE(run("view.clickSave(1)"));
        EXPECT_EQ(documents(), 0u);
    }

    TEST_F(SkyrimMenusLua, theMenuComesBackWhenTheModeStaysOpenAfterARefusedLoad)
    {
        openMenu(0);
        ASSERT_TRUE(click("New Game"));
        EXPECT_EQ(documents(), 0u);
        for (int i = 0; i < 80; ++i)
            frame();
        EXPECT_EQ(documents(), 0u);
        for (int i = 0; i < 20; ++i)
            frame();
        EXPECT_EQ(documents(), 1u);
    }

    // ---- saving --------------------------------------------------------------------------------------------------

    TEST_F(SkyrimMenusLua, theSavePageOffersANewSaveFirstAndThenTheExistingOnes)
    {
        openMenu(1);
        ASSERT_TRUE(click("Save Game"));
        EXPECT_EQ(get<std::string>("view.page()"), "save");
        const std::vector<std::string> rows = get<std::vector<std::string>>("view.saveRowTexts()");
        ASSERT_EQ(rows.size(), 3u);
        EXPECT_EQ(rows[0], "New Save");
    }

    TEST_F(SkyrimMenusLua, aNewSaveHasADescriptionFromTheTimeAndNoSlot)
    {
        openMenu(1);
        ASSERT_TRUE(click("Save Game"));
        ASSERT_TRUE(run("view.clickSave(1)"));
        EXPECT_EQ(calls(), "save:Save D5000|nil");
        EXPECT_EQ(get<std::string>("view.page()"), "pause");
        frame();
        frame();
        EXPECT_TRUE(anyTextContains("Game saved"));
        EXPECT_FALSE(anyTextContains("could not be saved"));
    }

    TEST_F(SkyrimMenusLua, aSaveTheEngineRefusedIsReportedAsFailedNotAsSaved)
    {
        ASSERT_TRUE(run("saveWrites = false"));
        openMenu(1);
        ASSERT_TRUE(click("Save Game"));
        ASSERT_TRUE(run("view.clickSave(1)"));
        for (int i = 0; i < 100; ++i)
            frame();
        EXPECT_FALSE(anyTextContains("Game saved"));
        EXPECT_FALSE(anyTextContains("could not be saved")); // not yet: the engine may still be writing
        for (int i = 0; i < 40; ++i)
            frame();
        EXPECT_TRUE(anyTextContains("The game could not be saved"));
        EXPECT_FALSE(anyTextContains("Game saved"));
    }

    TEST_F(SkyrimMenusLua, anOverwrittenSaveCountsAsSavedWhenItsTimeMoved)
    {
        openMenu(1);
        ASSERT_TRUE(click("Save Game"));
        ASSERT_TRUE(run("view.clickSave(2)"));
        frame();
        frame();
        EXPECT_TRUE(anyTextContains("Game saved"));
    }

    TEST_F(SkyrimMenusLua, aClickOnAnExistingSaveOverwritesThatSlot)
    {
        openMenu(1);
        ASSERT_TRUE(click("Save Game"));
        ASSERT_TRUE(run("view.clickSave(2)")); // the newest existing one
        EXPECT_EQ(calls(), "save:quick|Quicksave.omwsave");
    }

    // ---- settings -----------------------------------------------------------------------------------------------

    TEST_F(SkyrimMenusLua, settingsOpensTheEnginesWindowAndTheMenuStepsAsideWhileItShows)
    {
        openMenu(1);
        ASSERT_TRUE(click("Settings"));
        EXPECT_EQ(get<int>("eng.toggled"), 1);
        ASSERT_TRUE(run("eng.settings = true"));
        frame();
        EXPECT_EQ(documents(), 0u);
        ASSERT_TRUE(run("eng.settings = false"));
        frame();
        EXPECT_EQ(documents(), 1u);
        EXPECT_EQ(get<std::string>("view.page()"), "pause");
    }

    // ---- the loading screen tip ---------------------------------------------------------------------------------

    TEST_F(SkyrimMenusLua, noTipIsSentUntilThePackHasItsData)
    {
        for (int i = 0; i < 300; ++i)
            frame();
        EXPECT_EQ(get<int>("#eng.tips"), 0);
    }

    TEST_F(SkyrimMenusLua, theTipIsRenewedWithANewSeedEveryTwoSeconds)
    {
        ASSERT_TRUE(run("pack.ready = true"));
        frame();
        EXPECT_EQ(get<std::string>("eng.tip"), "tip1");
        for (int i = 0; i < 118; ++i)
            frame();
        EXPECT_EQ(get<int>("#eng.tips"), 1);
        frame();
        frame();
        EXPECT_EQ(get<std::string>("eng.tip"), "tip2");
        EXPECT_EQ(get<int>("#eng.tips"), 2);
    }

    TEST_F(SkyrimMenusLua, anEmptyTipIsNotSent)
    {
        ASSERT_TRUE(run("pack.ready = true; menus.tip = function() return '' end"));
        frame();
        EXPECT_EQ(get<int>("#eng.tips"), 0);
    }

    TEST_F(SkyrimMenusLua, startingOrLoadingAGameRenewsTheTipFirst)
    {
        ASSERT_TRUE(run("pack.ready = true"));
        openMenu(0);
        EXPECT_EQ(get<std::string>("eng.tip"), "tip1");
        ASSERT_TRUE(click("New Game"));
        EXPECT_EQ(get<std::string>("eng.tip"), "tip2");
        reopen();
        ASSERT_TRUE(click("Load Game"));
        ASSERT_TRUE(run("view.clickSave(1)"));
        EXPECT_EQ(get<std::string>("eng.tip"), "tip3");
        EXPECT_EQ(calls(), "new,load:Hero/Quicksave.omwsave");
    }
}
