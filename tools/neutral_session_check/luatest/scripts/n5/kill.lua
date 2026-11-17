-- tools/neutral_session_check (task 0278, kill moves; a copy of trade.lua with a third person camera: the player's body is
-- drawn only then). Started by "n5kill". Originally: task 0171, combat stage S5-3: stands the player DIST units from Hulda (on the side of the
-- room the swing scenario uses, facing her) and takes a picture every SHOT_EVERY seconds of real time from START to END,
-- while the melee test rules make her attack the player and the player swing at her (SETTINGS_EXTRA: combat test hostile
-- = Hulda, combat test weapon = IronSword, combat test player swing = 2, combat trace = true). Started by "n5trade"
-- (use at least 6000 frames).
local core = require('openmw.core')
local camera = require('openmw.camera')
local util = require('openmw.util')

local ACTOR = { 336, -416, 64 } -- Hulda
local DIST = 100
local START, END, SHOT_EVERY = 3, 30, 0.25 -- simulation seconds: the picture rate follows the frames, not the wall clock
local placed, nextShot, shots = nil, nil, 0

return {
    engineHandlers = {
        onFrame = function()
            local t = core.getSimulationTime()
            if not placed then
                if t < 6 then return end
                placed = t
                -- a static camera to the side of the two (KILL_CAM_SIDE units south of their midpoint, looking north) so both bodies show;
                -- the player's body is drawn only outside first person
                camera.setMode(camera.MODE.Static)
                -- Task 0171 S5-3b: the player stands EAST of her, in the open. West of her (where this scenario used to put it) the bar
                -- counter lies between the two, and since the line of sight is checked no blow crosses it (see tradewall.lua).
                local cx, cy = 736 - ACTOR[1], -416 - ACTOR[2]
                local len = math.sqrt(cx * cx + cy * cy)
                local px, py = ACTOR[1] + cx / len * DIST, ACTOR[2] + cy / len * DIST
                local yaw = math.atan2(ACTOR[1] - px, ACTOR[2] - py)
                core.sendGlobalEvent('N4BPlace', { x = px, y = py, z = ACTOR[3] + 2, yaw = yaw })
                nextShot = t + START
                return
            end
            -- the mode change takes a frame; the pose is set every frame once it holds (a script cannot set it before)
            if camera.getMode() == camera.MODE.Static then
                camera.setStaticPosition(util.vector3(ACTOR[1] + 50, ACTOR[2] + 200, ACTOR[3] + 120))
                camera.setYaw(math.pi)
                camera.setPitch(0.12)
            else
                camera.setMode(camera.MODE.Static)
            end
            if t >= nextShot and t - placed <= END then
                nextShot = t + SHOT_EVERY
                shots = shots + 1
                require('openmw.debug').takeScreenshot()
                print('N5KILL shot ' .. shots .. ' real ' .. string.format('%.2f', t))
            end
        end,
    },
}
