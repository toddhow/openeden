-- Skyrim SE barter, the global half (task 0189). Only a global script may call the pack's Mutate functions, so the
-- player's window sends its requests here. Needs the pack's `core.skyrimse.barter` namespace (the builtin line carries
-- REQUIRES=) and `[Game] neutral inventory` for the engine's dialogue event.
local world = require('openmw.world')
local okBarter, barter = pcall(require, 'core.skyrimse.barter')
if not okBarter then
  return {}
end

local function toPlayer(name, data)
  local player = world.players[1]
  if player then player:sendEvent(name, data) end
end

return {
  eventHandlers = {
    -- From the engine (NeutralDialogueSession): the merchant's barter line was chosen.
    -- data: placement and base of the speaker (hex text) and its name.
    openeden_skyrimse_barter_opened = function(data)
      print('[skyrimse barter] barter opened with ' .. tostring(data.name))
      barter.open(data.placement, data.base, data.name)
      toPlayer('SkyrimBarterOpen', { name = data.name })
    end,
    -- From the player's window.
    SkyrimBarterBuy = function(data) barter.buy(data.item, data.count) end,
    SkyrimBarterSell = function(data) barter.sell(data.item, data.count) end,
    SkyrimBarterClose = function() barter.close() end,
  },
}
