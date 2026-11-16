-- tools/neutral_session_check (task 0208): the player half of "n9pick": tilts the view to cfg.pitch (radians, positive =
-- down) and, when cfg.sneak, holds the sneak control, so the engine's own stance is what the pickpocket rule reads.
local self = require('openmw.self')
local camera = require('openmw.camera')
local cfg = require('scripts.n9.cfg')

return {
    engineHandlers = {
        onFrame = function()
            if cfg.sneak then self.controls.sneak = true end
            if cfg.pitch ~= 0 then
                local delta = cfg.pitch - camera.getPitch()
                self.controls.pitchChange = math.max(-0.05, math.min(0.05, delta))
            end
        end,
    },
}
