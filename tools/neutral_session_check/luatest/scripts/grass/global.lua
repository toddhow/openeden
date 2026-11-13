-- tools/neutral_session_check (task 0153, Skyrim grass): leaves the Mare by its front door, visits spots of Whiterun's
-- plains (squares with a lot of grass in the data), looks along the ground from first person and takes a picture at each
-- after the grass had time to be placed and built. Real seconds. Spots print as GRASS lines. Run it with
-- [Game] neutral grass false and true and compare the frames and the frame times (OPENEDEN_FRAME_STATS_FILE).
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

-- x, y, yaw (0 looks north), pitch (positive looks down)
local spots = {
    { 18432, -10240, 1.5708, 0.12 }, { 18432, -10240, 3.1416, 0.30 },
    { 30720, -14336, 0.0, 0.10 }, { 30720, -14336, 1.5708, 0.30 },
    { 26624, -14336, 3.1416, 0.10 }, { 30720, -12150, 1.5708, 0.20 },
}
local start, hourSet, nextAt, index, phase = nil, false, 8, 0, 'door'
local DWELL = 14

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if not hourSet and t >= 1 then
                hourSet = true
                pcall(function() world.mwscript.getGlobalVariables(player).gamehour = 12 end)
            end
            if t < nextAt then return end
            if phase == 'door' then
                for _, obj in ipairs(player.cell:getAll()) do
                    local ok, id = pcall(function() return obj.recordId end)
                    if ok and id == 'skyrimse:b152263' then obj:activateBy(player) break end
                end
                phase = 'go'
                nextAt = t + 8
            elseif phase == 'go' then
                index = index + 1
                local s = spots[index]
                if s == nil then phase = 'done'; print('GRASS done'); return end
                local z = -2600
                local ok, h = pcall(function() return core.land.getHeightAt(util.vector3(s[1], s[2], 0), player.cell) end)
                if ok and h then z = h + 40 end
                print(string.format('GRASS spot %d at %.0f,%.0f ground %.0f', index, s[1], s[2], z))
                player:teleport(player.cell, util.vector3(s[1], s[2], z), { onGround = true })
                player:sendEvent('GLook', { yaw = s[3], pitch = s[4] })
                phase = 'shot'
                nextAt = t + DWELL
            elseif phase == 'shot' then
                player:sendEvent('GShot')
                phase = 'go'
                nextAt = t + 1.5
            end
        end,
    },
}
