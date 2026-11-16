-- tools/neutral_session_check (task 0246): switches the collision debug render mode on once, so the frames of the
-- n5wolf scenario show the physics capsules of the creatures (content=n5colldebug.omwscripts in CFG_EXTRA).
local debug = require('openmw.debug')
local toggled = false

return {
    engineHandlers = {
        onFrame = function()
            if not toggled then
                toggled = true
                debug.toggleRenderMode(debug.RENDER_MODE.CollisionDebug)
            end
        end,
    },
}
