-- tools/neutral_session_check (task 0127 W-S3): the player side of the W3 check. Aims the first person camera at a
-- direction (yaw, pitch in radians) and takes a picture a moment later.
local debug = require('openmw.debug')
local core = require('openmw.core')
local camera = require('openmw.camera')

local shotAt = nil

return {
    eventHandlers = {
        W3Aim = function(data)
            camera.setMode(camera.MODE.FirstPerson)
            camera.setYaw(data.yaw)
            camera.setPitch(data.pitch)
            shotAt = core.getRealTime() + 1.5
            print(string.format('W3 aim yaw=%.2f pitch=%.2f', data.yaw, data.pitch))
        end,
    },
    engineHandlers = {
        onFrame = function()
            if shotAt ~= nil and core.getRealTime() >= shotAt then
                shotAt = nil
                debug.takeScreenshot()
                print('W3 shot')
            end
        end,
    },
}
