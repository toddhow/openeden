-- tools/skymap_check (task 0201): a Morrowind scene for the "Morrowind unchanged" comparison. Frame 200: a screenshot of the
-- plain scene; frame 300: opens the map window (the engine's local and world maps); frame 420: a screenshot of it.
local debug = require('openmw.debug')
local I = require('openmw.interfaces')
local frames = 0
return {
    engineHandlers = {
        onFrame = function()
            frames = frames + 1
            if frames == 200 then debug.takeScreenshot() end
            if frames == 300 then I.UI.setMode('Interface', { windows = { 'Map' } }) end
            if frames == 420 then debug.takeScreenshot() end
        end,
    },
}
