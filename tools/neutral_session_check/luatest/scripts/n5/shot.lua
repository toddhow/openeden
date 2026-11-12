-- tools/neutral_session_check (task 0121): takes a screenshot when the global script asks.
local debug = require('openmw.debug')
return {
    eventHandlers = {
        N5Shot = function() debug.takeScreenshot() end,
    },
}
