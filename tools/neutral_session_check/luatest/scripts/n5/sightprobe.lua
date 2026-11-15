-- tools/neutral_session_check (task 0171, combat stage S5-3b): finds where a wall or the bar counter lies between Hulda's
-- weapon and a spot round her. Casts rays from her weapon height to 16 directions at several distances through the
-- engine's own world (static layers and doors, as the combat line of sight does) and prints which are blocked and where.
-- Started by "n5sightprobe" (200 frames are enough).
local core = require('openmw.core')
local nearby = require('openmw.nearby')
local util = require('openmw.util')

local ACTOR = { 336, -416, 64 } -- Hulda's feet
local WEAPON = 80
local done = false

return {
    engineHandlers = {
        onFrame = function()
            if done or core.getRealTime() < 4 then return end
            done = true
            local from = util.vector3(ACTOR[1], ACTOR[2], ACTOR[3] + WEAPON)
            local mask = nearby.COLLISION_TYPE.World + nearby.COLLISION_TYPE.Door + nearby.COLLISION_TYPE.HeightMap
            for _, dist in ipairs({ 60, 100, 140 }) do
                local line = {}
                for k = 0, 15 do
                    local a = k / 16 * 2 * math.pi
                    -- Start just outside her own capsule (it is a world body too): 26 units out.
                    local start = util.vector3(ACTOR[1] + math.cos(a) * 26, ACTOR[2] + math.sin(a) * 26, ACTOR[3] + WEAPON)
                    local to = util.vector3(ACTOR[1] + math.cos(a) * dist, ACTOR[2] + math.sin(a) * dist, ACTOR[3] + WEAPON)
                    local hit = nearby.castRay(start, to, { collisionType = mask })
                    line[#line + 1] = hit.hit and string.format('%d:X(%.0f)', k, (hit.hitPos - from):length()) or string.format('%d:-', k)
                end
                print('N5SIGHT dist ' .. dist .. ' ' .. table.concat(line, ' '))
            end
            -- The wall to the west (direction 8): what height does it reach?
            local heights = {}
            for h = 10, 170, 10 do
                local start = util.vector3(ACTOR[1] - 26, ACTOR[2], ACTOR[3] + h)
                local to = util.vector3(ACTOR[1] - 100, ACTOR[2], ACTOR[3] + h)
                local hit = nearby.castRay(start, to, { collisionType = mask })
                heights[#heights + 1] = string.format('%d:%s', h, hit.hit and string.format('%.0f', (hit.hitPos - from):length()) or '-')
            end
            print('N5SIGHT west by height ' .. table.concat(heights, ' '))
        end,
    },
}
