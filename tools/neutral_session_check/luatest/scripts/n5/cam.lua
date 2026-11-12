-- tools/neutral_session_check (task 0121): for each neutral actor of the Bannered Mare (positions and facing from the
-- engine's "NACT actor" log line), stands the player in front of it at DIST units and takes a picture.
-- The list is the actors of WhiterunBanneredMare at the Skyrim SE masters; the numbers are x, y, z and the placement's z rotation.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local ACTORS = {
    { 336, -416, 64, -1.52699 },
    { 44.0677, 27.7724, 68, 2.88541 },
    { -136.195, -699.55, 69.1957, -1.68318 },
    { 66.3002, -579.985, 67.5885, -3.14159 },
    { -122.041, -55.383, 64, -3.14159 },
    { -241.652, -1071.21, 68, 0 },
}
local DIST = 260
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
                    player:sendEvent('N5Shot')
                    print('N5 shot ' .. step)
                    shotAt = nil
                end
                return
            end
            if t < 8 + step * 3 or step >= #ACTORS then return end
            step = step + 1
            local a = ACTORS[step]
            local cx, cy = -50 - a[1], -450 - a[2]
            local len = math.sqrt(cx * cx + cy * cy)
            local fx, fy = cx / len, cy / len
            local px, py = a[1] + fx * DIST, a[2] + fy * DIST
            local yaw = math.atan2(a[1] - px, a[2] - py)
            player:teleport(player.cell, util.vector3(px, py, a[3] + 2), { rotation = util.transform.rotateZ(yaw) })
            shotAt = t + 1.5
        end,
    },
}
