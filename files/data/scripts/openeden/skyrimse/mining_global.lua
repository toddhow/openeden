-- Skyrim SE mining and woodcutting, the global half (task 0224). Only a global script may call the pack's Mutate
-- functions. Needs the pack's `core.skyrimse.mining` namespace (the builtin line carries REQUIRES=) and
-- `[Game] neutral inventory` for the engine's activation events. The pack never reads a clock: this script passes the
-- game time in twice a second, which also fills depleted veins again.
local core = require('openmw.core')
local world = require('openmw.world')
local okMining, mining = pcall(require, 'core.skyrimse.mining')
if not okMining then
  return {}
end

local CLOCK_PERIOD = 0.5
local lastClock = nil -- simulation time of the last set_clock
local lastRevision = mining.revision() -- a Mutate call is queued, so its result shows up a frame later
local waiting = false

local function sendClock()
  local now = core.getSimulationTime()
  if lastClock and now - lastClock < CLOCK_PERIOD then return end
  lastClock = now
  mining.set_clock(core.getGameTime())
end

local function activate(data)
  sendClock()
  lastRevision = mining.revision()
  waiting = true
  mining.activate(data.placement, data.base)
end

return {
  engineHandlers = {
    onUpdate = function()
      sendClock()
      -- the line of a try the pack has run since the event: shown once
      local revision = mining.revision()
      if waiting and revision ~= lastRevision then
        waiting = false
        print('[skyrimse mining] ' .. mining.state() .. ': ' .. mining.message())
        local player = world.players[1]
        if player then player:sendEvent('SkyrimMiningMessage', { text = mining.message() }) end
      end
      lastRevision = revision
    end,
  },
  eventHandlers = {
    -- From the engine (Neutral::activate on an Activator): data has placement, base, kind ("resource") and name.
    openeden_skyrimse_resource_activated = activate,
    -- A Furniture (the chopping block) sends the bench event; the pack ignores a base that is not a resource.
    openeden_skyrimse_bench_opened = activate,
  },
}
