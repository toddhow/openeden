-- Skyrim SE dialogue services, the global half (task 0219): training, renting a room and gifts. Only a global script may
-- call the pack's Mutate functions, so the player's window sends its requests here. Needs the pack's
-- `core.skyrimse.services` namespace (the builtin line carries REQUIRES=) and `[Game] neutral inventory` for the
-- engine's dialogue event.
local core = require('openmw.core')
local world = require('openmw.world')
local okServices, services = pcall(require, 'core.skyrimse.services')
if not okServices then
  return {}
end
local okInv, inv = pcall(require, 'core.skyrimse.inventory')

local CLOCK_PERIOD = 0.5
local lastClock = nil

local function toPlayer(name, data)
  local player = world.players[1]
  if player then player:sendEvent(name, data) end
end

-- The pack gets the game time twice a second so that a rental ends on time (the update's `dt` is 0 while paused).
local function sendClock()
  local now = core.getSimulationTime()
  if lastClock and now - lastClock < CLOCK_PERIOD then return end
  lastClock = now
  services.set_clock(core.getGameTime())
end

return {
  engineHandlers = {
    onUpdate = function() sendClock() end,
  },
  eventHandlers = {
    -- From the engine (NeutralDialogueSession): a dialogue line opened a service.
    -- data: service ("training", "rent" or "gift"), placement and base of the speaker (hex text), its name.
    openeden_skyrimse_service_opened = function(data)
      print('[skyrimse services] ' .. tostring(data.service) .. ' opened with ' .. tostring(data.name))
      services.set_clock(core.getGameTime())
      if data.service == 'carry' then
        -- a follower's inventory: the container window of the inventory package, on the NPC's owner
        if okInv then
          inv.open(data.placement, data.base, 'actor')
          toPlayer('SkyrimInventoryOpen', { owner = data.placement, name = data.name })
        end
        return
      end
      services.open(data.service, data.placement, data.base, data.name)
      toPlayer('SkyrimServicesOpen', { service = data.service, name = data.name })
    end,
    -- From the player's window.
    SkyrimServicesTrain = function() services.train() end,
    SkyrimServicesRent = function() services.rent() end,
    SkyrimServicesGive = function(data)
      if okInv then inv.transfer('player', data.owner, data.item, data.count) end
    end,
    SkyrimServicesClose = function() services.close() end,
  },
}
