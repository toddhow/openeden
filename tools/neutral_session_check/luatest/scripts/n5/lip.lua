-- tools/neutral_session_check (task 0146): stands the player 70 units in front of Hulda (the first actor of n5/talk.lua),
-- activates her, and takes a picture of her face every SHOT_EVERY seconds of real time while she speaks. The turns are
-- pressed by OPENEDEN_NEUTRAL_DIALOGUE_PRESS. Needs --auto-quit of at least 6000 frames.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local A = { 336, -416, 64, -1.52699 } -- Hulda: x, y, z, placement rotation
local DIST = 70
local EYE = 108 -- camera height above her feet
local PITCH = 0.12
local SHOT_EVERY = 0.25
local SHOT_UNTIL = 16
local start, placed, activated, lastShot = nil, false, false, nil

local function nearest(cell)
    local best, bestD = nil, 1600
    for _, obj in ipairs(cell:getAll()) do
        local ok, id = pcall(function() return obj.recordId end)
        if ok and type(id) == 'string' and id:sub(1, 10) == 'skyrimse:b' then
            local p = obj.position
            local d = (p.x - A[1]) ^ 2 + (p.y - A[2]) ^ 2 + (p.z - A[3]) ^ 2
            if d < bestD then best, bestD = obj, d end
        end
    end
    return best
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if not placed and t >= 6 then
                placed = true
                local cx, cy = -50 - A[1], -450 - A[2]
                local len = math.sqrt(cx * cx + cy * cy)
                local px, py = A[1] + cx / len * DIST, A[2] + cy / len * DIST
                local yaw = math.atan2(A[1] - px, A[2] - py)
                player:sendEvent('N5Cam', { position = util.vector3(px, py, A[3] + EYE), yaw = yaw, pitch = PITCH })
                print('N5T placed at ' .. string.format('%.1f', t))
            end
            if placed and not activated and t >= 9 then
                activated = true
                local obj = nearest(player.cell)
                if obj then
                    print('N5T activating ' .. tostring(obj.recordId) .. ' at ' .. string.format('%.2f', t))
                    obj:activateBy(player)
                    lastShot = t
                end
            end
            if activated and lastShot and t - lastShot >= SHOT_EVERY and t < 9 + SHOT_UNTIL then
                lastShot = t
                player:sendEvent('N5Shot')
                print('N5T shot at ' .. string.format('%.2f', t))
            end
        end,
    },
}
