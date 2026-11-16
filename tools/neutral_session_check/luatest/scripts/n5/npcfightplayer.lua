-- tools/neutral_session_check (task 0197): the player half of "n5npcfight": raises the player's health, puts a static
-- camera above and behind the fight (the point the global script sends) and takes a picture every cfg.every seconds of
-- real time, at most cfg.maxShots.
local core = require('openmw.core')
local util = require('openmw.util')
local camera = require('openmw.camera')
local self = require('openmw.self')
local types = require('openmw.types')
local cfg = require('scripts.n5.npcfightcfg')

local look, nextShot, shots, healed = nil, nil, 0, false
return {
    eventHandlers = {
        NpcFightLook = function(data) look = data end,
    },
    engineHandlers = {
        onFrame = function()
            local t = core.getRealTime()
            if not healed and t > 1 then
                healed = true
                local hp = types.Actor.stats.dynamic.health(self)
                hp.base = 100000
                hp.current = 100000
            end
            if look == nil or t < 6 or cfg.every <= 0 then return end
            if camera.getMode() ~= camera.MODE.Static then
                camera.setMode(camera.MODE.Static)
                return
            end
            local cx, cy, cz = look.x - cfg.camBack, look.y - cfg.camBack * 0.6, look.z + cfg.camUp
            camera.setStaticPosition(util.vector3(cx, cy, cz))
            camera.setYaw(math.atan2(look.x - cx, look.y - cy))
            camera.setPitch(math.atan2(cz - (look.z + 60), math.sqrt((look.x - cx) ^ 2 + (look.y - cy) ^ 2)))
            nextShot = nextShot or t
            if t >= nextShot and shots < cfg.maxShots then
                nextShot = t + cfg.every
                shots = shots + 1
                require('openmw.debug').takeScreenshot()
                print('NPCFIGHT shot ' .. shots .. ' real ' .. string.format('%.2f', t))
            end
        end,
    },
}
