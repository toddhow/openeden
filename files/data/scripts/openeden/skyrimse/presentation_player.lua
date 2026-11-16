-- Skyrim SE: the player's level stat follows the pack's progression, so a save's header (and the load list) shows it
-- (task 0215; see openeden/skyrimse/level_mirror.lua). Started only with the pack's `core.skyrimse.progression`.
local okProg, prog = pcall(require, 'core.skyrimse.progression')
if not okProg then
  return {}
end
local self = require('openmw.self')
local types = require('openmw.types')
local mirror = require('openeden.skyrimse.level_mirror').create{
  ready = function() return prog.ready() end,
  level = function() return prog.level() end,
  getStat = function() return types.Actor.stats.level(self).current end,
  setStat = function(n) types.Actor.stats.level(self).current = n end,
}

return {
  engineHandlers = {
    onFrame = function()
      mirror.update()
    end,
  },
}
