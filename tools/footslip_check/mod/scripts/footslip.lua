-- The scripted scenario of tools/footslip_check (task 0108): the player starts, accelerates, stops, switches from walk
-- to run, walks an arc, turns sharply, strafes and reverses. Phases last a fixed number of frames. The log line
-- "CMPHASE <name> t=<seconds>" marks each change; slip.py reads it with the game's CMTRACE lines.
local self = require('openmw.self')
local core = require('openmw.core')
local camera = require('openmw.camera')
local util = require('openmw.util')
local debug = require('openmw.debug')
local nearby = require('openmw.nearby')

local settleSeconds = 3

-- SHOTS: a static side camera at foot height and a screenshot every `shotEvery` frames of the phases named in
-- `shotPhases` (tools/footslip_check/check.sh sets SHOTS=1 by rewriting the next line in its copy of this script).
local shots = false
local shotEvery = 6
local shotPhases = { stop_wr = true }
-- the camera follows the player at a fixed offset (west of him, looking east, at foot height): the actor stays in the
-- middle of the picture and the ground texture moves past, so a foot that slides over the ground is visible

-- movement: forward (1) / back (-1); side: strafe; yaw: radians per frame; run: boolean; at: where the phase starts
-- ('corridor' facing the long free corridor south of ToddTest's hall, 1600 units; 'hall' in the middle of the hall,
-- 200 units of room all round). Teleports happen only at the start of a phase that follows a stop.
local phases = {
    { name = 'idle', frames = 60 },
    { name = 'walk', frames = 150, movement = 1, at = 'corridor' },
    { name = 'stop_walk', frames = 90 },
    { name = 'run', frames = 150, movement = 1, run = true, at = 'corridor' },
    { name = 'stop_run', frames = 90 },
    { name = 'walk_then', frames = 60, movement = 1, at = 'corridor' },
    { name = 'walk_to_run', frames = 90, movement = 1, run = true },
    { name = 'stop_wr', frames = 90 },
    { name = 'arc_walk', frames = 90, movement = 1, yaw = 0.0178, at = 'hall' },
    { name = 'stop_arc', frames = 60 },
    { name = 'arc_run', frames = 60, movement = 1, run = true, yaw = -0.03, at = 'hall' },
    { name = 'stop_arcrun', frames = 90 },
    { name = 'strafe_left', frames = 90, side = -1, at = 'hall' },
    { name = 'stop_strafe', frames = 60 },
    { name = 'walk_pre_rev', frames = 90, movement = 1, at = 'corridor' },
    { name = 'reverse_walk', frames = 90, movement = -1 },
    { name = 'stop_rev', frames = 90 },
    { name = 'sharp_turn', frames = 60, movement = 1, run = true, yaw = 0.04, at = 'hall' },
    { name = 'stop_turn', frames = 90 },
    { name = 'end', frames = 1000000 },
}

local spots = {
    corridor = { position = util.vector3(2176, 3648, -191), yaw = math.pi },
    hall = { position = util.vector3(2176, 3648, -191), yaw = 0 },
}

local startTime = nil
local index = 1
local phaseFrame = 0

local function enter(phase)
    if phase.at then
        local spot = spots[phase.at]
        core.sendGlobalEvent('FootSlipTeleport', { cell = self.cell.name, position = spot.position, yaw = spot.yaw })
    end
    print('CMPHASE ' .. phase.name .. ' t=' .. string.format('%.3f', core.getSimulationTime() - startTime))
end

return {
    engineHandlers = {
        onFrame = function()
            if not startTime then
                if core.getSimulationTime() <= settleSeconds then return end
                startTime = core.getSimulationTime()
                enter(phases[index])
            end
            if not shots and camera.getMode() ~= camera.MODE.ThirdPerson then camera.setMode(camera.MODE.ThirdPerson) end
            phaseFrame = phaseFrame + 1
            if phaseFrame > phases[index].frames and index < #phases then
                index = index + 1
                phaseFrame = 1
                enter(phases[index])
            end
            local p = phases[index]
            local c = self.controls
            c.movement = p.movement or 0
            c.sideMovement = p.side or 0
            c.yawChange = p.yaw or 0
            c.use = 0
            c.run = p.run or false
            if shots then
                -- the side with more free room, at most 150 units away, looking at the actor
                local from = self.position + util.vector3(0, 0, 45)
                local function free(dx)
                    local r = nearby.castRay(from, from + util.vector3(dx * 400, 0, 0),
                        { collisionType = nearby.COLLISION_TYPE.World })
                    return r.hit and (r.hitPos - from):length() or 400
                end
                local west, east = free(-1), free(1)
                local side = west >= east and -1 or 1
                local dist = math.min(150, math.max(40, (side == -1 and west or east) - 15))
                if camera.getMode() ~= camera.MODE.Static then camera.setMode(camera.MODE.Static) end
                camera.setStaticPosition(from + util.vector3(side * dist, 0, 0))
                camera.setYaw(side == -1 and math.pi / 2 or -math.pi / 2)
                camera.setPitch(0.1)
                if shotPhases[p.name] and phaseFrame % shotEvery == 0 then debug.takeScreenshot() end
            end
            if p.at and phaseFrame <= 6 then -- the teleport lands within a few frames; do not move before it
                c.movement, c.sideMovement, c.yawChange, c.run = 0, 0, 0, false
            end
        end,
    },
}
