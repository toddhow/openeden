-- tools/neutral_session_check (task 0152): logs the model and position of every object of the player's cell
-- ("MAREOBJ <model> x,y,z") once, 3 real seconds after the start.
local core = require('openmw.core')
local world = require('openmw.world')
local types = require('openmw.types')

local start, done = nil, false
return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil or done then return end
            start = start or core.getRealTime()
            if core.getRealTime() - start < 3 then return end
            done = true
            for _, o in ipairs(player.cell:getAll()) do
                local ok, model = pcall(function() return o.type.record(o).model end)
                if not ok or model == nil then
                    ok, model = pcall(function() return types.Static.record(o).model end)
                end
                local p = o.position
                print(string.format('MAREOBJ %s %s %.1f,%.1f,%.1f', tostring(o.recordId) .. ' T=' .. tostring(o.type) .. ' R=' .. string.format('%.2f', o.rotation:getAnglesZYX()) .. ' E=' .. tostring(o.enabled), ok and tostring(model) or tostring(model), p.x, p.y, p.z))
            end
        end,
    },
}
