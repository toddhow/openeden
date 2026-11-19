-- Task 0336: god mode so nothing ends the run; a picture when the global script asks.
local debug = require('openmw.debug')
return { eventHandlers = {
    N336God = function() if not debug.isGodMode() then debug.toggleGodMode() end end,
    N336Shot = function() debug.takeScreenshot() end,
} }
