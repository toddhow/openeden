-- tools/neutral_session_check (task 0257, Night Eye): leaves the Bannered Mare by its front door into WhiterunWorld,
-- sets the game hour (NE_HOUR, default 23) with the clock stopped, and then asks the player script for a picture every
-- `every` simulation seconds, `count` times, starting `first` seconds after the door. The cast itself is the pack's test
-- rule (combat test player spell / combat test player cast every): this script never casts. Logs "NE ..." lines.
local core = require('openmw.core')
local world = require('openmw.world')
local cfg = require('scripts.ne.cfg')

local DOORBASE = 'skyrimse:b152263'
local step = 0
local shots = 0
local nextShot = 0
local t0 = nil

local function nearestDoor(cell, base, near)
    local best, bestD = nil, math.huge
    for _, obj in ipairs(cell:getAll()) do
        local ok, id = pcall(function() return obj.recordId end)
        if ok and id == base then
            local p = obj.position
            local d = (p.x - near[1]) ^ 2 + (p.y - near[2]) ^ 2 + (p.z - near[3]) ^ 2
            if d < bestD then best, bestD = obj, d end
        end
    end
    return best
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            local t = core.getSimulationTime()
            t0 = t0 or t
            local rel = t - t0
            if step == 0 and rel >= 6 then
                step = 1
                world.mwscript.getGlobalVariables(player).gamehour = cfg.hour
                world.mwscript.getGlobalVariables(player).timescale = 0
                local door = nearestDoor(player.cell, DOORBASE, { -128, -1152, 120 })
                if door == nil then print('NE door not found') else
                    print('NE activating the front door')
                    door:activateBy(player)
                end
            elseif step == 1 and rel >= 14 then
                step = 2
                world.mwscript.getGlobalVariables(player).gamehour = cfg.hour
                nextShot = rel + cfg.first
                print(string.format('NE outside at sim time %.2f', t))
            elseif step == 2 and rel >= nextShot then
                shots = shots + 1
                print(string.format('NE shot %d sim time %.2f', shots, t))
                player:sendEvent('NEShot', { index = shots })
                nextShot = rel + cfg.every
                if shots >= cfg.count then step = 3 end
            end
        end,
    },
}
