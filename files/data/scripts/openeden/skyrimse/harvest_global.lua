-- Skyrim SE harvesting, the global half (task 0221). Only a global script may call the pack's Mutate functions. Needs
-- the pack's `core.skyrimse.harvest` namespace (the builtin line carries REQUIRES=) and `[Game] neutral inventory` for
-- the engine's activation event. The pack never reads a clock: this script passes the game time and the calendar month
-- in twice a second, which also grows plants back.
local core = require('openmw.core')
local world = require('openmw.world')
local okHarvest, harvest = pcall(require, 'core.skyrimse.harvest')
if not okHarvest then
  return {}
end

local CLOCK_PERIOD = 0.5
local lastClock = nil -- simulation time of the last set_clock
local lastRevision = harvest.revision() -- a Mutate call is queued, so its result shows up a frame later
local waiting = false

-- The month 0 to 11 of the engine's calendar (-1: not known).
local function gameMonth()
  local ok, month = pcall(function() return world.mwscript.getGlobalVariables().gamemonth end)
  if ok and type(month) == 'number' then return math.floor(month) end
  return -1
end

local function sendClock()
  local now = core.getSimulationTime()
  if lastClock and now - lastClock < CLOCK_PERIOD then return end
  lastClock = now
  harvest.set_clock(core.getGameTime(), gameMonth())
end

return {
  engineHandlers = {
    onUpdate = function()
      sendClock()
      -- the line of a harvest the pack has run since the event: shown once
      local revision = harvest.revision()
      if waiting and revision ~= lastRevision then
        waiting = false
        print('[skyrimse harvest] ' .. harvest.state() .. ': ' .. harvest.message())
        local player = world.players[1]
        if player then player:sendEvent('SkyrimHarvestMessage', { text = harvest.message() }) end
      end
      lastRevision = revision
    end,
  },
  eventHandlers = {
    -- From the engine (Neutral::activate on a flora or tree): data has placement, base, kind ("flora") and name.
    openeden_skyrimse_harvest_activated = function(data)
      sendClock()
      lastRevision = harvest.revision()
      waiting = true
      harvest.harvest(data.placement, data.base)
    end,
  },
}
