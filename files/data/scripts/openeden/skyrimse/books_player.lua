-- Skyrim SE book reading window, the player half (task 0203): draws the pack's book with `openeden.ui` (see
-- openeden/skyrimse/book_window.lua). Opened by the inventory window's Read button (a local event); closes with the
-- interface mode. Needs `[GUI] rmlui`.
local I = require('openmw.interfaces')

local okUi, ui = pcall(require, 'openeden.ui')
local okLb, lb = pcall(require, 'core.skyrimse.lockbooks')
if not (okUi and okLb) then
  return {}
end
local windows = require('openeden.skyrimse.book_window')

local view = windows.create{ ui = ui, lb = lb }

return {
  engineHandlers = {
    onFrame = function()
      if view.isOpen() and I.UI.getMode() == nil then
        view.dismiss() -- the interface mode was left
      end
      view.update()
    end,
  },
  eventHandlers = {
    SkyrimBookOpen = function(data) view.open(data.item) end,
  },
}
