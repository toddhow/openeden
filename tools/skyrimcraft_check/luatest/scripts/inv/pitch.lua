-- Task 0206: tilts the player's view down from update cfg.frame on (cfg.pitch radians, positive looks down; 0 does nothing), so
-- that the activate ray reaches a bench lower than the eye. Config: scripts/inv/pitchcfg.lua written by check.sh.
local camera = require('openmw.camera')
local ok, cfg = pcall(require, 'scripts.inv.pitchcfg')
if not ok or cfg.pitch == 0 then return {} end
local frames = 0
return {
    engineHandlers = {
        onFrame = function()
            frames = frames + 1
            if frames >= cfg.frame then camera.setPitch(cfg.pitch) end
        end,
    },
}
