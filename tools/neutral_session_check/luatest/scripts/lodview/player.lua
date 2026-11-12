-- tools/neutral_session_check (task 0129): the player side of the water check. Puts the camera at a fixed place and
-- direction (the static camera, whose position can only be set once the mode has changed, so in the next frame) and takes
-- a picture on request.
local debug = require('openmw.debug')
local camera = require('openmw.camera')
local util = require('openmw.util')

local pending = nil

return {
    eventHandlers = {
        WaterCamera = function(d)
            camera.setMode(camera.MODE.Static)
            pending = d
        end,
        WaterGod = function() if not debug.isGodMode() then debug.toggleGodMode() end end,
        WaterShot = function() debug.takeScreenshot() end,
        WaterWire = function() debug.toggleRenderMode(debug.RENDER_MODE.Wireframe) end,
    },
    engineHandlers = {
        onFrame = function()
            if pending and camera.getMode() == camera.MODE.Static then
                camera.setStaticPosition(util.vector3(pending.x, pending.y, pending.z))
                camera.setYaw(pending.yaw)
                camera.setPitch(pending.pitch)
                pending = nil
            end
        end,
    },
}
