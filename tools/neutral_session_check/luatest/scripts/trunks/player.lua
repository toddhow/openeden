-- tools/neutral_session_check (task 0136): the player side of the "trunks" check. Puts a static camera where asked and
-- takes pictures on request. The camera mode changes at the end of the frame, so the position is set a frame later.
local debug = require('openmw.debug')
local camera = require('openmw.camera')

local core = require('openmw.core')
local pending = nil
local timing = false
local frames, total, worst = 0, 0, 0

return {
    eventHandlers = {
        TreeLook = function(look)
            camera.setMode(camera.MODE.Static)
            pending = look
        end,
        TreeShot = function(tag)
            debug.takeScreenshot()
            print('TRUNKS shot ' .. tostring(tag))
            if tag == 4 and timing then
                timing = false
                print(string.format('TRUNKS frame time over %d frames: mean %.3f ms, worst %.3f ms', frames, 1000 * total / math.max(frames, 1), 1000 * worst))
            end
        end,
    },
    engineHandlers = {
        onFrame = function()
            if pending ~= nil and camera.getMode() == camera.MODE.Static then
                camera.setStaticPosition(pending.pos)
                camera.setPitch(pending.pitch)
                camera.setYaw(pending.yaw)
                print('TRUNKS camera placed')
                pending = nil
                timing = true
            end
            if timing then
                local d = core.getRealFrameDuration()
                frames, total, worst = frames + 1, total + d, math.max(worst, d)
            end
        end,
    },
}
