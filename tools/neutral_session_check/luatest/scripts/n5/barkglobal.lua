-- tools/neutral_session_check (task 0231): the global half of "n5bark". The player visits the actors of the Bannered Mare
-- (BARK_TAGS, the base tags of their record ids) one after the other: placed BARK_FAR units east of the actor for BARK_WAIT
-- seconds (the pack sees it far), then moved to BARK_NEAR units over BARK_APPROACH seconds (it "comes near"), held there for
-- BARK_HOLD seconds, and a picture is taken BARK_SHOT seconds after arriving. Everything runs on simulation time. Prints
-- "N5BARK <t> <tag> <phase>".
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local cfg = require('scripts.n5.barkcfg')
local start = nil
local shot = {}
local said = {}

local function find(cell, tag)
    for _, o in ipairs(cell:getAll()) do
        if string.find(o.recordId, tag, 1, true) then return o end
    end
    return nil
end

local function lerp(a, b, k) return a + (b - a) * k end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getSimulationTime()
            local t = core.getSimulationTime() - start - cfg.start
            if t < 0 then return end
            local per = cfg.wait + cfg.approach + cfg.hold
            local index = math.floor(t / per) + 1
            local tag = cfg.tags[index]
            if tag == nil then return end
            local actor = find(player.cell, tag)
            if actor == nil then return end
            local local_t = t - (index - 1) * per
            local p = actor.position
            local dist, phase
            if local_t < cfg.wait then
                dist, phase = cfg.far, 'far'
            elseif local_t < cfg.wait + cfg.approach then
                dist, phase = lerp(cfg.far, cfg.near, (local_t - cfg.wait) / cfg.approach), 'approach'
            else
                dist, phase = cfg.near, 'near'
            end
            local px, py, pz = p.x + dist, p.y, p.z + 2
            local yaw = math.atan2(p.x - px, p.y - py)
            player:teleport(player.cell, util.vector3(px, py, pz), { rotation = util.transform.rotateZ(yaw) })
            if phase == 'near' and not shot[index] and local_t >= cfg.wait + cfg.approach + cfg.shot then
                shot[index] = true
                player:sendEvent('N5Shot')
                print(string.format('N5BARK %.2f %s shot', t, tag))
            end
            local key = tag .. phase
            if not said[key] then
                said[key] = true
                print(string.format('N5BARK %.2f %s %s', t, tag, phase))
            end
        end,
    },
}
