-- tools/ride_check (task 0251): the player half. Screenshots on request, the third person camera, always-run on request.
local debug = require('openmw.debug')
local camera = require('openmw.camera')
local storage = require('openmw.storage')
local core = require('openmw.core')
local I = require('openmw.interfaces')
local shots = 0
local started = false
return {
    engineHandlers = {
        onUpdate = function()
            if not started then
                started = true
                camera.setMode(camera.MODE.ThirdPerson)
                if I.Camera and I.Camera.setBaseThirdPersonDistance then I.Camera.setBaseThirdPersonDistance(420) end
            end
        end,
    },
    eventHandlers = {
        RideShot = function(name)
            shots = shots + 1
            debug.takeScreenshot()
            print(string.format('RIDE shot %d %s at %.2f', shots, tostring(name), core.getSimulationTime()))
        end,
        RideRun = function(on) storage.playerSection('SettingsOMWControls'):set('alwaysRun', on) end,
    },
}
