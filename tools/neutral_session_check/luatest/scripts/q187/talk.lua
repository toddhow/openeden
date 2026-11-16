-- tools/neutral_session_check (task 0187): activates the actor named in target.lua once, ACT_FRAME updates after the
-- start (late enough for the dialogue data, about 15 real seconds). The clicks come from OPENEDEN_RMLUI_INPUT_SCRIPT.
local core = require('openmw.core')
local world = require('openmw.world')

local ACT_FRAME = 9000 -- an update count, not a time: the input script's clicks are frame numbers
local TARGET = require('scripts.q187.target') -- { record id of the base, e.g. 'skyrimse:b108144' }
local frames, done = 0, false
local shotAt = {}

local function find(cell)
    for _, obj in ipairs(cell:getAll()) do
        local ok, id = pcall(function() return obj.recordId end)
        if ok and id == TARGET[1] then return obj end
    end
    return nil
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            frames = frames + 1
            local t = frames
            for i = #shotAt, 1, -1 do
                if t >= shotAt[i] then
                    table.remove(shotAt, i)
                    player:sendEvent('N5Shot')
                end
            end
            if not done and t >= ACT_FRAME then
                done = true
                local obj = find(player.cell)
                print('Q187 activating ' .. tostring(obj and obj.recordId))
                if obj then
                    obj:activateBy(player)
                    
                end
            end
        end,
    },
}
