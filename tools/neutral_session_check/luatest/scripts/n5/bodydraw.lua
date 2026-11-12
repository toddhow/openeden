-- tools/neutral_session_check (task 0133): the collision debug view in front of three neutral actors of the Bannered Mare,
-- so a frame shows the capsule round the drawn NPC. Started when the check is given "n5bodydraw".
local core = require('openmw.core')
local self = require('openmw.self')
local debug = require('openmw.debug')
local util = require('openmw.util')

-- x, y, z, z rotation of the actors (task 0121's list); the player stands DIST units away on the side of the cell's middle.
local ACTORS = {
    { -136.195, -699.55, 69.1957 },
    { -241.652, -1071.21, 68 },
    { 66.3002, -579.985, 67.5885 },
}
local DIST = 230
local toggled, start, step, shotAt = false, nil, 0, nil

return {
    engineHandlers = {
        onFrame = function()
            if not toggled then
                toggled = true
                debug.toggleRenderMode(debug.RENDER_MODE.CollisionDebug)
            end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if shotAt then
                if t >= shotAt then
                    debug.takeScreenshot()
                    print('N5B shot ' .. step)
                    shotAt = nil
                end
                return
            end
            if t < 8 + step * 3 or step >= #ACTORS then return end
            step = step + 1
            local a = ACTORS[step]
            local cx, cy = -50 - a[1], -450 - a[2]
            local len = math.sqrt(cx * cx + cy * cy)
            local px, py = a[1] + cx / len * DIST, a[2] + cy / len * DIST
            local yaw = math.atan2(a[1] - px, a[2] - py)
            core.sendGlobalEvent('N4BPlace', { x = px, y = py, z = a[3] + 2, yaw = yaw })
            shotAt = t + 1.5
        end,
    },
}
