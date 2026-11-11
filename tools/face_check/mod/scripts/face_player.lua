-- Task 0071 check: after the global script made the NPC speak, take a screenshot every STEP simulated seconds, SHOTS
-- times, then quit. The camera looks level at the NPC's face.
local core = require('openmw.core')
local debug = require('openmw.debug')
local camera = require('openmw.camera')
local params = require('scripts.faceparams')

-- With no voice line (a frame comparison under OPENEDEN_FIXED_FRAME_TIME) the simulated clock is used, so that every run
-- takes its screenshots on the same frames.
local function now()
    if params.voice == 'none' then return core.getSimulationTime() end
    return core.getRealTime()
end

local SHOTS = params.shots or 26
local STEP = params.shotStep or 0.1
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
            camera.setPitch(params.pitch or 0.0)
            if started == nil then return end
            local t = now() - started
            if t >= nextShot and shots < SHOTS then
                debug.takeScreenshot()
                shots = shots + 1
                nextShot = nextShot + STEP
                print('FACE shot ' .. shots .. ' at ' .. string.format('%.3f', t))
            elseif shots >= SHOTS and t >= nextShot + 1.0 then
                print('FACE done')
                core.quit()
            end
        end,
    },
    eventHandlers = {
        FaceStart = function()
            started = now()
            nextShot = 0
        end,
    },
}
