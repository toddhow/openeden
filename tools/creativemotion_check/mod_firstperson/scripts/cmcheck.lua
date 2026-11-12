-- First-person copy (task 0124, stage C0) of the scenario of tools/creativemotion_check (task 0067, stage 0): the player stands, walks, runs, stops,
-- turns in place, draws a weapon, attacks, takes a hit and sheathes. Each phase lasts a fixed number of frames, not
-- seconds, so that two runs change phase on the same frame. The log line "CMPHASE <name> t=<seconds>" marks each
-- change; tools/creativemotion_check/check.sh and creativemotioncheck read it together with the game's CMTRACE lines.
local self = require('openmw.self')
local types = require('openmw.types')
local core = require('openmw.core')
local camera = require('openmw.camera')

-- Frames before the scenario starts: the game is loaded and the player is standing still.
local settleSeconds = 3

-- The phases, in order. `frames` is how long each lasts; a phase after the last one is 'end'.
local phases = {
    { name = 'idle', frames = 120 },
    { name = 'walk', frames = 120 },
    { name = 'run', frames = 120 },
    { name = 'stop', frames = 120 },
    { name = 'turn', frames = 120 },
    { name = 'stop2', frames = 60 },
    { name = 'draw', frames = 60 },
    { name = 'drawn_idle', frames = 60 },
    { name = 'attack', frames = 90 },
    { name = 'drawn_idle2', frames = 60 },
    { name = 'hit', frames = 90 },
    { name = 'drawn_idle3', frames = 60 },
    { name = 'sheathe', frames = 60 },
    { name = 'end', frames = 1000000 },
}

local frame = nil -- frames since the scenario started; nil while settling
local startTime = nil
local index = 1
local phaseFrame = 0

local function enter(phase)
    local t = core.getSimulationTime() - startTime
    print('CMPHASE ' .. phase.name .. ' t=' .. string.format('%.3f', t))
    if phase.name == 'hit' then
        -- A small hit with the player as its own attacker: a hit reaction needs an attacker, and 2 points of damage
        -- are below the knockdown threshold (agility * fKnockDownMult), so the reaction never depends on a dice roll.
        types.Actor._onHit(self, { damage = { health = 2 }, successful = true, sourceType = 'melee', attacker = self })
    end
end

return {
    engineHandlers = {
        onFrame = function()
            if not startTime then
                if core.getSimulationTime() <= settleSeconds then return end
                startTime = core.getSimulationTime()
                frame = 0
                enter(phases[index])
            end
            if camera.getMode() ~= camera.MODE.FirstPerson then camera.setMode(camera.MODE.FirstPerson) end
            frame = frame + 1
            phaseFrame = phaseFrame + 1
            if phaseFrame > phases[index].frames and index < #phases then
                index = index + 1
                phaseFrame = 1
                enter(phases[index])
            end
            local name = phases[index].name
            local c = self.controls
            c.movement = 0
            c.sideMovement = 0
            c.yawChange = 0
            c.use = 0
            c.run = false
            if name == 'walk' then
                c.movement = 1
            elseif name == 'run' then
                c.movement = 1
                c.run = true
            elseif name == 'turn' then
                c.yawChange = 0.03 -- radians per frame: about 100 degrees per second at 60 frames per second
            elseif name == 'attack' then
                c.use = 1
            end
            if name == 'draw' or name == 'drawn_idle' or name == 'attack' or name == 'drawn_idle2' or name == 'hit'
                or name == 'drawn_idle3' then
                types.Actor.setStance(self, types.Actor.STANCE.Weapon)
            elseif name == 'sheathe' then
                types.Actor.setStance(self, types.Actor.STANCE.Nothing)
            end
        end,
    },
}
