-- Skyrim SE barter window, the player half (task 0189): draws the pack's barter state with `openeden.ui` (see
-- openeden/skyrimse/barter_windows.lua for the window logic). Needs `[GUI] rmlui`; without it `openeden.ui` is absent
-- and this script does nothing.
local core = require('openmw.core')
local I = require('openmw.interfaces')

local okUi, ui = pcall(require, 'openeden.ui')
local okInv, inv = pcall(require, 'core.skyrimse.inventory')
local okBarter, barter = pcall(require, 'core.skyrimse.barter')
if not (okUi and okInv and okBarter) then
  return {}
end
local windows = require('openeden.skyrimse.barter_windows')

local MODE = 'Interface'
local view
local seenMode, waited = false, 0

local function leave()
  I.UI.setMode() -- drops every mode; the windows follow in onFrame
end

view = windows.create{
  ui = ui,
  inv = inv,
  barter = barter,
  buy = function(item, count) core.sendGlobalEvent('SkyrimBarterBuy', { item = item, count = count }) end,
  sell = function(item, count) core.sendGlobalEvent('SkyrimBarterSell', { item = item, count = count }) end,
  close = leave,
}

local function show(name)
  view.open(name)
  seenMode, waited = false, 0
  -- GUI mode with no MyGUI window at all: the cursor, the pause and the RmlUi overlay's input
  I.UI.setMode(MODE, { windows = {} })
end

return {
  engineHandlers = {
    onFrame = function()
      if view.isOpen() then
        if I.UI.getMode() ~= nil then
          seenMode = true
        elseif seenMode or waited > 120 then
          view.dismiss() -- the mode was left (Escape, or the Close button)
          core.sendGlobalEvent('SkyrimBarterClose', {})
        else
          waited = waited + 1
        end
      end
      view.update()
    end,
  },
  eventHandlers = {
    SkyrimBarterOpen = function(data) show(data.name) end,
  },
}
