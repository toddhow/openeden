-- tools/lockbooks_check (task 0203, copy of tools/inventory_check with LB lines)
-- tools/inventory_check (task 0182): stands the player a short way in front of a container of the Bannered Mare, FACING
-- it, at the update number the config names (a frame count, not a clock, so the runs are equal), and logs where it
-- stands. The activation, the clicks and the screenshots come from OPENEDEN_RMLUI_INPUT_SCRIPT through the real input
-- path. Config: scripts/inv/cfg.lua written by check.sh: { base, near = {x,y,z}, from = {x,y}, dist, frame, owner, item }
-- (owner and item: the stacks the save and load phases print, worn: the item whose equipped flag they print; the wardrobe, its boots and
-- the same boots by default).
local world = require('openmw.world')
local util = require('openmw.util')
local storage = require('openmw.storage')
local cfg = require('scripts.inv.cfg')
local phase = require('scripts.inv.phase')
local okInv, inv = pcall(require, 'core.skyrimse.inventory')
local okLb, lb = pcall(require, 'core.skyrimse.lockbooks')
local okProg, prog = pcall(require, 'core.skyrimse.progression')

local function lbState(label)
    if not okLb then print('LB ' .. label .. ' no lockbooks namespace'); return end
    local spells = table.concat(lb.spells(), ',')
    local books = {}
    for id in string.gmatch(cfg.books, '[^,]+') do books[#books + 1] = id .. '=' .. tostring(lb.book_read(id)) .. '/' .. inv.count('player', id) end
    local skills = ''
    if okProg and prog.ready() then skills = string.format(' lockpicking=%d alchemy=%d enchanting=%d', prog.skill_level(8), prog.skill_level(10), prog.skill_level(17)) end
    print(string.format('LB %s picks=%d state=%s picked(%s)=%s spells=[%s] books={%s}%s', label, lb.picks(), lb.lock_state(), cfg.lockPlacement, tostring(lb.lock_picked(cfg.lockPlacement)), spells, table.concat(books, ' '), skills))
end

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
local target = nil
local acts = {}
for n in string.gmatch(cfg.acts, '[^,]+') do acts[tonumber(n)] = true end
return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            updates = updates + 1
            if updates % 300 == 0 then print('N6INV tick ' .. updates); lbState('tick ' .. updates) end -- task 0184: lines to follow the update number by
            if phase == 'save' and updates == cfg.saveAt then
                lbState('save')
                print(string.format('N6INV state boots player=%d container=%d revision=%d worn=%s', inv.count('player', cfg.item), inv.count(cfg.owner, cfg.item), inv.revision(), tostring(inv.equipped(cfg.worn))))
                storage.globalSection('N6INV'):set('saveNow', true)
            end
            if phase == 'load' then
                if updates == 300 or updates == 600 then
                    lbState('loaded ' .. updates)
                end
                if updates == 300 then
                    print(string.format('N6INV loaded state boots player=%d container=%d gold=%d known=%s revision=%d worn=%s', inv.count('player', cfg.item), inv.count(cfg.owner, cfg.item), inv.count('player', '0000000f'), tostring(inv.known(cfg.owner)), inv.revision(), tostring(inv.equipped(cfg.worn))))
                end
                return
            end
            -- placement 1 at cfg.frame, an optional placement 2 (task 0184: a second actor to strike with a looted weapon) at
            -- cfg.frame2 beside cfg.base2 (the east side of it when cfg.from2 is not given)
            local function place(base, near, from, dist, label)
                local obj, d = nearest(player.cell, base, near)
                if obj == nil then print('N6INV target not found'); return end
                target = obj
                local p = obj.position
                local fx, fy = (from and from[1]) or (p.x + 100), (from and from[2]) or p.y
                local cx, cy = fx - p.x, fy - p.y
                local len = math.sqrt(cx * cx + cy * cy)
                local px, py = p.x + cx / len * dist, p.y + cy / len * dist
                local yaw = math.atan2(p.x - px, p.y - py)
                print(string.format('N6INV target%s %s at %.0f,%.0f,%.0f (%.0f from the spot), placed at update %d', label, tostring(obj.recordId), p.x, p.y, p.z, d, updates))
                player:teleport(player.cell, util.vector3(px, py, p.z), { onGround = true, rotation = util.transform.rotateZ(yaw) })
            end
            if acts[updates] and target ~= nil then
                print('LB activate by script at update ' .. updates)
                target:activateBy(player)
            end
            if cfg.base2 ~= '' and updates == cfg.frame2 then
                place(cfg.base2, cfg.near2, cfg.from2, cfg.dist2, '2')
            end
            if placed or updates < cfg.frame then return end
            placed = true
            place(cfg.base, cfg.near, cfg.from, cfg.dist, '')
        end,
    },
}
