-- tools/neutral_session_check (task 0222, the player's Skyrim body): the player half of "pbody". A timeline of simulation
-- seconds (cfg.items, cfg.plan from check.sh, PB_*): the standard third person camera, then a static camera in front of
-- the player (standing), the gear equipped through the pack's inventory events, a walk, a run and a stretch of swings
-- (the pack's `combat test player swing` makes them), a picture at each step. Prints "PBODY" lines: logical events and
-- the player's position once a second, never anything that depends on a worker's timing.
local camera = require('openmw.camera')
local core = require('openmw.core')
local debug = require('openmw.debug')
local self = require('openmw.self')
local util = require('openmw.util')
local types = require('openmw.types')
local realSteps, realStart = nil, nil

local cfg = require('scripts.pbody.cfg')

local start = nil
local shots = {} -- sorted list of {t, kind}
local nextShot = 1
local mode = 'normal'
local lastLine = -1
local anchor, forward, right -- fixed when the walk starts

local function dir()
    local f = self.rotation * util.vector3(0, 1, 0)
    return util.vector3(f.x, f.y, 0):normalize()
end

local function frontCamera(p, f)
    -- in front of the player, looking at his chest
    return p + f * cfg.frontDistance + util.vector3(0, 0, cfg.frontHeight), p + util.vector3(0, 0, 70)
end

local function sideCamera(p, f)
    local r = util.vector3(f.y, -f.x, 0)
    return p + r * cfg.sideDistance + util.vector3(0, 0, cfg.sideHeight), p + util.vector3(0, 0, 60)
end

local function aim(pos, look)
    camera.setStaticPosition(pos)
    local dx, dy = look.x - pos.x, look.y - pos.y
    camera.setYaw(math.atan2(dx, dy))
    camera.setPitch(math.atan2(pos.z - look.z, math.sqrt(dx * dx + dy * dy)))
end

local pitchImpulse = 0 -- task 0245: a one-frame look up or down
local follow = false -- the static camera keeps the player at the side while he moves
local drive = { move = 0, run = false } -- written to the controls every frame (the engine clears them)
local plan = {}
local function at(t, fn) plan[#plan + 1] = { t = t, fn = fn, done = false } end

local function setup()
    local p0 = self.position
    if cfg.plan == 'probe' then
        -- which way can he walk: eight headings, 1.5 s each, the distance gained printed
        local base = 1.0
        for k = 0, 7 do
            local t0 = base + k * 3.0
            at(t0, function() self.controls.yawChange = math.pi / 4; if k == 0 then self.controls.yawChange = 0 end end)
            at(t0 + 0.2, function() drive.move = 1; anchor = self.position end)
            at(t0 + 1.7, function()
                drive.move = 0
                local p = self.position
                print(string.format('PBODY probe %d moved %.0f yaw %.2f', k, (p - anchor):length(), self.rotation:getAnglesZYX()))
            end)
        end
        return
    end
    if cfg.plan == 'fp' then
        -- task 0245: the first-person arms. The camera stays in first person all the time.
        at(1.0, function() debug.takeScreenshot(); print('PBODY shot fp idle') end)
        at(2.0, function() pitchImpulse = cfg.pitch or 0.6 end)
        at(2.6, function() debug.takeScreenshot(); print('PBODY shot fp looking') end)
        at(3.0, function() pitchImpulse = -(cfg.pitch or 0.6) end)
        at(4.0, function()
            for _, id in ipairs(cfg.items) do core.sendGlobalEvent('SkyrimInventoryEquip', { item = id }) end
            print('PBODY equip ' .. table.concat(cfg.items, ','))
        end)
        at(6.0, function() debug.takeScreenshot(); print('PBODY shot fp equipped') end)
        -- swings: the real attack key (OPENEDEN_RMLUI_INPUT_SCRIPT) or the pack's test rule; a picture every 0.1 s
        for i = 0, (cfg.swingCount or 30) - 1 do at(cfg.swingAt + (cfg.swingStep or 0.1) * i, function() debug.takeScreenshot(); print('PBODY shot fp swing ' .. i) end) end
        return
    end
    if cfg.plan == 'fp2' then
        -- task 0247: draw, sheathe and blends. The keys (F, the attack) come from OPENEDEN_RMLUI_INPUT_SCRIPT at frame 60 x t.
        -- The view looks a little down all the time so that the hands show once they are up.
        at(0.3, function() pitchImpulse = cfg.pitch or 0.3 end)
        at(1.0, function()
            for _, id in ipairs(cfg.items) do core.sendGlobalEvent('SkyrimInventoryEquip', { item = id }) end
            print('PBODY equip ' .. table.concat(cfg.items, ','))
        end)
        if cfg.race then
            -- task 0247: a race through the character service (task 0225): the same requests its window makes. The
            -- window pauses the world (simulation time stands still), so these run on real time (see onFrame).
            realSteps = {
                { t = cfg.raceAt or 2.0, fn = function() core.sendGlobalEvent('SkyrimCharacterRequest', { kind = 'race', index = cfg.race }) end },
                { t = (cfg.raceAt or 2.0) + 0.3, fn = function() core.sendGlobalEvent('SkyrimCharacterRequest', { kind = 'female', value = cfg.female == true }) end },
                { t = (cfg.raceAt or 2.0) + 1.0, fn = function() core.sendGlobalEvent('SkyrimCharacterRequest', { kind = 'finish' }); print('PBODY character finished') end },
            }
            -- the creation window holds the interface mode; closing it as its Done button does
            realSteps[#realSteps + 1] = { t = (cfg.raceAt or 2.0) + 1.6, fn = function()
                local ok, I = pcall(require, 'openmw.interfaces')
                if ok and I.UI then I.UI.setMode(); print('PBODY mode cleared') end
            end }
            -- F, as the stance control does it, a few simulation seconds after the world runs again
            at(cfg.drawAt or 4.0, function() types.Actor.setStance(self, types.Actor.STANCE.Weapon); print('PBODY stance weapon') end)
        end
        at(2.0, function() debug.takeScreenshot(); print('PBODY shot fp2 sheathed') end)
        -- bursts of pictures (cfg.swingCount of them, cfg.swingStep seconds apart) from the times in cfg.bursts
        for b, t0 in ipairs(cfg.bursts or {}) do
            for i = 0, (cfg.swingCount or 18) - 1 do
                at(t0 + (cfg.swingStep or 0.1) * i, function() debug.takeScreenshot(); print('PBODY shot fp2 burst ' .. b .. ' ' .. i) end)
            end
        end
        return
    end
    local f0 = dir()
    at(0.5, function() debug.takeScreenshot(); print('PBODY shot first-person') end)
    at(1.0, function() camera.setMode(camera.MODE.ThirdPerson) end)
    at(3.0, function() debug.takeScreenshot(); print('PBODY shot third-person') end)
    at(4.0, function() camera.setMode(camera.MODE.Static) end)
    at(4.5, function() local p, l = frontCamera(self.position, dir()); aim(p, l) end)
    at(5.2, function() debug.takeScreenshot(); print('PBODY shot front standing') end)
    at(6.0, function()
        for _, id in ipairs(cfg.items) do core.sendGlobalEvent('SkyrimInventoryEquip', { item = id }) end
        print('PBODY equip ' .. table.concat(cfg.items, ','))
    end)
    at(8.0, function() local p, l = frontCamera(self.position, dir()); aim(p, l) end)
    at(8.5, function() debug.takeScreenshot(); print('PBODY shot front equipped') end)
    if cfg.plan == 'short' then return end
    -- walk
    at(9.5, function()
        anchor, forward = self.position, dir()
        right = util.vector3(forward.y, -forward.x, 0)
        follow = true
        drive.move, drive.run = 1, false
        print('PBODY walk')
    end)
    for i = 0, 3 do at(10.5 + 0.7 * i, function() debug.takeScreenshot(); print('PBODY shot walk ' .. i) end) end
    -- run
    at(13.5, function() drive.run = true; print('PBODY run') end)
    for i = 0, 3 do at(14.6 + 0.5 * i, function() debug.takeScreenshot(); print('PBODY shot run ' .. i) end) end
    at(17.0, function()
        follow = false
        drive.move, drive.run = 0, false
        print('PBODY stop')
        local p, l = sideCamera(self.position, dir())
        aim(p, l)
    end)
    -- swings (the pack's test rule swings every few seconds)
    for i = 0, 11 do at(18.0 + 0.4 * i, function() debug.takeScreenshot() end) end
end

return {
    engineHandlers = {
        onFrame = function()
            local now = core.getSimulationTime()
            if start == nil then
                start = now
                setup()
            end
            local t = now - start
            if realSteps then
                realStart = realStart or core.getRealTime()
                local rt = core.getRealTime() - realStart
                for _, step in ipairs(realSteps) do
                    if not step.done and rt >= step.t then step.done = true; step.fn() end
                end
            end
            self.controls.movement = drive.move
            self.controls.run = drive.run
            self.controls.pitchChange = pitchImpulse
            pitchImpulse = 0
            if follow then
                local p = self.position
                aim(p + right * cfg.sideDistance + util.vector3(0, 0, cfg.sideHeight), p + util.vector3(0, 0, 60))
            end
            for _, step in ipairs(plan) do
                if not step.done and t >= step.t then
                    step.done = true
                    step.fn()
                end
            end
            local whole = math.floor(t)
            if whole ~= lastLine then
                lastLine = whole
                local p = self.position
                print(string.format('PBODY t %d pos %.0f %.0f %.0f', whole, p.x, p.y, p.z))
            end
        end,
    },
}
