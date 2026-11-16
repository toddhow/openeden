-- tools/skytrespass_check (task 0213): the global half. Waits for the pack's crime namespace, then every simulation second
-- logs the game clock, the player's cell, the bounty and the crime state (TRESPCHECK lines), advances the clock once
-- (preAdvance hours), puts the player at a bed spot (bed), teleports him into another cell (goCell, goPos at goAt) and
-- back (backAt), and asks the menu script to save (saveAt).
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')
local storage = require('openmw.storage')
local cfg = require('scripts.tresp10.cfg')
local okCrime, crime = pcall(require, 'core.skyrimse.crime')
if not okCrime then
    print('TRESPCHECK no crime namespace')
    return {}
end

local nextLog, t0, advanced, placed, went, back, furnDone = 0, nil, false, false, false, false, false
local firstCell = nil
local fired, shotAt = {}, {}
return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            t0 = t0 or core.getSimulationTime()
            local t = core.getSimulationTime() - t0
            if t >= nextLog then
                nextLog = t + 1
                local gt = core.getGameTime()
                print(string.format('TRESPCHECK t=%.1f game=%.0f hour=%.4f cell=%s bounty=%d state=%s', t, gt, (gt / 3600) % 24,
                    tostring(player.cell.name), crime.bounty(), crime.state()))
            end
            if cfg.preAdvance and cfg.preAdvance > 0 and not advanced and t >= 2 then
                advanced = true
                world.advanceTime(cfg.preAdvance)
                print(string.format('TRESPCHECK advanced %.2f hours', cfg.preAdvance))
            end
            if cfg.bed and not placed and t >= cfg.bed[6] then
                placed = true
                firstCell = player.cell
                player:teleport(player.cell, util.vector3(cfg.bed[1], cfg.bed[2], cfg.bed[3]), { rotation = util.transform.rotateZ(cfg.bed[4]) })
                player:sendEvent('RestLook', cfg.bed[5])
                print('TRESPCHECK player placed at the bed spot')
            end
            for i, e in ipairs(cfg.events or {}) do
                if not fired[i] and t >= e[1] then
                    fired[i] = true
                    print('TRESPCHECK sending the furniture event for ' .. e[4])
                    core.sendGlobalEvent('openeden_skyrimse_bench_opened', { placement = e[2], base = e[3], kind = 'bench', name = e[4] })
                    shotAt[#shotAt + 1] = t + 2
                end
            end
            for i = #shotAt, 1, -1 do
                if t >= shotAt[i] then
                    table.remove(shotAt, i)
                    player:sendEvent('RestShot')
                end
            end
            if cfg.furn and not furnDone and t >= cfg.furn[2] then
                furnDone = true
                local best, bestD
                for _, o in ipairs(player.cell:getAll()) do
                    if tostring(o.recordId) == cfg.furn[1] then
                        local d = (o.position.x - cfg.furn[5]) ^ 2 + (o.position.y - cfg.furn[6]) ^ 2
                        if not bestD or d < bestD then best, bestD = o, d end
                    end
                end
                if best then
                    print(string.format('TRESPCHECK furniture %s at %.0f,%.0f,%.0f', tostring(best.recordId), best.position.x, best.position.y, best.position.z))
                    player:teleport(player.cell, util.vector3(best.position.x, best.position.y - cfg.furn[4], best.position.z + 5), { rotation = util.transform.rotateZ(0) })
                    player:sendEvent('RestLook', cfg.furn[3])
                else
                    print('TRESPCHECK no furniture matches ' .. cfg.furn[1])
                end
            end
            if cfg.goCell ~= '' and not went and t >= cfg.goAt then
                went = true
                firstCell = player.cell
                local cell = world.getCellByName(cfg.goCell)
                if cell then
                    print('TRESPCHECK going to ' .. cfg.goCell)
                    player:teleport(cell, util.vector3(cfg.goPos[1], cfg.goPos[2], cfg.goPos[3]), {})
                else
                    print('TRESPCHECK no such cell ' .. cfg.goCell)
                end
            end
            if cfg.backAt and cfg.backAt > 0 and went and not back and t >= cfg.backAt and firstCell then
                back = true
                print('TRESPCHECK going back')
                player:teleport(firstCell, util.vector3(157, 470, 70), {})
            end
            if cfg.saveAt and cfg.saveAt > 0 and t >= cfg.saveAt then
                storage.globalSection('TRESP10'):set('saveNow', true)
            end
        end,
    },
    eventHandlers = {
        RestShotAll = function() local p = world.players[1]; if p then p:sendEvent('RestShot') end end,
    },
}
