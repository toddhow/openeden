-- tools/neutral_session_check (task 0132): stands the player in front of ONE neutral actor of the Bannered Mare and takes
-- pictures at three times (real seconds after the start), to compare the rest pose with the looping idle.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local A = { -122.041, -55.383, 64, -3.14159 }
local DIST = 260
local SHOTS = { 10, 13.3, 17.1 }
local start, placed, n = nil, false, 0

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if not placed and t >= 8 then
                placed = true
                local cx, cy = -50 - A[1], -450 - A[2]
                local len = math.sqrt(cx * cx + cy * cy)
                local px, py = A[1] + cx / len * DIST, A[2] + cy / len * DIST
                local yaw = math.atan2(A[1] - px, A[2] - py)
                player:teleport(player.cell, util.vector3(px, py, A[3] + 2), { rotation = util.transform.rotateZ(yaw) })
            end
            if placed and n < #SHOTS and t >= SHOTS[n + 1] then
                n = n + 1
                player:sendEvent('N5Shot')
                print('N5 shot ' .. n .. ' at ' .. t)
            end
        end,
    },
}
