-- Task 0069 stage e review: the player half of the torch scene of tools/mergedbody_check (SCENE=torches). The player
-- stands still, dressed (shirt, pants, shoes, gloves), and a static camera between it and the twenty NPCs of
-- mbtorch_global.lua behind it looks down at them. One frame by day; then the hour goes from noon to night and back,
-- CYCLES times, each change making all twenty take their torch out or put it away in one frame; one frame of the last
-- night; then every NPC puts on a full set of armour (a quiet window of ARMOURED seconds, and a frame); then the
-- inventory with the paper doll. Every phase is marked with "MBPHASE <name>", every screenshot with
-- "MBSHOT <name>". Use it with [Cells] load trace = true to see each change's frame in the trace.
local self = require('openmw.self')
local core = require('openmw.core')
local types = require('openmw.types')
local util = require('openmw.util')
local camera = require('openmw.camera')
local debug = require('openmw.debug')
local I = require('openmw.interfaces')

local SETTLE = 6 -- seconds of real time before anything happens (cells and actors load)
local DRESS = 2 -- seconds after SETTLE: the items have arrived
local DAY_SHOT = 5 -- seconds after SETTLE: the lines are dressed and still
local CYCLES = 4 -- night, then day: one cycle
local HOLD = 3 -- seconds per hour change: updateEquippedLight runs once a second
local NIGHT_HOUR = 22
local ARMOURED = 4 -- seconds of the quiet window with every NPC in a full set of armour
local DAY_HOUR = 12

local startReal = nil
local origin = nil
local yaw = 0
local ids = nil
local stage = 'settle'
local phaseStart = 0
local changes = 0
local shots = {}

local function toWorld(v)
    return origin + util.transform.rotateZ(yaw) * v
end

local function look(eye, at)
    -- The mode changes in the next frame, and a static position may only be set in the static mode.
    if camera.getMode() ~= camera.MODE.Static then
        camera.setMode(camera.MODE.Static)
        return
    end
    camera.setStaticPosition(eye)
    local d = at - eye
    camera.setYaw(math.atan2(d.x, d.y))
    camera.setPitch(-math.atan2(d.z, math.sqrt(d.x * d.x + d.y * d.y)))
end

local function enter(name, t)
    stage = name
    phaseStart = t
    print('MBPHASE ' .. name)
end

local function shot(name)
    if not shots[name] then
        shots[name] = true
        debug.takeScreenshot()
        print('MBSHOT ' .. name)
    end
end

local function hour(h)
    core.sendGlobalEvent('MBTorchHour', { hour = h })
end

return {
    eventHandlers = {
        MBTorchItems = function(data)
            ids = data.ids
        end,
    },
    engineHandlers = {
        onFrame = function()
            startReal = startReal or core.getRealTime()
            local t = core.getRealTime() - startReal - SETTLE
            local c = self.controls
            c.movement = 0
            c.sideMovement = 0
            c.yawChange = 0
            c.use = 0
            c.run = false
            if t < 0 then
                return
            end
            if origin then
                -- Behind the player and above it, looking down at the lines behind it.
                look(toWorld(util.vector3(0, -40, 230)), toWorld(util.vector3(0, -440, 50)))
            end
            if stage == 'settle' then
                types.Actor.setEquipment(self, {})
                origin = self.position
                yaw = self.rotation:getYaw()
                core.sendGlobalEvent('MBTorchSetup', { origin = origin, yaw = yaw,
                    forward = util.transform.rotateZ(yaw) * util.vector3(0, 1, 0),
                    right = util.transform.rotateZ(yaw) * util.vector3(1, 0, 0) })
                enter('setup', t)
            elseif stage == 'setup' then
                if ids and t >= DRESS then
                    local S = types.Actor.EQUIPMENT_SLOT
                    types.Actor.setEquipment(self, { [S.Shirt] = ids.shirt, [S.Pants] = ids.pants,
                        [S.Boots] = ids.shoes, [S.LeftGauntlet] = ids.lglove, [S.RightGauntlet] = ids.rglove })
                    enter('day', t)
                end
            elseif stage == 'day' then
                if t >= DAY_SHOT then
                    shot('row day')
                end
                if t >= DAY_SHOT + 1 then
                    changes = 0
                    enter('cycles', t)
                end
            elseif stage == 'cycles' then
                local n = math.floor((t - phaseStart) / HOLD)
                if n >= changes then
                    changes = changes + 1
                    if changes > 2 * CYCLES then
                        enter('night', t)
                        hour(NIGHT_HOUR)
                    else
                        -- Odd changes make it night (torches out), even ones day (torches away).
                        local night = changes % 2 == 1
                        print('MBPHASE change ' .. changes .. ' ' .. (night and 'night' or 'day'))
                        hour(night and NIGHT_HOUR or DAY_HOUR)
                    end
                end
            elseif stage == 'night' then
                if t - phaseStart >= HOLD then
                    shot('row night')
                end
                if t - phaseStart >= HOLD + 0.5 then
                    core.sendGlobalEvent('MBTorchArmour', { ids = ids })
                    enter('armour', t)
                end
            elseif stage == 'armour' then
                -- The armour arrives and is put on; then a quiet window of ARMOURED seconds (the frame cost of
                -- fully armoured merged bodies, against the same window of another build).
                if t - phaseStart >= HOLD then
                    enter('armoured', t)
                end
            elseif stage == 'armoured' then
                if t - phaseStart >= ARMOURED then
                    shot('row armoured')
                end
                if t - phaseStart >= ARMOURED + 0.5 then
                    I.UI.setMode('Interface', { windows = { 'Inventory' } })
                    enter('doll', t)
                end
            elseif stage == 'doll' then
                if t - phaseStart >= 1.5 then
                    shot('inventory doll')
                end
                if t - phaseStart >= 2 then
                    I.UI.setMode()
                    enter('quit', t)
                    core.quit()
                end
            end
        end,
    },
}
