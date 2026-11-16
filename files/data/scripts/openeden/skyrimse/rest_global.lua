-- Skyrim SE waiting and sleeping, the global half (task 0210). Only a global script may advance the clock
-- (`world.advanceTime`) and call the pack's Mutate functions, so the player's window sends its request here. Needs the
-- pack's `core.skyrimse.rest` namespace (the builtin line carries REQUIRES=). A bed arrives as the engine's furniture
-- event (`openeden_skyrimse_bench_opened`, only with `[Game] neutral inventory`): the pack says whether it is a bed.
local core = require('openmw.core')
local world = require('openmw.world')
local okRest, rest = pcall(require, 'core.skyrimse.rest')
if not okRest then
  return {}
end
-- Task 0213: using a bed that is not yours is a trespass when somebody sees it (the crime namespace is optional).
local okCrime, crime = pcall(require, 'core.skyrimse.crime')

local CLOCK_PERIOD = 0.5
local lastClock = nil -- simulation time of the last set_clock

local function toPlayer(name, data)
  local player = world.players[1]
  if player then player:sendEvent(name, data) end
end

local function gameSeconds()
  return core.getGameTime()
end

-- The pack gets the game time twice a second so that a Rested state ends on time; the update's `dt` is not used (it is 0
-- while the game is paused).
local function sendClock()
  local now = core.getSimulationTime()
  if lastClock and now - lastClock < CLOCK_PERIOD then return end
  lastClock = now
  rest.set_clock(gameSeconds())
end

local function doRest(data)
  local hours = math.floor(tonumber(data.hours) or 1)
  if hours < 1 then hours = 1 end
  local max = rest.max_hours()
  if hours > max then hours = max end
  local reason = rest.can_rest()
  if reason ~= '' then
    toPlayer('SkyrimRestMessage', { reason = reason })
    return
  end
  local scale = core.getGameTimeScale()
  if not (scale > 0) then scale = 1 end
  -- The pack grants Rested from the clock it was given, so give it the time the rest ends at, then pass the time.
  rest.set_clock(gameSeconds() + hours * 3600)
  rest.rest(hours, hours * 3600 / scale, data.sleeping == true)
  world.advanceTime(hours)
  print(string.format('[skyrimse rest] %s %d hours (clock %.0f -> %.0f)', data.sleeping and 'slept' or 'waited', hours,
    gameSeconds(), gameSeconds() + hours * 3600))
end

return {
  engineHandlers = {
    onUpdate = function() sendClock() end,
  },
  eventHandlers = {
    -- From the engine (Neutral::activate on a furniture): data has placement, base, kind ("bench") and name (hex text).
    openeden_skyrimse_bench_opened = function(data)
      local state = rest.bed_state(data.placement, data.base)
      print('[skyrimse rest] furniture ' .. tostring(data.name) .. ': ' .. state)
      if state == 'free' then
        toPlayer('SkyrimSleepOpen', { name = data.name })
      elseif state == 'owned' then
        if okCrime and crime.trespass_bed ~= nil then crime.trespass_bed(data.placement) end
        toPlayer('SkyrimRestNotice', { text = 'This bed is not yours.' })
      end
    end,
    -- From the player's window.
    SkyrimRestDo = doRest,
  },
}
