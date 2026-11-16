-- Task 0234: a one-off probe for a spot the activate key can reach a placed book from. With scripts/inv/pitchcfg.lua
-- carrying `sweep = { x, y, z, recordId }` it casts rendering rays from a ring of points round the target to its centre
-- and prints the points from which the first thing hit is the target (and the floor under each point, so that an eye
-- height of about 127 above it is a standing player). Prints nothing without `sweep`.
local nearby = require('openmw.nearby')
local util = require('openmw.util')
local ok, cfg = pcall(require, 'scripts.inv.pitchcfg')
if not ok or cfg.sweep == nil then return {} end
local frames = 0
return {
    engineHandlers = {
        onFrame = function()
            frames = frames + 1
            if frames ~= 700 then return end
            local tx, ty, tz, id = cfg.sweep[1], cfg.sweep[2], cfg.sweep[3], cfg.sweep[4]
            local target = util.vector3(tx, ty, tz)
            local found = 0
            for _, dist in ipairs({ 60, 90, 120, 160 }) do
                for k = 0, 15 do
                    local a = k * math.pi / 8
                    for _, dz in ipairs({ 20, 45, 70, 100 }) do
                        local from = util.vector3(tx + math.cos(a) * dist, ty + math.sin(a) * dist, tz + dz)
                        local r = nearby.castRenderingRay(from, target, { ignore = nil })
                        if r.hit and r.hitObject ~= nil and tostring(r.hitObject.recordId) == id then
                            local floor = nearby.castRenderingRay(from, from - util.vector3(0, 0, 400))
                            local fz = floor.hit and floor.hitPos.z or 0/0
                            found = found + 1
                            if found <= 12 then
                                print(string.format('SWEEP hit from %.0f,%.0f,%.0f (dist %d angle %d dz %d) floor %.0f eye %.0f', from.x, from.y, from.z, dist, k * 22.5, dz, fz, from.z - fz))
                            end
                        end
                    end
                end
            end
            print('SWEEP done: ' .. found .. ' hits for ' .. id)
        end,
    },
}
