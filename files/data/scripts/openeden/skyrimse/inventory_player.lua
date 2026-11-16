-- Skyrim SE inventory and container windows, the player half (task 0182): draws the pack's inventory state with
-- `openeden.ui` (see openeden/skyrimse/inventory_windows.lua for the window logic). Needs `[GUI] rmlui`; without it
-- `openeden.ui` is absent and this script does nothing. The inventory key is I (a Skyrim key, free in OpenMW).
local core = require('openmw.core')
local input = require('openmw.input')
local I = require('openmw.interfaces')
local self = require('openmw.self')

local okUi, ui = pcall(require, 'openeden.ui')
local okInv, inv = pcall(require, 'core.skyrimse.inventory')
if not (okUi and okInv) then
  return {}
end
local windows = require('openeden.skyrimse.inventory_windows')
local hasBooks = pcall(require, 'core.skyrimse.lockbooks') -- task 0203: books can be read

local MODE = 'Interface'
local view
local seenMode, waited = false, 0

local function leave()
  I.UI.setMode() -- drops every mode; the windows follow in onFrame
end

view = windows.create{
  ui = ui,
  inv = inv,
  transfer = function(from, to, item, count)
    core.sendGlobalEvent('SkyrimInventoryTransfer', { from = from, to = to, item = item, count = count })
  end,
  takeAll = function(from, to)
    core.sendGlobalEvent('SkyrimInventoryTakeAll', { from = from, to = to })
  end,
  close = leave,
  -- task 0184: wielding, wearing and drinking are Mutate calls, so the global script runs them
  equip = function(item) core.sendGlobalEvent('SkyrimInventoryEquip', { item = item }) end,
  unequip = function(item) core.sendGlobalEvent('SkyrimInventoryUnequip', { item = item }) end,
  use = function(item) core.sendGlobalEvent('SkyrimInventoryUse', { item = item }) end,
  -- task 0203: the global script applies the reading, the book window (books_player.lua) shows the book
  read = hasBooks and function(item)
    core.sendGlobalEvent('SkyrimBookRead', { item = item })
    self:sendEvent('SkyrimBookOpen', { item = item })
  end or nil,
}

local function show(owner, title)
  view.open(owner, title)
  seenMode, waited = false, 0
  -- GUI mode with no MyGUI window at all: the cursor, the pause and the RmlUi overlay's input. The mode stack of this
  -- script follows one frame later, so onFrame waits for the mode to appear before it watches for it to go.
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
        else
          waited = waited + 1
        end
      end
      view.update()
    end,
    onKeyPress = function(key)
      if key.code ~= input.KEY.I then return end
      if view.isOpen() then
        leave()
      elseif I.UI.getMode() == nil and inv.ready() then
        show(nil, '')
      end
    end,
  },
  eventHandlers = {
    SkyrimInventoryOpen = function(data) show(data.owner, data.name) end,
  },
}
