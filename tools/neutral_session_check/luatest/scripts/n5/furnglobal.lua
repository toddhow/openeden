-- tools/neutral_session_check (task 0135 stage S6b): the global half of "n5furn". Every STEP seconds it finds the next
-- neutral actor of TARGETS in the player's cell (by the number in its record id), puts the player SIDE_DISTANCE units
-- from it on its right (OFFSETS cycle: front, right, left) looking at it, and 1.5 s later asks the player script for a
-- picture. It prints "N5F <name> at x y z yaw" with the actor's own position and rotation first.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local TARGETS = { 'b108144', 'b80802', 'b50347871' }
local DIST = 150
local STEP = 4
local SHOTS = 18
local start, nextAt, shotAt, shot = nil, nil, nil, 0

local function find(cell, tag)
    for _, o in ipairs(cell:getAll()) do
        if string.find(o.recordId, tag, 1, true) then return o end
    end
    return nil
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if shotAt then
                if t >= shotAt then
                    player:sendEvent('N5Shot')
                    print('N5F shot ' .. shot)
                    shotAt = nil
                end
                return
            end
            if t < 8 or shot >= SHOTS then return end
            nextAt = nextAt or t
            if t < nextAt then return end
            nextAt = t + STEP
            shot = shot + 1
            local tag = TARGETS[(shot - 1) % #TARGETS + 1]
            local a = find(player.cell, tag)
            if a == nil then return end
            local p = a.position
            local yaw = a.rotation:getYaw()
            local side = math.floor((shot - 1) / #TARGETS) % 3 -- 0 front, 1 right, 2 left
            local ang = yaw + (side == 1 and math.pi / 2 or side == 2 and -math.pi / 2 or 0)
            local px, py = p.x + math.sin(ang) * DIST, p.y + math.cos(ang) * DIST
            local look = math.atan2(p.x - px, p.y - py)
            print(string.format('N5F %s at %.1f %.1f %.1f yaw %.2f side %d', tag, p.x, p.y, p.z, yaw, side))
            player:teleport(player.cell, util.vector3(px, py, p.z + 2), { rotation = util.transform.rotateZ(look) })
            shotAt = t + 1.5
        end,
    },
}
