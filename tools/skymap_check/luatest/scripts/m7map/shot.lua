-- tools/skymap_check (task 0201): takes a screenshot when the global script asks (openmw.debug is a player-script module).
local debug = require('openmw.debug')
local core = require('openmw.core')
local I = require('openmw.interfaces')
local frames = 0
return {
    engineHandlers = {
        onFrame = function()
            frames = frames + 1
            if frames % 120 == 0 then
                print(string.format('M7MAP frame %d sim %.2f mode %s', frames, core.getSimulationTime(), tostring(I.UI.getMode())))
            end
        end,
    },
    eventHandlers = {
        M7Shot = function() debug.takeScreenshot() end,
    },
}
