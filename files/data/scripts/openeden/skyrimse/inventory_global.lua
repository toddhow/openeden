-- Skyrim SE inventory, the global half (task 0182). Only a global script may call the pack's Mutate functions, so the
-- player's window sends its requests here. Needs the pack's `core.skyrimse.inventory` namespace (the builtin line
-- carries REQUIRES=) and `[Game] neutral inventory` for the engine's activation event.
local world = require('openmw.world')
local okInv, inv = pcall(require, 'core.skyrimse.inventory')
if not okInv then
  return {}
end

local function toPlayer(name, data)
  local player = world.players[1]
  if player then player:sendEvent(name, data) end
end

return {
  eventHandlers = {
    -- From the engine (MWClass::Neutral::activate): a container or a corpse was activated.
    -- data: placement and base (hex text), kind ('container' or 'actor'), name.
    openeden_skyrimse_container_opened = function(data)
      print('[skyrimse inventory] container opened ' .. tostring(data.placement))
      inv.open(data.placement, data.base, data.kind)
      toPlayer('SkyrimInventoryOpen', { owner = data.placement, name = data.name })
    end,
    -- From the player's windows.
    SkyrimInventoryTransfer = function(data)
      inv.transfer(data.from, data.to, data.item, data.count)
    end,
    SkyrimInventoryTakeAll = function(data)
      inv.take_all(data.from, data.to)
    end,
  },
}
