-- Task 0264: a ForceGreet package that a scene gave an actor ends with the actor opening its conversation. The pack
-- publishes the request (`pending_base`, the decimal base of the actor); this script opens it the way the engine opens
-- any talk: the actor is activated by the player. The actor is found among the player's cell objects by its base
-- (`<game>:b<base>`, the nearest of them to the player). One activation per request serial; the pack gives a request
-- up by itself when no conversation begins (its own deterministic timeout), so nothing here is timed.
local world = require('openmw.world')
local fg = require('core.skyrimse.forcegreet')

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
  return best
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
      local actor = find(player, base)
      if actor ~= nil then
        askedSerial = serial
        print('[skyrimse forcegreet] the actor opens its conversation')
        actor:activateBy(player)
      end
    end,
  },
}
