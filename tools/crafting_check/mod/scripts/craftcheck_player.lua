-- Task 0085 move 2 stage 2c check: opens the alchemy, repair, recharge and enchanting windows one after another through
-- the Lua UI interface (each runs the window's open path through the crafting contract) and takes a screenshot of each.
local core = require('openmw.core')
local types = require('openmw.types')
local self = require('openmw.self')
local I = require('openmw.interfaces')
local debug = require('openmw.debug')

local SETTLE = 8
local startReal = nil
local step = 0
local stepStart = 0

local function find(recordId)
    for _, item in ipairs(types.Actor.inventory(self):getAll()) do
        if item.recordId == recordId then return item end
    end
    return nil
end

local steps = {
    { name = 'alchemy', open = function() I.UI.setMode('Alchemy') end },
    { name = 'repair', open = function() I.UI.setMode('Repair', { target = find('repair_prongs') }) end },
    { name = 'recharge', open = function() I.UI.setMode('Recharge', { target = find('misc_soulgem_grand') }) end },
    { name = 'enchanting', open = function() I.UI.setMode('Enchanting', { target = find('misc_soulgem_grand') }) end },
}

return {
    engineHandlers = {
        onFrame = function()
            startReal = startReal or core.getRealTime()
            local t = core.getRealTime() - startReal - SETTLE
            if t < 0 then return end
            local dt = t - stepStart
            if step == 0 then
                debug.toggleGodMode()
                print('CRAFT items: prongs=' .. tostring(find('repair_prongs') ~= nil) .. ' gem=' .. tostring(find('misc_soulgem_grand') ~= nil))
                step = 1
                stepStart = t
                steps[1].open()
                print('CRAFT open ' .. steps[1].name)
            elseif step <= #steps then
                if dt >= 2.0 and not steps[step].shot then
                    steps[step].shot = true
                    debug.takeScreenshot()
                    print('CRAFT shot ' .. steps[step].name)
                elseif dt >= 3.0 then
                    I.UI.setMode()
                    step = step + 1
                    stepStart = t
                    if step <= #steps then
                        steps[step].open()
                        print('CRAFT open ' .. steps[step].name)
                    end
                end
            elseif dt >= 1.5 then
                print('CRAFT done')
                core.quit()
            end
        end,
    },
}
