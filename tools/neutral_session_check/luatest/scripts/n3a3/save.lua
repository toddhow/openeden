-- tools/neutral_session_check (task 0109): after the game has been running for a while, asks the engine to save. A neutral
-- session has no save support until stage N6, so the engine must refuse and say so in the log.
local menu = require('openmw.menu')

local frames = 0
local asked = false

return {
    engineHandlers = {
        onFrame = function()
            frames = frames + 1
            if not asked and frames > 200 and menu.getState() == menu.STATE.Running then
                asked = true
                print('N3A3 save attempt')
                menu.saveGame('n3a3 refusal test', 'n3a3')
            end
        end,
    },
}
