-- Forced greetings (tasks 0264, 0271 and 0273b): a ForceGreet package of a scene (Companions) or of a running quest's
-- alias (Brynjolf in Riften) ends with the actor opening its conversation. The pack publishes every waiting request
-- (`pending_requests`: "reference:base:serial;..."); this script opens each one the way the engine opens any talk: the
-- actor is activated by the player. The actor is the REQUESTING reference (a neutral placement's id is
-- `@0x3fffffff<placement as 8 hex digits>`); only when that reference is not in the player's cell, the nearest object
-- of its base (`<game>:b<base>`) is used, as before 0273b. One activation per request serial. A request that nobody
-- within RANGE of the player can serve is skipped, so it never holds up the others: the pack gives it up by itself
-- after a fixed number of asks (a package brain's) or of scene updates (a scene's), with no clock.
local world = require('openmw.world')
local fg = require('core.skyrimse.forcegreet')

local RANGE = 3000.0 -- the actor must be this near the player
local asked = {} -- [serial] = true

local function parse(text)
  local out = {}
  for reference, base, serial in tostring(text):gmatch('(%d+):(%d+):(%d+)') do
    out[#out + 1] = { reference = tonumber(reference), base = base, serial = tonumber(serial) }
  end
  return out
end

-- The object of the reference in the player's cell, else the nearest of the base.
local function find(player, request)
  if not player.cell then return nil end
  local wanted = string.format('@0x3fffffff%08x', request.reference)
  local best, bestDistance, byReference = nil, math.huge, nil
  for _, o in ipairs(player.cell:getAll()) do
    local ok, id = pcall(function() return o.id end)
    if ok and id == wanted then
      byReference = o
      break
    end
    local okRecord, record = pcall(function() return o.recordId end)
    if okRecord and record ~= nil and tostring(record):match(':b(%d+)$') == request.base then
      local d = (o.position - player.position):length()
      if d < bestDistance then best, bestDistance = o, d end
    end
  end
  if byReference ~= nil then
    return byReference, (byReference.position - player.position):length(), 'reference'
  end
  return best, bestDistance, 'base'
end

return {
  engineHandlers = {
    onUpdate = function()
      local requests = parse(fg.pending_requests())
      if #requests == 0 then
        asked = {}
        return
      end
      local player = world.players[1]
      if not player then return end
      for _, request in ipairs(requests) do
        if not asked[request.serial] then
          local actor, distance, how = find(player, request)
          if actor ~= nil and distance <= RANGE then
            asked[request.serial] = true
            print('[skyrimse forcegreet] the actor opens its conversation (by ' .. how .. ')')
            actor:activateBy(player)
            return -- one activation a frame
          end
        end
      end
    end,
  },
}
