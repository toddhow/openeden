-- tools/neutral_session_check (task 0355 stage g): visits camera spots in any interior (spots from scripts/l55g/spots.lua, written by check.sh from L55G_SPOTS) and takes a picture at each.
-- SPOTS are x, y, z, yaw (radians, 0 = +y), pitch (radians, positive = down). The picture is taken 1.2 s after the move.
-- Spots are x, y, z, yaw (0 = +y), pitch (positive = down), from scripts.l55g.spots (a Lua file returning the table), if present.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local ok, spots = pcall(require, 'scripts.l55g.spots')
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
local spawned = false

-- SPOTS.spawn = { { id = 'skyrimse:b1015997', x, y, z, rotY = 1.5708 }, ... }: objects (a light) created in the player's cell at
-- 3 real seconds, to put a chosen light record in a known place; rotY turns the placement about Y (radians).
local function spawnObjects(player)
    for _, o in ipairs(SPOTS.spawn or {}) do
        local ok2, obj = pcall(world.createObject, o.id)
        if ok2 and obj then
            local rot = util.transform.rotateY(o.rotY or 0) * util.transform.rotateZ(o.rotZ or 0)
            obj:teleport(player.cell, util.vector3(o.x, o.y, o.z), { rotation = rot })
            print(string.format('L55G spawned %s at %.0f,%.0f,%.0f', o.id, o.x, o.y, o.z))
        else
            print('L55G spawn failed for ' .. tostring(o.id) .. ': ' .. tostring(obj))
        end
    end
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if not spawned and t >= 3 then
                spawned = true
                spawnObjects(player)
            end
            if shotAt then
                if t >= shotAt then
                    player:sendEvent('MareShot')
                    print('L55G shot ' .. step)
                    shotAt = nil
                end
                return
            end
            if t < 8 + step * 3 or step >= #SPOTS then return end
            step = step + 1
            local s = SPOTS[step]
            player:teleport(player.cell, util.vector3(s[1], s[2], s[3]), { rotation = util.transform.rotateZ(s[4]) })
            player:sendEvent('MarePitch', s[5])
            print(string.format('L55G spot %d %.0f,%.0f,%.0f yaw %.2f pitch %.2f', step, s[1], s[2], s[3], s[4], s[5]))
            shotAt = t + 1.2
        end,
    },
}
