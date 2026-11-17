-- Forced greetings (tasks 0264 and 0271): a ForceGreet package of a scene (Companions) or of a running quest's alias
-- (Brynjolf in Riften) ends with the actor opening its conversation. The pack publishes the request (`pending_base`, the
-- decimal base of the actor); this script opens it the way the engine opens any talk: the actor is activated by the
-- player. The actor is found among the player's cell objects by its base (`<game>:b<base>`, the nearest of them to the
-- player). One activation per request serial.
--
-- A request that no actor of that base within RANGE of the player can serve is left alone: the pack gives it up by
-- itself after a fixed number of asks (a package brain's) or of scene updates (a scene's), with no clock.
local world = require('openmw.world')
local fg = require('core.skyrimse.forcegreet')

local RANGE = 3000.0 -- the actor must be this near the player
local askedSerial = -1

local function find(player, base)
  if not player.cell then return nil end
  local best, bestDistance = nil, math.huge
  for _, o in ipairs(player.cell:getAll()) do
    local ok, id = pcall(function() return o.recordId end)
    if ok and id ~= nil and tostring(id):match(':b(%d+)$') == base then
      local d = (o.position - player.position):length()
      if d < bestDistance then best, bestDistance = o, d end
    end
  end
  return best, bestDistance
end

return {
  engineHandlers = {
    onUpdate = function()
      local base = fg.pending_base()
      if base == '' then return end
      local serial = fg.pending_serial()
      if serial == askedSerial then return end
      local player = world.players[1]
      if not player then return end
      local actor, distance = find(player, base)
      if actor == nil or distance > RANGE then return end
      askedSerial = serial
      print('[skyrimse forcegreet] the actor opens its conversation')
      actor:activateBy(player)
    end,
  },
}
