-- tools/wordwall_check (task 0256): takes screenshots shortly after the word is learned and after the first shout.
local core = require('openmw.core')
local debug = require('openmw.debug')
local storage = require('openmw.storage')

local shots = {}

return {
    engineHandlers = {
        onFrame = function()
            local section = storage.globalSection('WW256')
            local now = core.getSimulationTime()
            local learned, shouted = section:get('learnedAt'), section:get('shoutedAt')
            if learned and not shots.a and now >= learned + 0.4 then
                shots.a = true
                debug.takeScreenshot()
            end
            if shouted and not shots.b and now >= shouted + 0.5 then
                shots.b = true
                debug.takeScreenshot()
            end
        end,
    },
}
