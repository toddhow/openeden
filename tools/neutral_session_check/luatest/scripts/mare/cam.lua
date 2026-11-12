-- tools/neutral_session_check (task 0152): visits camera spots in the Bannered Mare and takes a picture at each.
-- SPOTS are x, y, z, yaw (radians, 0 = +y), pitch (radians, positive = down). The picture is taken 1.2 s after the move.
-- Override the list with MARE_SPOTS in the file luatest/scripts/mare/spots.lua (a Lua file returning the table), if present.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local ok, spots = pcall(require, 'scripts.mare.spots')
local SPOTS = ok and spots or {
    { -128, 0, 170, 0, 0.6 },
    { -128, 300, 170, 0, 0.6 },
    { -128, 600, 170, math.pi, 0.6 },
    { 128, 200, 170, math.pi / 2, 0.6 },
    { -384, 200, 170, -math.pi / 2, 0.6 },
    { -128, 900, 170, math.pi, 0.6 },
    { 128, -1000, 170, 0, 0.6 },
    { -1000, -300, 170, -math.pi / 2, 0.6 },
}
local start, step, shotAt = nil, 0, nil

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if shotAt then
                if t >= shotAt then
                    player:sendEvent('MareShot')
                    print('MARE shot ' .. step)
                    shotAt = nil
                end
                return
            end
            if t < 8 + step * 3 or step >= #SPOTS then return end
            step = step + 1
            local s = SPOTS[step]
            player:teleport(player.cell, util.vector3(s[1], s[2], s[3]), { rotation = util.transform.rotateZ(s[4]) })
            player:sendEvent('MarePitch', s[5])
            print(string.format('MARE spot %d %.0f,%.0f,%.0f yaw %.2f pitch %.2f', step, s[1], s[2], s[3], s[4], s[5]))
            shotAt = t + 1.2
        end,
    },
}
