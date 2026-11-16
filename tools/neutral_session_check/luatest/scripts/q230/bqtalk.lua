-- tools/neutral_session_check (task 0230): once the quests have run (simulation time ACTIVATE_AT) activates Hulda in the
-- Bannered Mare (object:activateBy, the call "n5talk" uses) and takes pictures of the conversation box. The buttons are
-- pressed by OPENEDEN_RMLUI_INPUT_SCRIPT (real clicks).
local core = require('openmw.core')
local world = require('openmw.world')

local ACTIVATE_AT = 70 -- simulation seconds (the host starts late and the story event comes script seconds after that)
local SHOTS = { 3, 8, 13 } -- real seconds after the activation (the box pauses the simulation clock)
local activatedAt = nil
local shots = {}

local HULDA = 'skyrimse:b80803' -- the base record of Hulda (HuldaREF 0x1a66e names 0x13ba3 = 80803; ids are decimal)

local function find(cell)
    for _, obj in ipairs(cell:getAll()) do
        local ok, id = pcall(function() return obj.recordId end)
        if ok and type(id) == 'string' and id:lower() == HULDA then return obj end
    end
    return nil
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            local t = core.getSimulationTime()
            if activatedAt == nil and t >= ACTIVATE_AT then
                local obj = find(player.cell)
                if obj == nil then
                    print('Q230 actor not found')
                    activatedAt = -1
                else
                    print('Q230 activating ' .. tostring(obj.recordId))
                    obj:activateBy(player)
                    activatedAt = t
                    for _, s in ipairs(SHOTS) do shots[#shots + 1] = core.getRealTime() + s end
                end
            end
            for i = #shots, 1, -1 do
                if core.getRealTime() >= shots[i] then
                    table.remove(shots, i)
                    player:sendEvent('N5Shot')
                    print('Q230 shot at ' .. string.format('%.1f', core.getRealTime()))
                end
            end
        end,
    },
}
