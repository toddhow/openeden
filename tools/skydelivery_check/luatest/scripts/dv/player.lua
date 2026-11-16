-- tools/skydelivery_check (task 0242): the player half. Turns the player's view to the pitch cfg.pitch (radians, positive looks down)
-- through the pitch control (the combat reads the player's own pitch, which camera.setPitch does not change here) and takes a
-- picture when the global script asks (DVShot).
local camera = require('openmw.camera')
local debug = require('openmw.debug')
local self = require('openmw.self')
local cfg = require('scripts.dv.cfg')

return {
    eventHandlers = {
        DVShot = function() debug.takeScreenshot() end,
    },
    engineHandlers = {
        onFrame = function()
            if cfg.pitch ~= 0 then
                self.controls.pitchChange = 1.0 * (cfg.pitch - camera.getPitch())
            end
        end,
    },
}
