-- tools/neutral_session_check (task 0257): the player side of the NE check: god mode, first person, a picture on request.
local debug = require('openmw.debug')
local camera = require('openmw.camera')
local cfg = require('scripts.ne.cfg')

local aimed = false

return {
    eventHandlers = {
        NEShot = function(data)
            if not aimed then
                aimed = true
                if not debug.isGodMode() then debug.toggleGodMode() end
                camera.setMode(camera.MODE.FirstPerson)
                camera.setYaw(cfg.yaw)
                camera.setPitch(cfg.pitch)
            end
            debug.takeScreenshot()
            print('NE picture ' .. tostring(data.index))
        end,
    },
}
