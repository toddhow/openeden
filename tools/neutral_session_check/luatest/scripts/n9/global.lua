-- tools/neutral_session_check (task 0208): "n9pick" stands the player a short way from a target object or actor (cfg.near,
-- the nearest skyrimse object whose record id is cfg.base, or any when nil), facing it, and logs the player's count of
-- cfg.item and whether the object is still enabled every second. The activation itself comes from the real activate key
-- (OPENEDEN_RMLUI_INPUT_SCRIPT). Pictures (N5Shot) at the real times in cfg.shots. cfg is written by check.sh from N9_*.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')
local okInv, inv = pcall(require, 'core.skyrimse.inventory')
local okCrime, crime = pcall(require, 'core.skyrimse.crime')
local okPick, pick = pcall(require, 'core.skyrimse.pickup')
local cfg = require('scripts.n9.cfg')
local storage = require('openmw.storage')
local saved = false
local advanced = false
local pickedAt

local start, placed, target, nextShot, nextPrint = nil, false, nil, 1, 0

local function nearest(cell)
    local best, bestD = nil, math.huge
    for _, obj in ipairs(cell:getAll()) do
        local ok, id = pcall(function() return obj.recordId end)
        if ok and type(id) == 'string' and (cfg.base == '' and id:sub(1, 10) == 'skyrimse:b' or id == cfg.base) then
            local p = obj.position
            local d = (p.x - cfg.near[1]) ^ 2 + (p.y - cfg.near[2]) ^ 2 + (p.z - cfg.near[3]) ^ 2
            if d < bestD then best, bestD = obj, d end
        end
    end
    return best
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if cfg.phase == 'load' and not placed and t >= 6 then
                placed = true
                target = nearest(player.cell)
                print('N9 loaded: target ' .. tostring(target and target.recordId) .. ' enabled ' .. tostring(target and target.enabled))
            end
            -- Task 0221: cfg.advanceHours game hours are passed once at real time cfg.advanceAt (plants grow back).
            if cfg.advanceHours > 0 and not advanced and t >= cfg.advanceAt then
                advanced = true
                world.advanceTime(cfg.advanceHours)
                print(string.format('N9 advanced %d hours', cfg.advanceHours))
            end
            local have = okInv and cfg.item ~= '' and inv.count('player', cfg.item) or 0
            if have > 0 then pickedAt = pickedAt or t end
            if cfg.phase == 'save' and not saved and pickedAt and t >= pickedAt + 3 then
                saved = true
                print(string.format('N9 saving: count %d', okInv and inv.count('player', cfg.item) or -1))
                storage.globalSection('N8CRIME'):set('saveNow', true)
            end
            if not placed and t >= 6 then
                placed = true
                target = nearest(player.cell)
                if target == nil then
                    print('N9 target not found; the cell has ' .. tostring(#player.cell:getAll()) .. ' objects')
                    for i, obj in ipairs(player.cell:getAll()) do
                        if i > 6 then break end
                        print('N9 sees ' .. tostring(obj.recordId) .. ' ' .. tostring(obj.type))
                    end
                    return
                end
                local p = target.position
                local cx, cy = cfg.from[1], cfg.from[2]
                local len = math.sqrt(cx * cx + cy * cy)
                local px, py = p.x + cx / len * cfg.dist, p.y + cy / len * cfg.dist
                local yaw = math.atan2(p.x - px, p.y - py) + (cfg.yaw or 0)
                print(string.format('N9 target %s at %.1f,%.1f,%.1f; player to %.1f,%.1f', tostring(target.recordId), p.x, p.y, p.z, px, py))
                player:teleport(player.cell, util.vector3(px, py, p.z - cfg.drop), { onGround = true, rotation = util.transform.rotateZ(yaw) })
            end
            if target and t >= nextPrint then
                nextPrint = t + 1
                local count = okInv and cfg.item ~= '' and inv.count('player', cfg.item) or -1
                local state = okPick and pick.state() or '-'
                local bounty = okCrime and crime.bounty() or -1
                local enabled = 'gone'
                if target:isValid() then enabled = tostring(target.enabled) end
                print(string.format('N9 t %.1f count %d enabled %s pickup %s bounty %d', t, count, enabled, state, bounty))
            end
            if cfg.shots[nextShot] and t >= cfg.shots[nextShot] then
                nextShot = nextShot + 1
                player:sendEvent('N5Shot')
            end
        end,
    },
}
