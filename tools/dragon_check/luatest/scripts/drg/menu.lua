-- tools/skydeaths_check (task 0254): the menu half: saves once the global script reported (SAVE phase), then quits 3 real
-- seconds later so that the save is complete.
local menu = require('openmw.menu')
local storage = require('openmw.storage')
local core = require('openmw.core')
local cfg = require('scripts.drg.cfg')

local asked, quitAt = false, nil

return {
    engineHandlers = {
        onFrame = function()
            if menu.getState() ~= menu.STATE.Running then return end
            if cfg.phase == 'save' and not asked and storage.globalSection('N280'):get('reported') then
                asked = true
                print('DRG save request')
                menu.saveGame('dragon', 'dragon')
                quitAt = core.getRealTime() + 3
            end
            if quitAt ~= nil and core.getRealTime() > quitAt then
                quitAt = nil
                print('DRG quit')
                menu.quit()
            end
        end,
    },
}
