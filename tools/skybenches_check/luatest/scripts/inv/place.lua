-- tools/skyrimcraft_check (task 0199): opens a crafting bench of a kind at an update number (the pack setting
-- `crafting test bench` must be on), prints the player's inventory at a save and after a load. Config:
-- scripts/inv/cfg.lua written by check.sh (kind, openFrame, name, saveAt).
local core = require('openmw.core')
local world = require('openmw.world')
local storage = require('openmw.storage')
local util = require('openmw.util')
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

-- Task 0206: with cfg.base set, stands the player cfg.dist from the nearest object of that record, facing it, at update
-- cfg.frame (a bench opened by the REAL activate key instead of the test setting). cfg.height raises the aim to the
-- middle of the bench (the player's eye is about 120 above the feet, a bench top is lower).
local function nearest(cell, base, near)
    local best, bestD = nil, math.huge
    for _, obj in ipairs(cell:getAll()) do
        local ok, id = pcall(function() return obj.recordId end)
        if ok and id == base then
            local p = obj.position
            local d = (p.x - near[1]) ^ 2 + (p.y - near[2]) ^ 2 + (p.z - near[3]) ^ 2
            if d < bestD then best, bestD = obj, d end
        end
    end
    return best, math.sqrt(bestD)
end

local updates, placed = 0, false
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
            if cfg.base ~= '' and not placed and updates >= cfg.frame then
                placed = true
                local obj, d = nearest(player.cell, cfg.base, cfg.near)
                local p
                if obj ~= nil then p = obj.position
                elseif cfg.near[1] ~= 0 then
                    print('N6INV target not in the cell: using the given position')
                    p = util.vector3(cfg.near[1], cfg.near[2], cfg.near[3])
                    obj = { recordId = cfg.base }
                end
                d = d or 0
                if p == nil then
                    print('N6INV target not found')
                else
                    local fx, fy = cfg.from[1], cfg.from[2]
                    if fx == 0 and fy == 0 then fx, fy = p.x + 100, p.y end
                    local cx, cy = fx - p.x, fy - p.y
                    local len = math.sqrt(cx * cx + cy * cy)
                    local px, py = p.x + cx / len * cfg.dist, p.y + cy / len * cfg.dist
                    local yaw = math.atan2(p.x - px, p.y - py) + (cfg.yawOff or 0)
                    print(string.format('N6INV target %s at %.0f,%.0f,%.0f (%.0f from the spot), placed at update %d', tostring(obj.recordId), p.x, p.y, p.z, d, updates))
                    player:teleport(player.cell, util.vector3(px, py, p.z), { onGround = true, rotation = util.transform.rotateZ(yaw) })
                end
            end
            if updates == cfg.openFrame and cfg.openFrame > 0 then
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
            -- task 0206: how many recipes the open bench offers (a perk the player bought opens more)
            if okCraft and cfg.openFrame > 0 and (updates == cfg.openFrame + 60 or (cfg.openFrame2 > 0 and updates == cfg.openFrame2 + 60)) then
                print('N6INV recipes ' .. tostring(crafting.bench()) .. ' ' .. #crafting.recipes())
            end
            if cfg.openFrame > 0 and updates == cfg.openFrame + 200 then dump('before') end
            if cfg.openFrame > 0 and updates == cfg.openFrame + 700 then dump('after') end
            -- drink the first potion the player holds (the pack's use path; needs the combat rules for the heal)
            if cfg.openFrame > 0 and updates == cfg.openFrame + 750 and okInv then
                for _, id in ipairs(inv.items('player')) do
                    if inv.category(id) == 'potion' then
                        print('N6INV use ' .. id .. ' ' .. inv.name(id))
                        inv.use(id)
                        break
                    end
                end
            end
            if cfg.openFrame == 0 and (updates == 690 or updates == 1000 or updates == 1400 or updates == 1800) then dump('t' .. updates) end
            if updates == 650 and cfg.probe then
                local want = { ['skyrimse:b468897'] = 'tanning', ['skyrimse:b640718'] = 'smelter', ['skyrimse:b784865'] = 'forge' }
                for _, obj in ipairs(player.cell:getAll()) do
                    local ok, id = pcall(function() return obj.recordId end)
                    if ok and want[id] then
                        local d = (obj.position - player.position):length()
                        if d < 3000 then print(string.format('N6INV probe %s %s at %.0f,%.0f,%.0f dist %.0f', want[id], id, obj.position.x, obj.position.y, obj.position.z, d)) end
                    end
                end
            end
            if cfg.openFrame > 0 and updates == cfg.openFrame + 800 then dump('drunk') end
            -- task 0229: wield a held item, use another (a poison, a food) by the same binding the window calls
            if cfg.equipItemFrame > 0 and updates == cfg.equipItemFrame and okInv then
                print('N6INV equip ' .. cfg.equipItem)
                inv.equip(cfg.equipItem)
            end
            if cfg.useFrame > 0 and updates == cfg.useFrame and okInv then
                print('N6INV use ' .. cfg.useItem .. ' ' .. inv.name(cfg.useItem))
                inv.use(cfg.useItem)
            end
        end,
    },
}
