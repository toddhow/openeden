-- tools/skyrimcraft_check (task 0199): opens a crafting bench of a kind at an update number (the pack setting
-- `crafting test bench` must be on), prints the player's inventory at a save and after a load. Config:
-- scripts/inv/cfg.lua written by check.sh (kind, openFrame, name, saveAt).
local core = require('openmw.core')
local world = require('openmw.world')
local storage = require('openmw.storage')
local cfg = require('scripts.inv.cfg')
local phase = require('scripts.inv.phase')
local okInv, inv = pcall(require, 'core.skyrimse.inventory')
local okCraft, crafting = pcall(require, 'core.skyrimse.crafting')

local function dump(label)
    local parts = {}
    if okInv then
        for _, id in ipairs(inv.items('player')) do
            parts[#parts + 1] = string.format('%s=%s x%d', id, inv.name(id), inv.count('player', id))
        end
    end
    print('N6INV ' .. label .. ' revision=' .. tostring(okInv and inv.revision()) .. ' items: ' .. table.concat(parts, '; '))
    if okCraft then
        print('N6INV ' .. label .. ' crafting revision=' .. crafting.revision() .. ' results=' .. crafting.results() .. ' last=' .. crafting.last_result())
    end
end

local updates = 0
return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            updates = updates + 1
            if updates % 300 == 0 then print('N6INV tick ' .. updates) end
            if phase == 'save' and updates == cfg.saveAt then
                dump('save')
                storage.globalSection('N6INV'):set('saveNow', true)
            end
            if phase == 'load' then
                if updates == 650 then dump('loaded') end
                return
            end
            if updates == cfg.openFrame then
                print('N6INV open ' .. cfg.kind)
                core.sendGlobalEvent('SkyrimCraftingOpenKind', { kind = cfg.kind, name = cfg.name })
            end
            if cfg.kind2 ~= '' and updates == cfg.openFrame2 then
                print('N6INV open ' .. cfg.kind2)
                core.sendGlobalEvent('SkyrimCraftingOpenKind', { kind = cfg.kind2, name = cfg.name .. '-2' })
            end
            -- wield the first made weapon (an item the pack made: its id starts with ff)
            if cfg.equipFrame > 0 and updates == cfg.equipFrame and okInv then
                for _, id in ipairs(inv.items('player')) do
                    if id:sub(1, 2) == 'ff' and inv.category(id) == 'weapon' then
                        print('N6INV equip ' .. id .. ' ' .. inv.name(id))
                        inv.equip(id)
                        break
                    end
                end
            end
            if updates == cfg.openFrame + 200 then dump('before') end
            if updates == cfg.openFrame + 700 then dump('after') end
            -- drink the first potion the player holds (the pack's use path; needs the combat rules for the heal)
            if updates == cfg.openFrame + 750 and okInv then
                for _, id in ipairs(inv.items('player')) do
                    if inv.category(id) == 'potion' then
                        print('N6INV use ' .. id .. ' ' .. inv.name(id))
                        inv.use(id)
                        break
                    end
                end
            end
            if updates == cfg.openFrame + 800 then dump('drunk') end
        end,
    },
}
