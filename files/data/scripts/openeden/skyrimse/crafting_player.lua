-- Skyrim SE crafting window, the player half (task 0199): draws the pack's crafting state with `openeden.ui` (see
-- openeden/skyrimse/crafting_windows.lua for the window logic). Needs `[GUI] rmlui`; without it `openeden.ui` is absent
-- and this script does nothing.
local core = require('openmw.core')
local I = require('openmw.interfaces')

local okUi, ui = pcall(require, 'openeden.ui')
local okInv, inv = pcall(require, 'core.skyrimse.inventory')
local okCrafting, crafting = pcall(require, 'core.skyrimse.crafting')
if not (okUi and okInv and okCrafting) then
  return {}
end
local windows = require('openeden.skyrimse.crafting_windows')

local MODE = 'Interface'
local view
local seenMode, waited = false, 0

local function send(name) return function(data) core.sendGlobalEvent(name, data) end end

view = windows.create{
  ui = ui,
  inv = inv,
  crafting = crafting,
  brew = function(a, b, c) core.sendGlobalEvent('SkyrimCraftingBrew', { a = a, b = b, c = c }) end,
  eat = function(item) core.sendGlobalEvent('SkyrimCraftingEat', { item = item }) end,
  craft = function(recipe) core.sendGlobalEvent('SkyrimCraftingCraft', { recipe = recipe }) end,
  temper = function(recipe) core.sendGlobalEvent('SkyrimCraftingTemper', { recipe = recipe }) end,
  disenchant = function(item) core.sendGlobalEvent('SkyrimCraftingDisenchant', { item = item }) end,
  enchant = function(item, e, gem) core.sendGlobalEvent('SkyrimCraftingEnchant', { item = item, enchantment = e, gem = gem }) end,
  close = function() I.UI.setMode() end, -- drops every mode; the window follows in onFrame
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
          core.sendGlobalEvent('SkyrimCraftingClose', {})
        else
          waited = waited + 1
        end
      end
      view.update()
    end,
  },
  eventHandlers = {
    SkyrimCraftingOpen = function(data) show(data.name) end,
  },
}
