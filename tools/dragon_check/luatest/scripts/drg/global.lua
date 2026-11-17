-- tools/skydeaths_check (task 0254): the global half. Every 5 real seconds logs "DRG t <s>"; at cfg.report real seconds
-- it logs every actor of the player's cell whose base tag is in cfg.tags ("DRG actor <tag> <id> enabled <b> x y z"), asks the
-- player script for a picture of the first one (event DRGShot, a free camera) and tells the menu script it may save
-- (storage N280.reported). With cfg.place the player is put cfg.dist units east of it at cfg.placeAt (SAVE phase). In the LOAD phase at cfg.activate real seconds the first actor is activated by the player
-- (activateBy: a dead actor opens its loot window) and a picture of the window follows 1.5 s later.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')
local storage = require('openmw.storage')
local cfg = require('scripts.drg.cfg')

local start, reported, activated, placed = nil, false, false, false
local lastSecond = -1

local function actorsOf(cell)
    local found = {}
    for _, tag in ipairs(cfg.tags) do
        for _, o in ipairs(cell:getAll()) do
            local ok, id = pcall(function() return o.recordId end)
            if ok and id ~= nil and string.find(id, tag, 1, true) then found[#found + 1] = { tag = tag, obj = o } end
        end
    end
    return found
end

local function report(player)
    local list = actorsOf(player.cell)
    for _, a in ipairs(list) do
        local p = a.obj.position
        print(string.format('DRG actor %s %s enabled %s %.1f %.1f %.1f', a.tag, tostring(a.obj.id), tostring(a.obj.enabled), p.x, p.y, p.z))
    end
    return list
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if math.floor(t / 5) ~= lastSecond then
                lastSecond = math.floor(t / 5)
                print(string.format('DRG t %d', lastSecond * 5))
            end
            if cfg.place and cfg.phase == 'save' and not placed and t >= cfg.placeAt then
                -- the raise scenario: the player stands cfg.dist units east of the first actor, facing it (once)
                local list = actorsOf(player.cell)
                if list[1] ~= nil then
                    placed = true
                    local p = list[1].obj.position
                    local px, py = p.x + cfg.dist, p.y
                    player:teleport(player.cell, util.vector3(px, py, p.z + 2),
                        { rotation = util.transform.rotateZ(math.atan2(p.x - px, p.y - py)) })
                    print(string.format('DRG player placed %.0f %.0f', px, py))
                end
            end
            if not reported and t >= cfg.report then
                reported = true
                local list = report(player)
                if list[1] ~= nil then
                    local p = list[1].obj.position
                    player:sendEvent('DRGShot', { x = p.x, y = p.y, z = p.z })
                end
                storage.globalSection('N280'):set('reported', true)
            end
            if cfg.phase == 'load' and not activated and t >= cfg.activate then
                activated = true
                local list = actorsOf(player.cell)
                if list[1] ~= nil then
                    print('DRG activate ' .. tostring(list[1].obj.id))
                    list[1].obj:activateBy(player)
                    player:sendEvent('DRGShotLater', {})
                end
            end
        end,
    },
}
