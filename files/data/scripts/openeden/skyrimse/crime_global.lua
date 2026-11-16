-- Skyrim SE crime, the global half (task 0204). Only a global script may call the pack's Mutate functions, so the
-- player's window sends its choice here; it also moves the player to the jail and back. Needs the pack's
-- `core.skyrimse.crime` namespace (the builtin line carries REQUIRES=).
local world = require('openmw.world')
local util = require('openmw.util')
local core = require('openmw.core')
local okCrime, crime = pcall(require, 'core.skyrimse.crime')
if not okCrime then
  return {}
end

local jail = nil -- { back = { cell, position, rotation }, until = simulation time }

-- Task 0213: trespass. The pack is not told where the player stands, so this script reports the player's cell each time
-- it changes (the first report only records it; a cell the jail sends the player to is no trespass: the pack knows).
-- Needs the functions of 0213; an older pack without them is feature-detected.
local lastCell = nil
local function reportCell()
  if crime.entered_cell == nil then return end
  local player = world.players[1]
  if not player or not player.cell then return end
  local name = player.cell.name
  if name == nil or name == '' or name == lastCell then return end
  lastCell = name
  crime.entered_cell(name)
end

-- Task 0258: when the pack says the guard's own arrest conversation can run (`offer_mode() == 'dialogue'`), this script
-- opens it the way the engine opens any talk: the guard is activated by the player. The guard is found among the cell's
-- objects by its base (`<game>:b<base>`, the nearest of them to the player). If the conversation has not begun after a
-- few real seconds, or the guard cannot be found, the pack is told (`dialogue_failed`) and the window offers instead.
local DIALOGUE_WAIT = 4.0 -- real seconds
local asked = nil -- { at = real time } while an arrest conversation is requested for the standing offer

local function findGuard(player)
  local base = crime.guard_base()
  if base == '' or not player.cell then return nil end
  local best, bestDistance = nil, math.huge
  for _, o in ipairs(player.cell:getAll()) do
    local ok, id = pcall(function() return o.recordId end)
    if ok and id ~= nil and tostring(id):match(':b(%d+)$') == base then
      local d = (o.position - player.position):length()
      if d < bestDistance then
        best, bestDistance = o, d
      end
    end
  end
  return best
end

local function arrestDialogue(state)
  if crime.offer_mode == nil then return end
  if state ~= 'offer' then
    asked = nil
    return
  end
  local player = world.players[1]
  if not player then return end
  if crime.offer_mode() ~= 'dialogue' then return end
  if asked == nil then
    local guard = findGuard(player)
    asked = { at = core.getRealTime() }
    if guard ~= nil then
      print('[skyrimse crime] the guard opens the arrest conversation')
      guard:activateBy(player)
    end
  end
  if not crime.dialogue_open() and core.getRealTime() > asked.at + DIALOGUE_WAIT then
    print('[skyrimse crime] the arrest conversation did not begin: the window offers')
    crime.dialogue_failed()
  end
end

local function enter()
  local player = world.players[1]
  if not player or not crime.jail_available() then
    crime.released()
    return
  end
  local cell = world.getCellByName(crime.jail_cell())
  if not cell then
    print('[skyrimse crime] the jail cell ' .. tostring(crime.jail_cell()) .. ' is not available')
    crime.released()
    return
  end
  jail = {
    back = { cell = player.cell, position = player.position, rotation = player.rotation },
    release = core.getSimulationTime() + crime.jail_seconds(),
  }
  print('[skyrimse crime] jail ' .. crime.jail_cell())
  player:teleport(cell, util.vector3(crime.jail_x(), crime.jail_y(), crime.jail_z()),
    { rotation = util.transform.rotateZ(crime.jail_heading()) })
end

local function leave()
  local player = world.players[1]
  local back = jail.back
  jail = nil
  if player and back then
    player:teleport(back.cell, back.position, { rotation = back.rotation })
    print('[skyrimse crime] released')
  end
  crime.released()
end

return {
  engineHandlers = {
    onUpdate = function()
      reportCell()
      local state = crime.state()
      arrestDialogue(state)
      if state == 'jailed' then
        if jail == nil then
          enter()
        elseif core.getSimulationTime() >= jail.release then
          leave()
        end
      end
    end,
  },
  eventHandlers = {
    SkyrimCrimeChoose = function(data) crime.choose(data.choice) end,
  },
}
