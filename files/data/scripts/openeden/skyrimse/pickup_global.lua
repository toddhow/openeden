-- Skyrim SE item pickup and pickpocketing, the global half (task 0208). Only a global script may call the pack's Mutate
-- functions. Needs the pack's `core.skyrimse.pickup` and `core.skyrimse.inventory` namespaces (the builtin line carries
-- REQUIRES=) and `[Game] neutral inventory` for the engine's activation events.
local world = require('openmw.world')
local okPick, pick = pcall(require, 'core.skyrimse.pickup')
local okInv, inv = pcall(require, 'core.skyrimse.inventory')
if not (okPick and okInv) then
  return {}
end

return {
  eventHandlers = {
    -- From the engine (MWClass::Neutral::activate): a placed item was activated by the player.
    -- data: placement and base (hex text), kind ('item'), name.
    openeden_skyrimse_item_activated = function(data)
      print('[skyrimse pickup] item activated ' .. tostring(data.placement) .. ' ' .. tostring(data.name))
      pick.pickup(data.placement, data.base)
    end,
    -- From the engine: a sneaking player activated a living actor. The pack registers its list as a corpse's would be
    -- and the player's window opens beside it.
    openeden_skyrimse_pickpocket_opened = function(data)
      print('[skyrimse pickup] pickpocket ' .. tostring(data.placement) .. ' ' .. tostring(data.name))
      inv.open(data.placement, data.base, 'actor')
      local player = world.players[1]
      if player then player:sendEvent('SkyrimPickpocketOpen', { owner = data.placement, name = data.name }) end
    end,
    -- From the player's window: a try for `count` of `item`.
    SkyrimPickpocketTake = function(data)
      pick.take(data.owner, data.item, data.count)
    end,
  },
}
