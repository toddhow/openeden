-- The riding camera (task 0255). Started only when the session's game pack declares `core.skyrimse.riding` (the pack
-- setting [extension.skyrimse] riding; the REQUIRES tag in builtin.omwscripts). While the player sits on a horse the
-- third person camera is pulled back by the pack's factor (a horse is longer than a person, the saddle is 135 units up);
-- when he gets off the distance he had comes back. Nothing here runs on foot.
local I = require('openmw.interfaces')
local riding = require('core.skyrimse.riding')

local saved = nil -- the base distance before the pull back

return {
    engineHandlers = {
        onUpdate = function()
            if I.Camera == nil or I.Camera.getBaseThirdPersonDistance == nil then return end
            if riding.active() then
                if saved == nil then
                    saved = I.Camera.getBaseThirdPersonDistance()
                    I.Camera.setBaseThirdPersonDistance(saved * riding.camera_scale())
                    print(string.format('RIDING camera distance %.0f -> %.0f', saved, saved * riding.camera_scale()))
                end
            elseif saved ~= nil then
                I.Camera.setBaseThirdPersonDistance(saved)
                print(string.format('RIDING camera distance back to %.0f', saved))
                saved = nil
            end
        end,
    },
}
