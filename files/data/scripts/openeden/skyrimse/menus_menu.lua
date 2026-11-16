-- Skyrim SE main menu, pause menu and loading screen tips, the menu-script half (task 0207): draws the menus with
-- `openeden.ui` (see openeden/skyrimse/menus.lua for the window logic). Started only when the session's game pack has the
-- namespace `core.skyrimse.menus` (the pack setting `[extension.skyrimse] menus`); without `[GUI] rmlui` `openeden.ui` is
-- absent and this script does nothing, so the engine's own menus stay.
--
-- The engine's main menu mode (Escape in a game, or the start screen with no game) is the trigger. While this script is
-- running the engine's own (MyGUI) menu draws nothing; its GUI mode and Escape handling work as always.
local okUi, ui = pcall(require, 'openeden.ui')
local okPack, menus = pcall(require, 'core.skyrimse.menus')
if not (okUi and okPack) then
  return {}
end
local menu = require('openmw.menu')
local windows = require('openeden.skyrimse.menus')

local seedBase = 0
if os and os.time then seedBase = os.time() % 100000 end

local view = windows.create{
  ui = ui,
  menu = menu,
  menus = menus,
  seedBase = seedBase,
  engine = {
    setReplaced = function(b) menu._setMainMenuReplaced(b) end,
    mainMenuOpen = function() return menu._mainMenuOpen() end,
    closeMainMenu = function() menu._closeMainMenu() end,
    settingsVisible = function() return menu._settingsVisible() end,
    toggleSettings = function() menu._toggleSettings() end,
    setTip = function(text) menu._setLoadingTip(text) end,
  },
}

return {
  engineHandlers = {
    onFrame = function()
      view.update()
    end,
  },
}
