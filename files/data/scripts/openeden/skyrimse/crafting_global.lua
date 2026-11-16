-- Skyrim SE crafting, the global half (task 0199). Only a global script may call the pack's Mutate functions, so the
-- player's window sends its requests here. Needs the pack's `core.skyrimse.crafting` namespace (the builtin line carries
-- REQUIRES=) and `[Game] neutral inventory` for the engine's activation event.
local world = require('openmw.world')
local okCrafting, crafting = pcall(require, 'core.skyrimse.crafting')
if not okCrafting then
  return {}
end

local function toPlayer(name, data)
  local player = world.players[1]
  if player then player:sendEvent(name, data) end
end

return {
  eventHandlers = {
    -- From the engine (Neutral::activate on a furniture): data has placement, base, kind ("bench") and name (hex text).
    openeden_skyrimse_bench_opened = function(data)
      print('[skyrimse crafting] bench activated: ' .. tostring(data.name))
      crafting.open(data.placement, data.base, data.name)
      toPlayer('SkyrimCraftingOpen', { name = data.name })
    end,
    -- A test aid: needs the pack setting `crafting test bench`; opens a bench of a kind without a furniture.
    SkyrimCraftingOpenKind = function(data)
      crafting.open_kind(data.kind)
      toPlayer('SkyrimCraftingOpen', { name = data.name or data.kind })
    end,
    -- From the player's window.
    SkyrimCraftingBrew = function(d) crafting.brew(d.a, d.b, d.c) end,
    SkyrimCraftingEat = function(d) crafting.eat(d.item) end,
    SkyrimCraftingCraft = function(d) crafting.craft(d.recipe) end,
    SkyrimCraftingTemper = function(d) crafting.temper(d.recipe) end,
    SkyrimCraftingDisenchant = function(d) crafting.disenchant(d.item) end,
    SkyrimCraftingEnchant = function(d) crafting.enchant(d.item, d.enchantment, d.gem) end,
    SkyrimCraftingClose = function() crafting.close() end,
  },
}
