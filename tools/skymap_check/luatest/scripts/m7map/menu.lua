-- tools/skymap_check (task 0201): in the "run" phase saves the game once the global script asks.
local menu = require('openmw.menu')
local storage = require('openmw.storage')
local cfg = require('scripts.m7map.cfg')

local asked = false

return {
    engineHandlers = {
        onFrame = function()
            if menu.getState() ~= menu.STATE.Running then return end
            if cfg.phase == 'run' and not asked and storage.globalSection('M7MAP'):get('saveNow') then
                asked = true
                print('M7MAP save request')
                menu.saveGame('m7map', 'm7map')
            end
        end,
    },
}
