local self = require('openmw.self')
local debug = require('openmw.debug')
local camera = require('openmw.camera')

local shotAtFrame = nil
local frame = 0
return {
    eventHandlers = {
        TLook = function()
            camera.setMode(camera.MODE.FirstPerson)
            camera.setPitch(0.55)
            shotAtFrame = frame + 400
        end,
    },
    engineHandlers = {
        onFrame = function()
            frame = frame + 1
            if shotAtFrame and frame >= shotAtFrame then
                shotAtFrame = nil
                local p = self.object.position
                print(string.format('TLAYERS shot at %.0f,%.0f,%.0f', p.x, p.y, p.z))
                debug.takeScreenshot()
            end
        end,
    },
}
