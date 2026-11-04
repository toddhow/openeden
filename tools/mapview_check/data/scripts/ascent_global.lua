-- Task 0066 stage e: moves the player on request of ascent_player.lua. A player script cannot teleport; this one can.
-- Only part of tools/mapview_check; never part of the game.
local world = require('openmw.world')

return {
    eventHandlers = {
        MapViewAscentMove = function(data)
            local player = world.players[1]
            if player then
                -- An empty cell name means the default exterior worldspace; the cell comes from the position.
                player:teleport('', data.position)
            end
        end,
    },
}
