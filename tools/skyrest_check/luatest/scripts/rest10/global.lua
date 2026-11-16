-- tools/skyrest_check (task 0210): the global half. Waits for the pack's rest namespace, then every two simulation seconds
-- logs the game clock and where the actors of the cell stand (RESTCHECK lines), hurts the player once on request of the
-- scenario (HURT_AT simulation seconds, a health value) and asks the menu script to save (SAVE_AT).
local core = require('openmw.core')
local world = require('openmw.world')
local types = require('openmw.types')
local storage = require('openmw.storage')
local cfg = require('scripts.rest10.cfg')
local rest = require('core.skyrimse.rest')

local util = require('openmw.util')
local nextLog, t0, hurt, advanced, placed = 0, nil, false, false, false
return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil or not rest.ready() then return end
            t0 = t0 or core.getSimulationTime()
            local t = core.getSimulationTime() - t0
            if t >= nextLog then
                nextLog = t + 2
                local gt = core.getGameTime()
                local line = { string.format('RESTCHECK t=%.1f game=%.0f hour=%.2f can_rest=%q rested=%s left=%.2f', t, gt, (gt / 3600) % 24,
                    rest.can_rest(), tostring(rest.rested()), rest.rested_hours_left()) }
                local list = {}
                for _, o in ipairs(player.cell:getAll()) do
                    if types.Actor.objectIsInstance(o) and o ~= player then list[#list + 1] = o end
                end
                table.sort(list, function(a, b) return tostring(a.id) < tostring(b.id) end)
                for i = 1, math.min(#list, 8) do
                    local o = list[i]
                    line[#line + 1] = string.format('%s@%.0f,%.0f,%.0f', tostring(o.recordId):gsub('skyrimse:', ''), o.position.x, o.position.y, o.position.z)
                end
                print(table.concat(line, ' '))
            end
            if cfg.preAdvance and cfg.preAdvance > 0 and not advanced and t >= 2 then
                advanced = true
                world.advanceTime(cfg.preAdvance)
                print(string.format("RESTCHECK advanced %d hours before the scenario", cfg.preAdvance))
            end
            if cfg.bed and not placed and t >= cfg.bed[6] then
                placed = true
                player:teleport(player.cell, util.vector3(cfg.bed[1], cfg.bed[2], cfg.bed[3]), { rotation = util.transform.rotateZ(cfg.bed[4]) })
                player:sendEvent('RestLook', cfg.bed[5])
                print('RESTCHECK player placed at the bed spot')
            end
            if cfg.hurtAt and cfg.hurtAt > 0 and not hurt and t >= cfg.hurtAt then
                hurt = true
                player:sendEvent('RestHurt', { health = cfg.hurtHealth or 30 })
            end
            if cfg.saveAt and cfg.saveAt > 0 and t >= cfg.saveAt then
                storage.globalSection('REST10'):set('saveNow', true)
            end
        end,
    },
    eventHandlers = {
        RestShotAll = function() local p = world.players[1]; if p then p:sendEvent('RestShot') end end,
    },
}
