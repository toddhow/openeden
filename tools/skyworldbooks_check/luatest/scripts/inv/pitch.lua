-- Task 0206: tilts the player's view down from update cfg.frame on (cfg.pitch radians, positive looks down; 0 does nothing), so
-- that the activate ray reaches a bench lower than the eye. Config: scripts/inv/pitchcfg.lua written by check.sh.
local camera = require('openmw.camera')
local util = require('openmw.util')
local ok, cfg = pcall(require, 'scripts.inv.pitchcfg')
if not ok or (cfg.pitch == 0 and cfg.aim == nil) then return {} end
local frames = 0
return {
    engineHandlers = {
        onFrame = function()
            frames = frames + 1
            if frames >= cfg.frame then
                if cfg.eyeUp ~= nil and cfg.eyeUp ~= 0 then camera.setFirstPersonOffset(util.vector3(0, 0, cfg.eyeUp)) end -- task 0234: raises the eye
                if cfg.aim ~= nil then
                    -- task 0234: looks straight at a point (x, y, z): the pitch from the eye to it
                    local eye = camera.getPosition()
                    local dx, dy = cfg.aim[1] - eye.x, cfg.aim[2] - eye.y
                    camera.setPitch(math.atan2(eye.z - cfg.aim[3], math.sqrt(dx * dx + dy * dy)))
                else
                    camera.setPitch(cfg.pitch)
                end
            end
        end,
    },
}
