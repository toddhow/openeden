-- Task 0095, stage W-d check: the player script of the scenario: god mode, camera pitch, one screenshot per pose a moment
-- after the teleport (so the cell around is loaded), quit at the end.
local core = require('openmw.core')
local debug = require('openmw.debug')
local camera = require('openmw.camera')
local nearby = require('openmw.nearby')
local util = require('openmw.util')

local godDone = false
local pitch = 0.4
local shotAt = nil
local label = nil
local quitAt = nil
local probes = nil

return {
    engineHandlers = {
        onFrame = function()
            if not godDone then
                debug.toggleGodMode()
                camera.setMode(camera.MODE.FirstPerson)
                godDone = true
            end
            camera.setPitch(pitch)
            local now = core.getSimulationTime()
            if shotAt and now >= shotAt then
                shotAt = nil
                debug.takeScreenshot()
                print('PM shot ' .. label)
                for _, p in ipairs(probes or {}) do
                    local result = nearby.castRay(util.vector3(p[1], p[2], p[3] + 3000), util.vector3(p[1], p[2], p[3] - 3000),
                        { collisionType = nearby.COLLISION_TYPE.HeightMap })
                    print(string.format('PM probe %.0f %.0f baked %.2f ray %s', p[1], p[2], p[3],
                        result.hit and string.format('%.2f', result.hitPos.z) or 'none'))
                end
            end
            if quitAt and now >= quitAt then
                print('PM done')
                core.quit()
            end
        end,
    },
    eventHandlers = {
        PMPose = function(data)
            pitch = data.pitch
            label = data.index .. ' ' .. data.name
            probes = data.probes
            -- Shoot 0.8 s after the teleport (every hold must be longer): the cell is loaded and the camera has settled.
            shotAt = core.getSimulationTime() + 0.8
        end,
        PMQuit = function()
            quitAt = core.getSimulationTime() + 1.0
        end,
    },
}
