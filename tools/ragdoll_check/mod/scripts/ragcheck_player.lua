-- Task 0106 check: takes SHOTS screenshots every SHOT_STEP seconds once the global script has killed the target, a last
-- one beside the corpse after it was activated, reports the UI mode and what a rendering ray sees, then quits.
local core = require('openmw.core')
local debug = require('openmw.debug')
local camera = require('openmw.camera')
local self = require('openmw.self')
local util = require('openmw.util')
local nearby = require('openmw.nearby')
local interfaces = require('openmw.interfaces')
local params = require('scripts.ragparams')

local SHOTS = 9
local STEP = params.shotStep or 0.5
local started = nil
local shots = 0
local nextShot = 0
local godDone = false
local lootNpc = nil
local lootAt = nil
local lootShot = false

return {
    engineHandlers = {
        onFrame = function()
            if not godDone then
                debug.toggleGodMode()
                godDone = true
            end
            camera.setPitch(0.28) -- look a little down at the body
            if lootAt ~= nil then
                local t = core.getRealTime() - lootAt
                if t >= 0.5 and not lootShot then
                    print('RAG ui mode ' .. tostring(interfaces.UI.getMode()))
                    -- a rendering ray from beside the player to the corpse's position
                    local from = self.position + util.vector3(0, 0, 100)
                    local to = lootNpc.position + util.vector3(0, 0, 4)
                    nearby.asyncCastRenderingRay(
                        require('openmw.async'):callback(function(result)
                            print('RAG rendering ray hit ' .. tostring(result.hit) .. ' '
                                .. (result.hitObject and result.hitObject.recordId or 'nothing'))
                        end), from, to)
                    debug.takeScreenshot()
                    print('RAG loot shot')
                    lootShot = true
                elseif lootShot and t >= 3.0 then
                    print('RAG done')
                    core.quit()
                end
                return
            end
            if started == nil then return end
            local t = core.getSimulationTime() - started
            if t >= nextShot and shots < SHOTS then
                debug.takeScreenshot()
                shots = shots + 1
                nextShot = nextShot + STEP
                print('RAG shot ' .. shots .. ' at ' .. string.format('%.2f', t))
            elseif shots >= SHOTS and t >= 30.0 then
                print('RAG done (no loot step)')
                core.quit()
            end
        end,
    },
    eventHandlers = {
        RagStart = function()
            started = core.getSimulationTime()
            nextShot = 0
        end,
        RagLoot = function(data)
            lootNpc = data.npc
            lootAt = core.getRealTime()
        end,
    },
}
