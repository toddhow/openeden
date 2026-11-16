-- tools/wordwall_check (task 0256): saves the game once the global script asks (run phase only).
local menu = require('openmw.menu')
local storage = require('openmw.storage')
local cfg = require('scripts.ww256.cfg')

local asked = false

return {
    engineHandlers = {
        onFrame = function()
            if menu.getState() ~= menu.STATE.Running then return end
            if cfg.phase == 'run' and not asked and storage.globalSection('WW256'):get('saveNow') then
                asked = true
                print('WW256 save request')
                menu.saveGame('ww256', 'ww256')
            end
        end,
    },
}
