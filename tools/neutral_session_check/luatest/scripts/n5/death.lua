-- tools/neutral_session_check (task 0174, combat stage S5-5): the player fights Hulda where the trade scenario puts it
-- (100 units east of her; the melee test settings in SETTINGS_EXTRA make her and the player trade blows until she
-- dies and, with [Physics] creativemotion ragdolls on, falls as a ragdoll). DEATH_AFTER seconds of real time later the
-- camera goes static and looks at where she died from four sides, SHOTS pictures SHOT_EVERY seconds apart from each
-- (the player does not move). Started by "n5death" (use at least 9000 frames).
local core = require('openmw.core')
local util = require('openmw.util')
local camera = require('openmw.camera')

local ACTOR = { 336, -416, 64 } -- Hulda
local DIST = 100
local DEATH_AFTER = 11
local SHOT_EVERY, SHOTS = 0.6, 3
local VIEW_DIST, VIEW_HEIGHT = 200, 40
local SIDES = { { 1, 0.6 }, { 0.8, -1 }, { -1, -0.2 }, { 0.2, 1 } } -- directions from her place to the camera
local placed, side, shots, nextShot = nil, 0, 0, nil

local function look(dir)
    local len = math.sqrt(dir[1] * dir[1] + dir[2] * dir[2])
    local cx, cy, cz = ACTOR[1] + dir[1] / len * VIEW_DIST, ACTOR[2] + dir[2] / len * VIEW_DIST, ACTOR[3] + VIEW_HEIGHT
    if camera.getMode() ~= camera.MODE.Static then
        camera.setMode(camera.MODE.Static)
        return false
    end
    camera.setStaticPosition(util.vector3(cx, cy, cz))
    camera.setYaw(math.atan2(ACTOR[1] - cx, ACTOR[2] - cy))
    camera.setPitch(math.atan2(cz - (ACTOR[3] + 10), VIEW_DIST))
    return true
end

return {
    engineHandlers = {
        onFrame = function()
            local t = core.getRealTime()
            if not placed then
                if t < 6 then return end
                placed = t
                local cx, cy = 736 - ACTOR[1], -416 - ACTOR[2]
                local len = math.sqrt(cx * cx + cy * cy)
                local px, py = ACTOR[1] + cx / len * DIST, ACTOR[2] + cy / len * DIST
                local yaw = math.atan2(ACTOR[1] - px, ACTOR[2] - py)
                core.sendGlobalEvent('N4BPlace', { x = px, y = py, z = ACTOR[3] + 2, yaw = yaw })
                return
            end
            if t - placed < DEATH_AFTER or side > #SIDES then return end
            if side == 0 or shots >= SHOTS then
                side = side + 1
                shots = 0
                if side > #SIDES then return end
                nextShot = t + 1.0
            end
            if look(SIDES[side]) and t >= nextShot then
                nextShot = t + SHOT_EVERY
                shots = shots + 1
                require('openmw.debug').takeScreenshot()
                print('N5DEATH side ' .. side .. ' shot ' .. shots .. ' real ' .. string.format('%.2f', t))
            end
        end,
    },
}
