-- Drives the 'load' runs of tools/actors_check/save_check.sh: load the save 'a', wait, optionally save it again as 'b', quit.
local core = require('openmw.core')
local menu = require('openmw.menu')

local frames = 0
local stage = 0
local runningSince = nil
local savedAt = nil

local function firstSave()
    for dir, slots in pairs(menu.getAllSaves()) do
        for slot in pairs(slots) do
            if slot == 'a.omwsave' then return dir, slot end
        end
    end
end

return {
    engineHandlers = {
        onFrame = function()
            frames = frames + 1
            if stage == 0 and frames > 60 then
                local dir, slot = firstSave()
                print('ACSAVE loading ' .. tostring(dir) .. ' / ' .. tostring(slot))
                menu.loadGame(dir, slot)
                stage = 1
            elseif stage == 1 and menu.getState() == menu.STATE.Running then
                runningSince = core.getRealTime()
                stage = 2
            elseif stage == 2 and core.getRealTime() - runningSince > 12 then
                menu.saveGame('b')
                savedAt = frames
                stage = 3
            elseif stage == 3 and frames > savedAt + 120 then
                core.quit()
            end
        end,
    },
}
