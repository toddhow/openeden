-- tools/skydungeon_check (task 0289): the player half. DNGShot { from, to }: a picture from a free camera.
local camera = require('openmw.camera')
local debug = require('openmw.debug')

local pending = nil

return {
    eventHandlers = {
        DNGShot = function(data)
            local d = data.to - data.from
            camera.setMode(camera.MODE.Static, true)
            camera.setStaticPosition(data.from)
            camera.setYaw(math.atan2(d.x, d.y))
            camera.setPitch(math.atan2(-d.z, math.sqrt(d.x * d.x + d.y * d.y)))
            pending = { frames = 8 }
        end,
    },
    engineHandlers = {
        onFrame = function()
            if pending ~= nil then
                camera.setMode(camera.MODE.Static, true)
                pending.frames = pending.frames - 1
                if pending.frames == 0 then
                    debug.takeScreenshot()
                    pending = nil
                end
            end
        end,
    },
}
