-- Drives the three runs of tools/extension_data_check/check.sh, choosing by which saves exist.
local core = require('openmw.core')
local menu = require('openmw.menu')

local FRAMES_BETWEEN_STEPS = 60

local function countSaves()
    local count = 0
    for _, directory in pairs(menu.getAllSaves()) do
        for _ in pairs(directory) do
            count = count + 1
        end
    end
    return count
end

local function steps()
    local directory = ' - 1'
    local saves = countSaves()
    if saves == 0 then
        return {
            function() menu.newGame() end,
            function() menu.saveGame('a') end,
            function() menu.loadGame(directory, 'a.omwsave') end,
        }
    elseif saves == 1 then
        return {
            function() menu.loadGame(directory, 'a.omwsave') end,
            function() menu.saveGame('b') end,
        }
    end
    return {
        function() menu.loadGame(directory, 'b.omwsave') end,
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
