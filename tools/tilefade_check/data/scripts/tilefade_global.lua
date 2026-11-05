-- Task 0087: moves the player on request of tilefade_player.lua (a player script cannot teleport). Only part of
-- tools/tilefade_check; never part of the game.
local world = require('openmw.world')

return {
    eventHandlers = {
        TileFadeMove = function(data)
            local player = world.players[1]
            if player then
                player:teleport('', data.position)
            end
        end,
    },
}
