-- tools/skythieves_check (task 0275): the camera of the real activate key. The global script teleports the player (yaw only);
-- this one sets first person and holds the pitch the key's ray needs to reach a low object (a strongbox) or an actor: the
-- engine's own camera script moves the pitch back, so it is set again every frame until the next look (a test aid).
local camera = require('openmw.camera')

local want = nil
local frames = 0
local shots = 0 -- task 0338: MQShot { n, gap }: a screenshot every gap frames, n times (frames run in a conversation too)
local shotGap = 120
local shotWait = 0
local okDebug, debug = pcall(require, 'openmw.debug')

return {
    engineHandlers = {
        onFrame = function()
            if shots > 0 and okDebug then
                shotWait = shotWait - 1
                if shotWait <= 0 then
                    shotWait = shotGap
                    shots = shots - 1
                    debug.takeScreenshot()
                end
            end
            if want ~= nil and frames > 0 then
                camera.setMode(camera.MODE.FirstPerson)
                camera.setPitch(want)
                frames = frames - 1
            end
        end,
    },
    eventHandlers = {
        MQShot = function(e)
            shots = e.n or 3
            shotGap = e.gap or 120
            shotWait = e.first or 60
        end,
        MQLook = function(e)
            want = e.pitch or 0
            frames = 2000
            camera.setMode(camera.MODE.FirstPerson)
            camera.setPitch(want)
        end,
    },
}
