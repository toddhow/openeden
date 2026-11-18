-- tools/skythieves_check (task 0275): the camera of the real activate key. The global script teleports the player (yaw only);
-- this one sets first person and holds the pitch the key's ray needs to reach a low object (a strongbox) or an actor: the
-- engine's own camera script moves the pitch back, so it is set again every frame until the next look (a test aid).
local camera = require('openmw.camera')

local want = nil
local frames = 0

return {
    engineHandlers = {
        onFrame = function()
            if want ~= nil and frames > 0 then
                camera.setMode(camera.MODE.FirstPerson)
                camera.setPitch(want)
                frames = frames - 1
            end
        end,
    },
    eventHandlers = {
        MQLook = function(e)
            want = e.pitch or 0
            frames = 2000
            camera.setMode(camera.MODE.FirstPerson)
            camera.setPitch(want)
        end,
    },
}
