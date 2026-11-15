-- tools/neutral_session_check (task 0178, combat stage S5-6): stands the player DIST units east of Hulda, facing her, and
-- takes a picture every EVERY seconds of real time from START to END; in the simulation-time windows of `burst` it takes
-- one picture EVERY FRAME (an arrow at 6000 units a second crosses the room in a few frames). The settings of the
-- scenario (SETTINGS_EXTRA): combat test hostile = Hulda, combat test weapon = IronBow, combat test ammo = IronArrow,
-- combat test player bow = 1.8, combat trace = true. Started by "n5bow" (use at least 6000 frames). The numbers come from
-- scripts/n5/bowcfg.lua, written by check.sh (BOW_DIST, BOW_EVERY, BOW_BURST="t0,t1;t2,t3").
local core = require('openmw.core')
local util = require('openmw.util')
local camera = require('openmw.camera')
local cfg = require('scripts.n5.bowcfg')

local ACTOR = { 336, -416, 64 } -- Hulda
local START, END = 3, 40
local placed, nextShot, shots, burstShots = nil, nil, 0, 0

local function inBurst(sim)
    for _, w in ipairs(cfg.burst) do
        if sim >= w[1] and sim <= w[2] then return true end
    end
    return false
end

return {
    engineHandlers = {
        onFrame = function()
            local t = core.getRealTime()
            local sim = core.getSimulationTime()
            if not placed then
                if t < 6 then return end
                placed = t
                local cx, cy = 736 - ACTOR[1], -416 - ACTOR[2]
                local len = math.sqrt(cx * cx + cy * cy)
                local px, py = ACTOR[1] + cx / len * cfg.dist, ACTOR[2] + cy / len * cfg.dist
                local yaw = math.atan2(ACTOR[1] - px, ACTOR[2] - py)
                core.sendGlobalEvent('N4BPlace', { x = px, y = py, z = ACTOR[3] + 2, yaw = yaw })
                nextShot = t + START
                return
            end
            -- cfg.cam = { x, y, z, lookX, lookY, lookZ }: a static camera that looks at the flight from the side.
            if cfg.cam then
                if camera.getMode() ~= camera.MODE.Static then
                    camera.setMode(camera.MODE.Static)
                else
                    camera.setStaticPosition(util.vector3(cfg.cam[1], cfg.cam[2], cfg.cam[3]))
                    local dx, dy = cfg.cam[4] - cfg.cam[1], cfg.cam[5] - cfg.cam[2]
                    camera.setYaw(math.atan2(dx, dy))
                    camera.setPitch(math.atan2(cfg.cam[3] - cfg.cam[6], math.sqrt(dx * dx + dy * dy)))
                end
            end
            if inBurst(sim) then
                burstShots = burstShots + 1
                require('openmw.debug').takeScreenshot()
                print('N5BOW burst ' .. burstShots .. ' sim ' .. string.format('%.4f', sim))
                return
            end
            if t >= nextShot and t - placed <= END then
                nextShot = t + cfg.every
                shots = shots + 1
                require('openmw.debug').takeScreenshot()
                print('N5BOW shot ' .. shots .. ' real ' .. string.format('%.2f', t) .. ' sim ' .. string.format('%.4f', sim))
            end
        end,
    },
}
