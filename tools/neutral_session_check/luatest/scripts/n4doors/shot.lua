-- tools/neutral_session_check (task 0115): takes a screenshot when the global step script asks.
local debug = require('openmw.debug')
return {
    eventHandlers = {
        N4DShot = function() debug.takeScreenshot() end,
    },
}
