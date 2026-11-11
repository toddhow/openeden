-- Task 0106 check: the target NPC dies, walks or looks for a wall when the global script says so.
local types = require('openmw.types')
local self = require('openmw.self')
local core = require('openmw.core')
local util = require('openmw.util')
local nearby = require('openmw.nearby')
local interfaces = require('openmw.interfaces')

return {
    eventHandlers = {
        RagKill = function()
            print('RAG kill ' .. self.recordId)
            types.Actor.stats.dynamic.health(self).current = 0
        end,
        RagWalk = function(data)
            print('RAG walk to ' .. tostring(data.destination))
            interfaces.AI.startPackage({ type = 'Travel', destPosition = data.destination })
        end,
        RagFindWall = function()
            -- the nearest wall: horizontal rays at chest height in 16 directions
            local origin = self.position + util.vector3(0, 0, 100)
            local best, bestDistance, bestNormal = nil, 1e9, nil
            for i = 0, 15 do
                local angle = i * math.pi / 8
                local direction = util.vector3(math.cos(angle), math.sin(angle), 0)
                local result = nearby.castRay(origin, origin + direction * 600, { ignore = self })
                if result.hit and result.hitPos ~= nil then
                    local d = (result.hitPos - origin):length()
                    if d < bestDistance then best, bestDistance, bestNormal = result.hitPos, d, result.hitNormal end
                end
            end
            if best == nil then
                print('RAG no wall found')
                return
            end
            core.sendGlobalEvent('RagWall', { hitPos = best, normal = bestNormal })
        end,
    },
}
