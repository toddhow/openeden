-- Task 0106 check: the target NPC dies when the global script says so.
local types = require('openmw.types')
local self = require('openmw.self')

return {
    eventHandlers = {
        RagKill = function()
            print('RAG kill ' .. self.recordId)
            types.Actor.stats.dynamic.health(self).current = 0
        end,
    },
}
