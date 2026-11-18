-- Task 0323: god mode so that nothing ends the run; in 'real' mode the player moves by the engine's own movement
-- (collision off: the stub player flies along its heading at run speed), so a border crossing is the one the game makes
-- when a player walks (the incremental cell build applies to it, a Lua teleport is not one).
local self = require('openmw.self')
local debug = require('openmw.debug')
local types = require('openmw.types')
local go = false
return {
    eventHandlers = {
        S323God = function() if not debug.isGodMode() then debug.toggleGodMode() end end,
        S323Go = function(d)
            if debug.isCollisionEnabled() then debug.toggleCollision() end
            if d.speed then pcall(function() types.NPC.stats.attributes.speed(self).base = d.speed end) end
            if d.athletics then pcall(function() types.NPC.stats.skills.athletics(self).base = d.athletics end) end
            go = true
        end,
        S323Stop = function()
            go = false
            self.controls.movement = 0
            if not debug.isCollisionEnabled() then debug.toggleCollision() end
        end,
    },
    engineHandlers = {
        onFrame = function()
            if go then
                self.controls.movement = 1
                self.controls.run = true
            end
        end,
    },
}
