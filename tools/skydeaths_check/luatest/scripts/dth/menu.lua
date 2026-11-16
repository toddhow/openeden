-- tools/skydeaths_check (task 0254): the menu half: saves once the global script reported (SAVE phase), then quits 3 real
-- seconds later so that the save is complete.
local menu = require('openmw.menu')
local storage = require('openmw.storage')
local core = require('openmw.core')
local cfg = require('scripts.dth.cfg')

local asked, quitAt = false, nil

return {
    engineHandlers = {
        onFrame = function()
            if menu.getState() ~= menu.STATE.Running then return end
            if cfg.phase == 'save' and not asked and storage.globalSection('N254'):get('reported') then
                asked = true
                print('DTH save request')
                menu.saveGame('deaths', 'deaths')
                quitAt = core.getRealTime() + 3
            end
            if quitAt ~= nil and core.getRealTime() > quitAt then
                quitAt = nil
                print('DTH quit')
                menu.quit()
            end
        end,
    },
}
