-- tools/neutral_session_check (task 0135 stage S6f): the global half of "n5wake". After WAIT seconds (the sleepers are in
-- bed by then) it stands the player 150 units in front of a sleeper (TARGET, a number in its record id), takes a picture,
-- activates it (object:activateBy, the same call "n5talk" uses), takes pictures while the conversation box is open and
-- after it was closed (OPENEDEN_NEUTRAL_DIALOGUE_PRESS presses Goodbye), and prints the actor's position each time.
-- To try another actor change TARGET (b108144 Mikael, b80802 Saadia; a chair sitter: n5furn lists the ids).
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local TARGET = 'b108144'
local WAIT = 50
local DIST = 150
local SCHEDULE = { { 0, 'place' }, { 2, 'shot' }, { 3, 'activate' }, { 6, 'shot' }, { 10, 'shot' }, { 16, 'shot' },
    { 24, 'shot' }, { 34, 'shot' }, { 46, 'shot' }, { 60, 'shot' } }
local start, index, base = nil, 1, nil

local function find(cell, tag)
    for _, o in ipairs(cell:getAll()) do
        if string.find(o.recordId, tag, 1, true) then return o end
    end
    return nil
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if t < WAIT or index > #SCHEDULE then return end
            base = base or t
            local item = SCHEDULE[index]
            if t - base < item[1] then return end
            index = index + 1
            local a = find(player.cell, TARGET)
            if a == nil then print('N5W actor not found'); return end
            local p = a.position
            if item[2] == 'place' then
                local yaw = a.rotation:getYaw()
                local px, py = p.x + math.sin(yaw) * DIST, p.y + math.cos(yaw) * DIST
                local look = math.atan2(p.x - px, p.y - py)
                player:teleport(player.cell, util.vector3(px, py, p.z + 2), { rotation = util.transform.rotateZ(look) })
            elseif item[2] == 'activate' then
                a:activateBy(player)
            else
                player:sendEvent('N5Shot')
            end
            print(string.format('N5W %s at %.1f %.1f %.1f t %.1f', item[2], p.x, p.y, p.z, t))
        end,
    },
}
