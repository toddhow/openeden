-- The player's part of tools/extension_world_check/check.sh: the engine's own ground height to compare the extension's cast
-- with, and a melee attack.
local core = require('openmw.core')
local nearby = require('openmw.nearby')
local self = require('openmw.self')
local types = require('openmw.types')
local util = require('openmw.util')

-- The same numbers as the extension's cast (kProbeTopM, kProbeLengthM) and global script's PROBE_M.
local UNITS_PER_METER = 69.99125109
local PROBE_M = 58.5216
local TOP_M = 60
local LENGTH_M = 120

-- From the integration test 'player weapon attack' (scripts/data/integration_tests/test_lua_api/player.lua).
local WEAPON_ID = 'basic_dagger1h'
local ATTACK_SECONDS = 25
local SWING_SECONDS = 0.5
local HIGH_ATTRIBUTE = 1000

local attack = nil

local function printGround()
    local x = PROBE_M * UNITS_PER_METER
    local top = util.vector3(x, x, TOP_M * UNITS_PER_METER)
    local bottom = util.vector3(x, x, (TOP_M - LENGTH_M) * UNITS_PER_METER)
    local result = nearby.castRay(top, bottom, {
        collisionType = nearby.COLLISION_TYPE.World + nearby.COLLISION_TYPE.HeightMap,
    })
    if result.hit then
        print(string.format('LUA_GROUND %.3f', result.hitPos.z))
    else
        print('LUA_GROUND miss')
    end
end

local function finishAttack(hit)
    self.controls.use = self.ATTACK_TYPE.NoAttack
    attack = nil
    core.sendGlobalEvent('OeWorldCheck_AttackDone', { hit = hit })
end

local function updateAttack()
    local health = types.Actor.stats.dynamic.health(attack.target)
    if health.current < attack.health then
        finishAttack(true)
        return
    end
    local time = core.getSimulationTime()
    if time > attack.endTime then
        finishAttack(false)
        return
    end

    if not attack.ready then
        local weapon = types.Actor.inventory(self):find(WEAPON_ID)
        types.Actor.setEquipment(self, { [types.Actor.EQUIPMENT_SLOT.CarriedRight] = weapon })
        attack.ready = true
        return
    end
    types.Actor.setStance(self, types.Actor.STANCE.Weapon)

    if attack.nextTime < time then
        attack.use = attack.use == self.ATTACK_TYPE.NoAttack and self.ATTACK_TYPE.Any or self.ATTACK_TYPE.NoAttack
        attack.nextTime = time + SWING_SECONDS
    end
    self.controls.use = attack.use

    local direction = (attack.target.position - self.position):normalize()
    self.controls.yawChange = util.normalizeAngle(math.atan2(direction.x, direction.y) - self.rotation:getYaw())
end

return {
    engineHandlers = {
        onFrame = function()
            if attack ~= nil then
                updateAttack()
            end
        end,
    },
    eventHandlers = {
        OeWorldCheck_Ground = function()
            printGround()
            core.sendGlobalEvent('OeWorldCheck_GroundDone')
        end,
        OeWorldCheck_Attack = function(data)
            types.Actor.stats.attributes.strength(self).base = HIGH_ATTRIBUTE
            types.Actor.stats.attributes.agility(self).base = HIGH_ATTRIBUTE
            attack = {
                target = data.target,
                health = types.Actor.stats.dynamic.health(data.target).current,
                endTime = core.getSimulationTime() + ATTACK_SECONDS,
                nextTime = 0,
                use = self.ATTACK_TYPE.NoAttack,
                ready = false,
            }
        end,
    },
}
