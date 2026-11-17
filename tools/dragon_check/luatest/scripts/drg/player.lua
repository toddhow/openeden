-- tools/skydeaths_check (task 0254): the player half. DRGShot: a picture from a free camera cfg.cam units away from the point
-- the global script sends, looking at it; DRGShotLater (load phase): a picture of whatever the screen shows 1.5 real seconds
-- after the activation (the loot window).
local camera = require('openmw.camera')
local debug = require('openmw.debug')
local util = require('openmw.util')
local core = require('openmw.core')
local cfg = require('scripts.drg.cfg')

local pending, later = nil, nil

return {
    eventHandlers = {
        DRGShot = function(data)
            local mid = util.vector3(data.x, data.y, data.z + 30)
            local from = mid + util.vector3(cfg.cam[1], cfg.cam[2], cfg.cam[3])
            local d = mid - from
            camera.setMode(camera.MODE.Static, true)
            camera.setStaticPosition(from)
            camera.setYaw(math.atan2(d.x, d.y))
            camera.setPitch(math.atan2(-d.z, math.sqrt(d.x * d.x + d.y * d.y)))
            pending = { frames = 6 }
        end,
        DRGShotLater = function() later = core.getRealTime() + 1.5 end,
    },
    engineHandlers = {
        onFrame = function()
            if pending ~= nil then
                camera.setMode(camera.MODE.Static, true)
                pending.frames = pending.frames - 1
                if pending.frames == 0 then
                    debug.takeScreenshot()
                    pending = nil
                end
            end
            if later ~= nil and core.getRealTime() > later then
                later = nil
                debug.takeScreenshot()
            end
        end,
    },
}
