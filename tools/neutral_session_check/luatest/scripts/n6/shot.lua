-- tools/neutral_session_check (task 0177): a screenshot at 5 and again at 9 real seconds of the run.
local core = require('openmw.core')
local debug = require('openmw.debug')

local start = nil
local shots = 0
return {
    engineHandlers = {
        onUpdate = function()
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if (shots == 0 and t > 5) or (shots == 1 and t > 9) then
                shots = shots + 1
                debug.takeScreenshot()
            end
        end,
    },
}
