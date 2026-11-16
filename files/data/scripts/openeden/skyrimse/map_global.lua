-- Skyrim SE map discovery, the global half (task 0201). Only a global script may call the pack's Mutate functions: twice a
-- second it tells the pack where the player stands, and the pack marks the map markers within reach as discovered. Needs
-- the pack's `core.skyrimse.map` namespace (the builtin line carries REQUIRES=).
local core = require('openmw.core')
local world = require('openmw.world')
local okMap, map = pcall(require, 'core.skyrimse.map')
if not okMap then
  return {}
end

local PERIOD = 0.5
local last = nil -- simulation time of the last visit (the update's dt is not used: it is 0 while the game is paused)

return {
  engineHandlers = {
    onUpdate = function()
      local now = core.getSimulationTime()
      if last and now - last < PERIOD then return end
      last = now
      local player = world.players[1]
      if not player or not player.cell then return end
      local id = tostring(player.cell.worldSpaceId or '')
      local worldId = tonumber(id:match(':w(%d+)$'))
      if not worldId then return end
      local pos = player.position
      map.visit(worldId, pos.x, pos.y)
    end,
  },
}
