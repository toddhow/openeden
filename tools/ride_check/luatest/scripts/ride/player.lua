-- tools/ride_check (task 0251): the player half. Screenshots on request, the third person camera, always-run on request.
local debug = require('openmw.debug')
local camera = require('openmw.camera')
local storage = require('openmw.storage')
local core = require('openmw.core')
local I = require('openmw.interfaces')
local self = require('openmw.self')
local going = false
local shots = 0
local started = false
return {
    engineHandlers = {
        onUpdate = function()
            -- task 0255: the forward control is the script's (simulation time, not input frames): a ride is repeatable
            if going then
                self.controls.movement = 1
                self.controls.run = true
            end
            if not started then
                started = true
                camera.setMode(camera.MODE.ThirdPerson)
                -- task 0255: the distance is the pack's own now (core.skyrimse.riding, riding_player.lua): nothing set here
            end
        end,
    },
    eventHandlers = {
        -- task 0255: the route of a ride: the navigator's path from here to a far point, sent to the global script
        RideRoute = function(dest)
            local nearby = require('openmw.nearby')
            local util = require('openmw.util')
            local status, path = nearby.findPath(require('openmw.self').position, util.vector3(dest.x, dest.y, dest.z),
                { destinationTolerance = 400 })
            local pts = {}
            for _, p in ipairs(path or {}) do pts[#pts + 1] = { x = p.x, y = p.y } end
            print(string.format('RIDE route status %s points %d', tostring(status), #pts))
            for i, p in ipairs(pts) do print(string.format('RIDE route point %d %.0f,%.0f', i, p.x, p.y)) end
            core.sendGlobalEvent('RideWaypoints', { pts = pts })
        end,
        RideGo = function(on)
            going = on and true or false
            I.Controls.overrideMovementControls(going)
        end,
        RideShot = function(name)
            shots = shots + 1
            debug.takeScreenshot()
            print(string.format('RIDE shot %d %s at %.2f', shots, tostring(name), core.getSimulationTime()))
        end,
        RideRun = function(on) storage.playerSection('SettingsOMWControls'):set('alwaysRun', on) end,
    },
}
