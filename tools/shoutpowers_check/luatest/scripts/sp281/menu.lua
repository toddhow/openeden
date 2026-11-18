-- tools/shoutmenu_check (task 0266): saves the game once the global script asks (run phase only).
local menu = require('openmw.menu')
local storage = require('openmw.storage')
local cfg = require('scripts.sp281.cfg')

local asked = false

return {
    engineHandlers = {
        onFrame = function()
            if menu.getState() ~= menu.STATE.Running then return end
            if cfg.phase == 'run' and not asked and storage.globalSection('SP281'):get('saveNow') then
                asked = true
                print('SHOUTMENU save request')
                menu.saveGame('sp281', 'sp281')
            end
        end,
    },
}
