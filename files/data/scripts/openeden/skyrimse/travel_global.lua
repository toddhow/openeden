-- Skyrim SE fast travel, the global half (task 0218). The world map's window asks for a travel with SkyrimTravelDo; only a
-- global script may teleport the player, pass the game clock and call the pack's Mutate functions. The pack decides
-- whether the player may travel (`refusal`) and where and how long (`plan`); the engine carries the followers along with
-- the player's teleport (mwworld/neutralfollowers.*). Needs the pack's `core.skyrimse.travel` namespace (the builtin line
-- carries REQUIRES=).
local world = require('openmw.world')
local util = require('openmw.util')
local okTravel, travel = pcall(require, 'core.skyrimse.travel')
if not okTravel then
  return {}
end

local CELL_UNITS = 4096 -- the side of a Skyrim SE exterior cell
local DROP = 64 -- the arrival is this high above the landing's floor; `onGround` puts the player down

local function toPlayer(name, data)
  local player = world.players[1]
  if player then player:sendEvent(name, data) end
end

local function refuse(reason, name)
  print(string.format('[skyrimse travel] refused (%s) to %s', reason, tostring(name)))
  toPlayer('SkyrimTravelRefused', { reason = reason, name = name })
end

local function doTravel(data)
  local player = world.players[1]
  if not player or not player.cell then return end
  local worldId = tonumber(tostring(player.cell.worldSpaceId or ''):match(':w(%d+)$'))
  local interior = worldId == nil -- only exterior neutral cells are in a worldspace
  local marker = tostring(data.marker or '')
  local pos = player.position
  local reason = travel.refusal(worldId or 0, interior, marker, pos.x, pos.y)
  if reason ~= '' then return refuse(reason, data.name) end
  local plan = travel.plan(worldId, marker, pos.x, pos.y)
  if #plan < 6 then return refuse('unknown', data.name) end
  local x, y, z, yaw, hours = plan[1], plan[2], plan[3], plan[4], plan[5]
  local ok, cell = pcall(world.getExteriorCell, math.floor(x / CELL_UNITS), math.floor(y / CELL_UNITS), player.cell)
  if not ok or cell == nil then return refuse('no_destination', data.name) end
  print(string.format('[skyrimse travel] to %s at (%.0f, %.0f, %.0f) in %d hours, %d followers', tostring(data.name), x, y,
    z, hours, travel.follower_count()))
  travel.commit(worldId, false, marker, pos.x, pos.y)
  world.advanceTime(hours)
  player:teleport(cell, util.vector3(x, y, z + DROP), { rotation = util.transform.rotateZ(yaw), onGround = true })
  toPlayer('SkyrimTravelDone', { name = data.name, hours = hours })
end

return {
  eventHandlers = {
    -- From the world map's window: data has marker (8 hex digits) and name.
    SkyrimTravelDo = doTravel,
  },
}
