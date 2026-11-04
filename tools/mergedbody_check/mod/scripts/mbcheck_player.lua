-- Task 0069 stage d: the player half of tools/mergedbody_check. Fixed real-time phases: settle, a row of NPCs of every
-- race and sex (rest frames from a static camera), the player without clothes walking, running, turning and attacking
-- (third person frames), and close-ups of the player's right wrist and knee (the two welded seams). Screenshots go to
-- the user data folder; check.sh copies them. Every phase is marked with a log line "MBPHASE <name>".
local self = require('openmw.self')
local core = require('openmw.core')
local types = require('openmw.types')
local util = require('openmw.util')
local camera = require('openmw.camera')
local debug = require('openmw.debug')

local SETTLE = 6 -- seconds of real time before anything happens (cells and actors load)
-- name, start (seconds after SETTLE), screenshot (seconds after the phase starts; nil: none)
local phases = {
    { name = 'idle', at = 0, shot = 1.0 },
    { name = 'walk', at = 2, shot = 1.2 },
    { name = 'run', at = 4, shot = 1.0 },
    { name = 'turn', at = 6, shot = 0.6 },
    { name = 'stop', at = 8, shot = 1.5 },
    { name = 'attack', at = 10, shot = 0.5 },
    { name = 'wrist', at = 13, shot = 1.0 },
    { name = 'knee', at = 15, shot = 1.0 },
    -- The row last: an attack near the NPCs is a crime, and a guard's dialog would cover the close-ups.
    { name = 'spawn', at = 17 },
    { name = 'row', at = 20, shot = 2.0 },
    { name = 'end', at = 23 },
    -- The game quits by itself, so the run ends through the normal shutdown (the session and its GPU objects go
    -- before the device), and check.sh can require exit code 0 and no crash dump (review finding 3).
    { name = 'quit', at = 24 },
}

local startReal = nil
local index = 0
local shotTaken = false
local origin = nil

return {
    engineHandlers = {
        onFrame = function()
            startReal = startReal or core.getRealTime()
            local t = core.getRealTime() - startReal - SETTLE
            if t < 0 then
                return
            end
            if not origin then
                types.Actor.setEquipment(self, {})
            end
            origin = origin or self.position
            while index < #phases and t >= phases[index + 1].at do
                index = index + 1
                shotTaken = false
                print('MBPHASE ' .. phases[index].name)
                if phases[index].name == 'quit' then
                    core.quit()
                end
                if phases[index].name == 'spawn' then
                    core.sendGlobalEvent('MBCheckSpawn', { origin = origin })
                end
            end
            local phase = phases[index]
            local c = self.controls
            c.movement = 0
            c.sideMovement = 0
            c.yawChange = 0
            c.use = 0
            c.run = false
            local name = phase.name
            if name == 'row' then
                camera.setMode(camera.MODE.Static)
                camera.setStaticPosition(origin + util.vector3(0, -450, 260))
                camera.setYaw(0)
                camera.setPitch(0.28)
            elseif name == 'wrist' or name == 'knee' then
                camera.setMode(camera.MODE.Static)
                local height = (name == 'wrist') and 75 or 40
                camera.setStaticPosition(self.position + util.vector3(45, 55, height))
                camera.setYaw(math.rad(-140))
                camera.setPitch(0.05)
            else
                if camera.getMode() ~= camera.MODE.ThirdPerson then
                    camera.setMode(camera.MODE.ThirdPerson)
                end
                if name == 'walk' then
                    c.movement = 1
                elseif name == 'run' then
                    c.movement = 1
                    c.run = true
                elseif name == 'turn' then
                    c.yawChange = 0.03
                elseif name == 'attack' then
                    types.Actor.setStance(self, types.Actor.STANCE.Weapon)
                    c.use = 1
                elseif name == 'stop' then
                    types.Actor.setStance(self, types.Actor.STANCE.Nothing)
                end
            end
            if phase.shot and not shotTaken and t - phase.at >= phase.shot then
                shotTaken = true
                debug.takeScreenshot()
                print('MBSHOT ' .. name)
            end
        end,
    },
}
