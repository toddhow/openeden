-- tools/neutral_session_check (task 0115): the Morrowind control. In a Morrowind session (neutral world setting off
-- or on) it activates the first teleport door of the player's cell by the same object:activateBy path and prints the
-- cell and position the player lands at, so two executables can be compared line by line.
local core = require('openmw.core')
local types = require('openmw.types')
local world = require('openmw.world')

local start = nil
local stage = 0

local function report(tag)
    local player = world.players[1]
    local p = player.position
    print(string.format('N4MW %s cell=%s pos=%.1f,%.1f,%.1f', tag, tostring(player.cell.name), p.x, p.y, p.z))
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if stage == 0 and t > 5 then
                stage = 1
                report('before')
                local doors = {}
                for _, door in ipairs(player.cell:getAll(types.Door)) do
                    if types.Door.isTeleport(door) then table.insert(doors, door) end
                end
                table.sort(doors, function(a, b) return a.id < b.id end)
                print('N4MW teleport doors in the cell: ' .. #doors)
                if doors[1] ~= nil then doors[1]:activateBy(player) end
            elseif stage == 1 and t > 14 then
                stage = 2
                report('after')
            end
        end,
    },
}
