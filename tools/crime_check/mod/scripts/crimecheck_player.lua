-- Task 0098 move 7 check: takes one screenshot after the crimes of crimecheck_global.lua so that the scene can be viewed.
local core = require('openmw.core')
local debug = require('openmw.debug')

local startReal = nil
local done = false

return {
    engineHandlers = {
        onFrame = function()
            startReal = startReal or core.getRealTime()
            if not done and core.getRealTime() - startReal > 9 then
                done = true
                debug.takeScreenshot()
            end
        end,
    },
}
