-- tools/neutral_session_check (task 0146): player side of n5/lip.lua: puts the camera at a fixed place looking at a
-- point (N5Cam: position, yaw, pitch), and takes a screenshot when asked (N5Shot). The static mode takes effect on the
-- frame after setMode, so the placement is applied from onUpdate.
local camera = require('openmw.camera')
local debug = require('openmw.debug')
local wanted = nil
return {
    engineHandlers = {
        onUpdate = function()
            if wanted and camera.getMode() == camera.MODE.Static then
                camera.setStaticPosition(wanted.position)
                camera.setYaw(wanted.yaw)
                camera.setPitch(wanted.pitch)
            end
        end,
    },
    eventHandlers = {
        N5Cam = function(data)
            camera.setMode(camera.MODE.Static)
            wanted = data
        end,
        N5Shot = function() debug.takeScreenshot() end,
    },
}
