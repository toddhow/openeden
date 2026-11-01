-- A scripted walk for tools/benchmark.sh (task 0023, F0b): the player runs forward and turns a little every few
-- seconds, so streaming of cells, terrain and actors is part of what is measured and not only a still view. It does
-- nothing unless the benchmark adds this folder to the data paths, so it is never part of the game.
local self = require('openmw.self')

local seconds = 0
local TURN_EVERY = 4 -- seconds between turns
local TURN_ANGLE = 0.35 -- radians, a gentle bend so the route does not end at a wall at once

return {
    engineHandlers = {
        -- onFrame runs for player scripts every rendered frame; onInputUpdate is not a handler in this version.
        onFrame = function(dt)
            seconds = seconds + dt
            self.controls.run = true
            self.controls.movement = 1
            if math.floor(seconds / TURN_EVERY) ~= math.floor((seconds - dt) / TURN_EVERY) then
                self.controls.yawChange = TURN_ANGLE
            end
        end,
    },
}
