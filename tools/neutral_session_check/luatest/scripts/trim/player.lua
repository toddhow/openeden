-- tools/neutral_session_check (task 0151): the player side of the floating slab scenario. Aims, then 1.5 s later prints what the physics
-- heightmap and the rendering ray hit in a column grid over the slab, and takes a picture.
local debug = require('openmw.debug')
local core = require('openmw.core')
local camera = require('openmw.camera')
local nearby = require('openmw.nearby')
local util = require('openmw.util')

local shotAt = nil
local SX, SY = 104403, 18631

local function col(label, x, y)
    local from = util.vector3(x, y, 2000)
    local to = util.vector3(x, y, -20000)
    local h = nearby.castRay(from, to, { collisionType = nearby.COLLISION_TYPE.HeightMap })
    local r = nearby.castRenderingRay(from, to)
    print(string.format('TRIM col %s %.0f,%.0f heightmap %s render %s %s', label, x, y,
        h.hit and string.format('%.1f', h.hitPos.z) or 'none',
        r.hit and string.format('%.1f', r.hitPos.z) or 'none', r.hitObject and r.hitObject.recordId or '(terrain)'))
end

return {
    eventHandlers = {
        TrimAim = function(data)
            camera.setMode(camera.MODE.FirstPerson)
            camera.setYaw(data.yaw)
            camera.setPitch(-data.pitch)
            shotAt = core.getRealTime() + 1.5
        end,
    },
    engineHandlers = {
        onFrame = function()
            if shotAt ~= nil and core.getRealTime() >= shotAt then
                shotAt = nil
                for dx = -800, 800, 400 do
                    for dy = -800, 800, 400 do col(string.format('%d,%d', dx, dy), SX + dx, SY + dy) end
                end
                debug.takeScreenshot()
                print('TRIM shot')
            end
        end,
    },
}
