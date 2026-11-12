-- tools/neutral_session_check (task 0152): sets the camera pitch and takes a screenshot when the global script asks; with
-- MARE_PROBE (always on here) it also casts a rendering ray through every 12th pixel of the picture and prints the pixels
-- whose ray hits nothing ("MAREHOLE row: x0-x1 at y") with the hit positions on both sides of each run, so the hole's
-- place in the world is known.
local debug = require('openmw.debug')
local camera = require('openmw.camera')
local nearby = require('openmw.nearby')
local util = require('openmw.util')
local ui = require('openmw.ui')

local shots = 0

local function probe()
    local size = ui.screenSize()
    local origin = camera.getPosition()
    local step = 12
    for py = 0, size.y - 80, step do
        local row = {}
        for px = 0, size.x - 1, step do
            local dir = camera.viewportToWorldVector(util.vector2(px / size.x, py / size.y))
            local r = nearby.castRay(origin, origin + dir * 4000, { collisionType = nearby.COLLISION_TYPE.World })
            row[#row + 1] = r and r.hit and r.hitPos or false
        end
        local i = 1
        while i <= #row do
            if row[i] == false then
                local j = i
                while j + 1 <= #row and row[j + 1] == false do j = j + 1 end
                if i > 1 and j < #row then
                    local a, b = row[i - 1], row[j + 1]
                    print(string.format('MAREHOLE shot %d y=%d x=%d..%d  left %.0f,%.0f,%.0f  right %.0f,%.0f,%.0f', shots, py,
                        (i - 1) * step, (j - 1) * step, a.x, a.y, a.z, b.x, b.y, b.z))
                end
                i = j + 1
            else
                i = i + 1
            end
        end
    end
end

return {
    eventHandlers = {
        MarePitch = function(pitch)
            camera.setMode(camera.MODE.FirstPerson)
            camera.setPitch(pitch)
        end,
        MareShot = function()
            debug.takeScreenshot()
            probe()
            shots = shots + 1
        end,
    },
}
