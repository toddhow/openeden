-- tools/neutral_session_check (task 0192): the player half of "n5many": from a static camera MANY_CAM_BACK units behind
-- and MANY_CAM_UP above the player, looking at it, takes a picture every MANY_EVERY seconds of real time from START.
local core = require('openmw.core')
local util = require('openmw.util')
local camera = require('openmw.camera')
local self = require('openmw.self')
local types = require('openmw.types')
local cfg = require('scripts.n5.manycfg')

local nextShot, shots, healed = nil, 0, false
return {
    engineHandlers = {
        onFrame = function()
            local t = core.getRealTime()
            if not healed and t > 1 then
                healed = true
                local hp = types.Actor.stats.dynamic.health(self)
                hp.base = 100000
                hp.current = 100000
            end
            if t < 8 or cfg.every <= 0 then return end
            if camera.getMode() ~= camera.MODE.Static then
                camera.setMode(camera.MODE.Static)
                return
            end
            local p = self.position
            local cx, cy, cz = p.x - cfg.camBack, p.y - cfg.camBack * 0.35, p.z + cfg.camUp
            camera.setStaticPosition(util.vector3(cx, cy, cz))
            camera.setYaw(math.atan2(p.x - cx, p.y - cy))
            camera.setPitch(math.atan2(cz - (p.z + 60), math.sqrt((p.x - cx) ^ 2 + (p.y - cy) ^ 2)))
            nextShot = nextShot or t
            if t >= nextShot and shots < cfg.maxShots then
                nextShot = t + cfg.every
                shots = shots + 1
                require('openmw.debug').takeScreenshot()
                print('N5MANY shot ' .. shots .. ' real ' .. string.format('%.2f', t) .. ' mode ' .. tostring(camera.getMode()) .. ' cam ' .. tostring(camera.getPosition()) .. ' player ' .. tostring(p))
            end
        end,
    },
}
