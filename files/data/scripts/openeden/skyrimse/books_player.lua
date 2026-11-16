-- Skyrim SE book reading window, the player half (task 0203): draws the pack's book with `openeden.ui` (see
-- openeden/skyrimse/book_window.lua). Opened by the inventory window's Read button (a local event, the interface mode is
-- already up) or, since task 0234, by a book lying in the world (the same event with a placement: this script opens the
-- interface mode itself and offers Take); closes with the interface mode. Needs `[GUI] rmlui`.
local core = require('openmw.core')
local I = require('openmw.interfaces')

local okUi, ui = pcall(require, 'openeden.ui')
local okLb, lb = pcall(require, 'core.skyrimse.lockbooks')
if not (okUi and okLb) then
  return {}
end
local windows = require('openeden.skyrimse.book_window')

local MODE = 'Interface'
local worldBook = false -- the window was opened for a book in the world: this script owns the mode
local seenMode, waited = false, 0

local view = windows.create{
  ui = ui,
  lb = lb,
  take = function(placement, item)
    core.sendGlobalEvent('SkyrimBookTake', { placement = placement, item = item })
  end,
  close = function()
    if worldBook then
      worldBook = false
      I.UI.setMode() -- the world runs again, so a theft is judged
    end
  end,
}

return {
  engineHandlers = {
    onFrame = function()
      if view.isOpen() then
        if worldBook then
          if I.UI.getMode() ~= nil then
            seenMode = true
          elseif seenMode or waited > 120 then
            view.dismiss() -- the mode was left (Escape)
            worldBook = false
          else
            waited = waited + 1
          end
        elseif I.UI.getMode() == nil then
          view.dismiss() -- the interface mode was left
        end
      end
      view.update()
    end,
  },
  eventHandlers = {
    SkyrimBookOpen = function(data)
      view.open(data.item, data.placement, data.takeable)
      if data.placement ~= nil then
        worldBook = true
        seenMode, waited = false, 0
        I.UI.setMode(MODE, { windows = {} })
      end
    end,
  },
}
