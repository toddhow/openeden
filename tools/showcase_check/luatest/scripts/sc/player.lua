-- tools/showcase_check (task 0360): the player half. ScSetup { first, yaw, pitch, move }, ScCam { x, y, z, lx, ly, lz } (a static
-- camera, sent every frame while a shot runs), ScShot (a picture three frames later, after the camera moved).
local camera = require('openmw.camera')
local debug = require('openmw.debug')
local util = require('openmw.util')
local self = require('openmw.self')
local I = require('openmw.interfaces')
local noHud = pcall(require, 'scripts.sc.nohud')
local hudHidden = false

local setup, cam, shotIn, moving, turning = nil, nil, nil, nil, nil

return {
    eventHandlers = {
        ScSetup = function(d) setup = d; cam = nil; moving = d.move; turning = d.turn end,
        ScCam = function(d) cam = d end,
        ScShot = function() shotIn = 3 end,
    },
    engineHandlers = {
        onFrame = function()
            if noHud and not hudHidden then hudHidden = true; I.UI.setHudVisibility(false) end
            if moving then
                self.controls.movement = 1
                self.controls.run = (moving == 'run')
            else
                self.controls.movement = 0
            end
            if turning then self.controls.yawChange = turning * 0.016667 end
            if setup and setup.first then
                if camera.getMode() ~= camera.MODE.FirstPerson then camera.setMode(camera.MODE.FirstPerson) end
                camera.setYaw(setup.yaw)
                camera.setPitch(setup.pitch)
            elseif cam then
                if camera.getMode() ~= camera.MODE.Static then camera.setMode(camera.MODE.Static); return end
                camera.setStaticPosition(util.vector3(cam.x, cam.y, cam.z))
                local dx, dy = cam.lx - cam.x, cam.ly - cam.y
                camera.setYaw(math.atan2(dx, dy))
                camera.setPitch(math.atan2(cam.z - cam.lz, math.sqrt(dx * dx + dy * dy)))
            end
            if shotIn then
                shotIn = shotIn - 1
                if shotIn <= 0 then shotIn = nil; debug.takeScreenshot() end
            end
        end,
    },
}
