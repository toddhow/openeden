-- tools/neutral_session_check (task 0157, 0159): puts the player at a list of viewpoints in the Mare, optionally turns a
-- debug view on (SRWire: navmesh; SRCol: collision shapes; SRRecast: the recast mesh) and takes a picture at each
-- ("SKYR pose n").
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

-- x, y, z, yaw, pitch (pitch positive looks down), view ('', 'col', 'nav', 'recast')
local WATCH = true
local POSES = {
    { -1044, -258, 336, math.pi, 0.45, 'col' },
    { -1044, -258, 336, math.pi, 0.45, 'nav' },
    { -1044, -258, 336, math.pi, 0.45, 'recast' },
    { -1044, -258, 336, math.pi / 2, 0.2, 'col' },
    { -1044, -258, 336, math.pi / 2, 0.2, 'nav' },
    { -1044, -258, 336, math.pi / 2, 0.2, 'recast' },
}
local start, nextAt, pose, stage = nil, 5, 0, 0
local current = ''

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
                if p[6] ~= current then
                    if current ~= '' then player:sendEvent('SRView', current) end
                    current = p[6]
                    if current ~= '' then player:sendEvent('SRView', current) end
                end
                player:teleport(player.cell, util.vector3(p[1], p[2], p[3]))
                player:sendEvent('SRLook', { pitch = p[5], yaw = p[4] })
                stage = 2; nextAt = t + 1.5; return
            end
            if stage == 2 then
                player:sendEvent('SRShot'); print('SKYR pose ' .. pose); stage = 1; nextAt = t + 1.0
            end
        end,
    },
}
