-- Drives the runs of tools/plugin_example_check/check.sh, choosing by whether a save exists.
local core = require('openmw.core')
local menu = require('openmw.menu')

local FRAMES_BETWEEN_STEPS = 60

local function hasSaves()
    for _, directory in pairs(menu.getAllSaves()) do
        for _ in pairs(directory) do
            return true
        end
    end
    return false
end

local function steps()
    local directory = ' - 1'
    if not hasSaves() then
        return {
            function() menu.newGame() end,
            function() menu.saveGame('a') end,
            function() menu.loadGame(directory, 'a.omwsave') end,
        }
    end
    return {
        function() menu.loadGame(directory, 'a.omwsave') end,
    }
end

local plan
local nextStep = 1
local wait = 0

return {
    engineHandlers = {
        onFrame = function()
            wait = wait + 1
            if wait < FRAMES_BETWEEN_STEPS then
                return
            end
            wait = 0
            plan = plan or steps()
            if nextStep > #plan then
                core.quit()
                return
            end
            plan[nextStep]()
            nextStep = nextStep + 1
        end,
    },
}
