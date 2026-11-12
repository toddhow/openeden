-- tools/neutral_session_check (task 0140): activates each neutral actor of the Bannered Mare in turn (object:activateBy),
-- and takes pictures of the conversation box. The buttons are pressed by OPENEDEN_NEUTRAL_DIALOGUE_PRESS.
-- Positions are those of n5/cam.lua; the actor is the object of the cell nearest to the position.
local core = require('openmw.core')
local world = require('openmw.world')

local ACTORS = {
    { 336, -416, 64 },
    { 44.0677, 27.7724, 68 },
    { -136.195, -699.55, 69.1957 },
    { 66.3002, -579.985, 67.5885 },
    { -122.041, -55.383, 64 },
    { -241.652, -1071.21, 68 },
}
local INTERVAL = 14
local start, step, shotAt = nil, 0, {}

local function nearest(cell, pos)
    local best, bestD = nil, 1600
    for _, obj in ipairs(cell:getAll()) do
        local ok, id = pcall(function() return obj.recordId end)
        if ok and type(id) == 'string' and id:sub(1, 10) == 'skyrimse:b' then
            local p = obj.position
            local d = (p.x - pos[1]) ^ 2 + (p.y - pos[2]) ^ 2 + (p.z - pos[3]) ^ 2
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
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            for i = #shotAt, 1, -1 do
                if t >= shotAt[i] then
                    table.remove(shotAt, i)
                    player:sendEvent('N5Shot')
                    print('N5T shot at ' .. string.format('%.1f', t))
                end
            end
            if step < #ACTORS and t >= 8 + step * INTERVAL then
                step = step + 1
                local obj = nearest(player.cell, ACTORS[step])
                if obj == nil then
                    print('N5T actor ' .. step .. ' not found')
                else
                    print('N5T activating actor ' .. step .. ' ' .. tostring(obj.recordId))
                    obj:activateBy(player)
                    shotAt[#shotAt + 1] = t + 2
                    shotAt[#shotAt + 1] = t + 6
                    shotAt[#shotAt + 1] = t + 10
                end
            end
        end,
    },
}
