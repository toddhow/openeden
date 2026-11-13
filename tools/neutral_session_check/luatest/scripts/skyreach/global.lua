-- tools/neutral_session_check (task 0157): puts the player at a list of viewpoints in the Mare, turns the navmesh debug
-- view on and takes a picture at each ("SKYR pose n").
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

-- x, y, z, yaw, pitch (pitch positive looks down)
-- WATCH: stand in front of the ground floor's WRDoor02 at (-128, 384) and take a picture every 2 s, to see it open for Mikael.
local WATCH = true
local POSES = {}
for i = 1, 40 do POSES[i] = { -150, 250, 150, 0.15, 0.1 } end
local start, nextAt, pose, stage = nil, 5, 0, 0

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if t < nextAt then return end
            if stage == 0 then
                if not WATCH then player:sendEvent('SRWire') end
                stage = 1; nextAt = t + 1; return
            end
            if stage == 1 then
                pose = pose + 1
                local p = POSES[pose]
                if p == nil then return end
                player:teleport(player.cell, util.vector3(p[1], p[2], p[3]))
                player:sendEvent('SRLook', { pitch = p[5], yaw = p[4] })
                stage = 2; nextAt = t + 1.0; return
            end
            if stage == 2 then
                player:sendEvent('SRShot'); print('SKYR pose ' .. pose); stage = 1; nextAt = t + 1.0
            end
        end,
    },
}
