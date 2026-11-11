-- Moves the player back to the start of the scenario's space between phases (tools/footslip_check, task 0108).
local world = require('openmw.world')
local util = require('openmw.util')
return {
    eventHandlers = {
        FootSlipTeleport = function(data)
            world.players[1]:teleport(data.cell, data.position, { rotation = util.transform.rotateZ(data.yaw) })
        end,
    },
}
