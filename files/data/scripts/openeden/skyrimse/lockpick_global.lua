-- Skyrim SE lock picking, the global half (task 0203). Only a global script may call the pack's Mutate functions, so the
-- player's window sends its requests here. Needs the pack's `core.skyrimse.lockbooks` namespace (the builtin line
-- carries REQUIRES=) and `[Game] neutral inventory` for the engine's activation event.
local world = require('openmw.world')
local okLb, lb = pcall(require, 'core.skyrimse.lockbooks')
if not okLb then
  return {}
end

return {
  eventHandlers = {
    -- From the engine (MWClass::Neutral::activate): a locked door or container the player has no key for.
    -- data: placement and base (hex text), kind ('door' or 'container'), level (decimal text), name.
    openeden_skyrimse_lock_activated = function(data)
      print('[skyrimse lockpick] ' .. tostring(data.kind) .. ' ' .. tostring(data.placement) .. ' level '
        .. tostring(data.level))
      local player = world.players[1]
      if player then
        player:sendEvent('SkyrimLockpickOpen',
          { placement = data.placement, level = tonumber(data.level) or 0, name = data.name })
      end
    end,
    -- From the player's window.
    SkyrimLockpickBegin = function(data) lb.pick_begin(data.placement, data.level) end,
    SkyrimLockpickInput = function(data) lb.pick_input(data.angle, data.turning, data.dtMs) end,
    SkyrimLockpickCancel = function() lb.pick_cancel() end,
  },
}
