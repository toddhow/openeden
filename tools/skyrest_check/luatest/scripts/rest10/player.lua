-- tools/skyrest_check (task 0210): player half: screenshots on request, and sets the vitals the scenario asks for.
local debug = require('openmw.debug')
local core = require('openmw.core')
local types = require('openmw.types')
local self = require('openmw.self')
local I = require('openmw.interfaces')
local frames = 0
local camera = require('openmw.camera')
return {
    engineHandlers = {
        onFrame = function()
            frames = frames + 1
            if frames % 240 == 0 then
                print(string.format('RESTCHECK player frame %d sim %.2f mode %s', frames, core.getSimulationTime(), tostring(I.UI.getMode())))
            end
        end,
    },
    eventHandlers = {
        RestShot = function() debug.takeScreenshot() end,
        RestLook = function(pitch) camera.setMode(camera.MODE.FirstPerson); camera.setPitch(pitch) end,
        RestHurt = function(d)
            local h = types.Actor.stats.dynamic.health(self)
            h.current = d.health
            print(string.format('RESTCHECK health set to %.1f', h.current))
        end,
    },
}
