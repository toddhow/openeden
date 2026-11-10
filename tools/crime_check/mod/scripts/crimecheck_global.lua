-- Task 0098 move 7 check: commits four crimes against a guard it puts next to the player (the NPC nearest to the player if
-- the guard cannot be made) (through the public
-- interfaces.Crimes.commitCrime, which runs MechanicsManager::commitCrime and so the justice rules), one after the
-- other, with the victim already aware, and prints for each the return value, the player's bounty and the victim's
-- disposition, one line each, for the check script to compare between two executables started with the same seed.
-- The player's bounty is set to 0 before each crime. Waits some frames between crimes so that the AI reaction is in force.
local world = require('openmw.world')
local types = require('openmw.types')
local core = require('openmw.core')
local I = require('openmw.interfaces')
local util = require('openmw.util')

local OT = types.Player.OFFENSE_TYPE
local scenarios = {
    { name = 'trespass', type = OT.Trespassing, arg = 0 },
    { name = 'theft', type = OT.Theft, arg = 120 },
    { name = 'pickpocket', type = OT.Pickpocket, arg = 0 },
    { name = 'assault', type = OT.Assault, arg = 0 },
}
local startReal = nil
local step = 0
local waitFrames = 0
local guard = nil

local function nearestNpc(player)
    local best, bestDist = nil, nil
    for _, actor in pairs(world.activeActors) do
        if types.NPC.objectIsInstance(actor) and actor ~= player and types.Actor.stats.dynamic.health(actor).current > 0 then
            local d = (actor.position - player.position):length()
            if best == nil or d < bestDist or (d == bestDist and actor.recordId < best.recordId) then
                best, bestDist = actor, d
            end
        end
    end
    return best
end

return {
    engineHandlers = {
        onUpdate = function()
            startReal = startReal or core.getRealTime()
            if step > #scenarios or core.getRealTime() - startReal < 4 then return end
            if waitFrames > 0 then waitFrames = waitFrames - 1 return end
            local player = world.players[1]
            if guard == nil then
                -- a guard (Alarm 100) next to the player: it reports the crimes, so the bounty is not always 0
                local ok, created = pcall(world.createObject, 'hlaalu guard', 1)
                if ok then
                    guard = created
                    guard:teleport(player.cell, player.position + util.vector3(200, 0, 0))
                    waitFrames = 30
                    return
                end
                guard = false
            end
            if step == #scenarios then
                types.Player.setCrimeLevel(player, 0)
                print('CRIMECHECK done')
                step = step + 1
                return
            end
            step = step + 1
            local scenario = scenarios[step]
            local victim = guard or nearestNpc(player)
            if victim == nil then print('CRIMECHECK no victim') return end
            types.Player.setCrimeLevel(player, 0)
            local result = I.Crimes.commitCrime(player,
                { victim = victim, type = scenario.type, arg = scenario.arg, victimAware = true })
            print(string.format('CRIMECHECK %s victim=%s seen=%s bounty=%d disposition=%d', scenario.name,
                victim.recordId, tostring(result.wasCrimeSeen), types.Player.getCrimeLevel(player),
                types.NPC.getDisposition(victim, player)))
            waitFrames = 10
        end,
    },
}
