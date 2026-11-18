-- tools/skysummons_check (task 0250): the player half. A picture is taken on the global script's DVShot event, from a free
-- camera standing cfg.camera (dx, dy, dz) away from the point the global script sends (the middle of the action), looking at
-- it; with no camera in the config the picture is the player's own view. The camera is put back to first person afterwards.
local camera = require('openmw.camera')
local debug = require('openmw.debug')
local util = require('openmw.util')
local core = require('openmw.core')
local cfg = require('scripts.dv.cfg')

local pending = nil
local equipCount = 0
local function lookAt(from, to)
    local d = to - from
    local yaw = math.atan2(d.x, d.y)
    local pitch = math.atan2(-d.z, math.sqrt(d.x * d.x + d.y * d.y))
    return yaw, pitch
end

return {
    eventHandlers = {
        DVShot = function(data)
            if cfg.camera ~= nil and data ~= nil and data.x ~= nil then
                local c = cfg.camera[(data.n % #cfg.camera) + 1]
                local mid = util.vector3(data.x, data.y, data.z)
                local from = mid + util.vector3(c[1], c[2], c[3])
                local yaw, pitch = lookAt(from, mid)
                camera.setMode(camera.MODE.Static, true)
                camera.setStaticPosition(from)
                camera.setYaw(yaw)
                camera.setPitch(pitch)
                pending = { frames = 3 }
            else
                debug.takeScreenshot()
            end
        end,
    },
    engineHandlers = {
        onFrame = function()
            -- Task 0300: the items of cfg.equip (hex ids, held through `inventory test items`) are wielded at EQUIP_AT seconds of simulation time
            -- the first requests only start the pack's item table (it answers `not equippable` until that is read, at a
            -- real time that differs from run to run): four requests, four seconds of simulation time apart
            equipCount = equipCount or 0
            if equipCount < 4 and cfg.equip ~= nil and #cfg.equip > 0 and core.getSimulationTime() >= (cfg.equipAt or 3) + 4 * equipCount then
                equipCount = equipCount + 1
                for _, id in ipairs(cfg.equip) do core.sendGlobalEvent('SkyrimInventoryEquip', { item = id }) end
                print('DV equip ' .. table.concat(cfg.equip, ','))
            end
            if cfg.camera ~= nil and pending ~= nil then
                camera.setMode(camera.MODE.Static, true)
                pending.frames = pending.frames - 1
                if pending.frames == 0 then
                    debug.takeScreenshot()
                    pending = nil
                end
            end
        end,
    },
}
