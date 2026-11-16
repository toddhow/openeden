-- tools/neutral_session_check (task 0190): the player half of "n5ai". The global script sends N5AIShot { x, y, z, lx, ly, lz }
-- (the camera position and the point to look at); the camera goes to Static mode (it takes a frame), is placed there, and
-- a picture is taken in the frame after. N5Shot takes a plain picture.
local camera = require('openmw.camera')
local debug = require('openmw.debug')
local util = require('openmw.util')

local want, shotPending = nil, false

return {
    eventHandlers = {
        N5Shot = function() debug.takeScreenshot() end,
        N5AIShot = function(d) want = d end,
    },
    engineHandlers = {
        onFrame = function()
            if want then
                if camera.getMode() ~= camera.MODE.Static then
                    camera.setMode(camera.MODE.Static) -- takes effect next frame
                    return
                end
                local d = want
                want = nil
                camera.setStaticPosition(util.vector3(d.x, d.y, d.z))
                local dx, dy = d.lx - d.x, d.ly - d.y
                camera.setYaw(math.atan2(dx, dy))
                camera.setPitch(math.atan2(d.z - d.lz, math.sqrt(dx * dx + dy * dy)))
                shotPending = true
            elseif shotPending then
                shotPending = false
                debug.takeScreenshot()
            end
        end,
    },
}
