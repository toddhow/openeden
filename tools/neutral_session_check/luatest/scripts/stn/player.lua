-- tools/neutral_session_check (task 0237): tilts the view to the pitch the global script asks for (radians, down is positive).
local self = require('openmw.self')
local camera = require('openmw.camera')
local goal = nil
return {
    engineHandlers = {
        onFrame = function()
            if goal then
                self.controls.pitchChange = math.max(-0.05, math.min(0.05, goal - camera.getPitch()))
            end
        end,
    },
    eventHandlers = {
        StnPitch = function(data) goal = data.pitch end,
    },
}
