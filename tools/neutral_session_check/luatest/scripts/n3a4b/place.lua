-- tools/neutral_session_check (task 0110): a global script that puts the player at a position in its own cell when the
-- player script asks (a local script cannot teleport).
local world = require('openmw.world')
local util = require('openmw.util')

return {
    eventHandlers = {
        N4BPlace = function(data)
            local player = world.players[1]
            player:teleport(player.cell, util.vector3(data.x, data.y, data.z), { rotation = util.transform.rotateZ(data.yaw) })
        end,
    },
}
