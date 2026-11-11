-- Task 0106 check: takes a screenshot every 0.5 s for 4 s once the global script has killed the target, then quits.
local core = require('openmw.core')
local debug = require('openmw.debug')
local camera = require('openmw.camera')

local started = nil
local shots = 0
local nextShot = 0
local godDone = false

return {
    engineHandlers = {
        onFrame = function()
            if not godDone then
                debug.toggleGodMode()
                godDone = true
            end
            camera.setPitch(0.28) -- look a little down at the body
            if started == nil then return end
            local t = core.getSimulationTime() - started
            if t >= nextShot and shots < 9 then
                debug.takeScreenshot()
                shots = shots + 1
                nextShot = nextShot + 0.5
                print('RAG shot ' .. shots .. ' at ' .. string.format('%.2f', t))
            elseif shots >= 9 and t >= 24.0 then
                print('RAG done')
                core.quit()
            end
        end,
    },
    eventHandlers = {
        RagStart = function()
            started = core.getSimulationTime()
            nextShot = 0
        end,
    },
}
