-- tools/neutral_session_check (task 0208): the player half of "n9pick": tilts the view to cfg.pitch (radians, positive =
-- down) and, when cfg.sneak, holds the sneak control, so the engine's own stance is what the pickpocket rule reads.
-- Task 0224: with cfg.scan the player casts rays over a grid of directions at real time cfg.scanAt and prints the nearest
-- hit of every record id (N9 scan lines), to find where an activatable object can be hit from.
local self = require('openmw.self')
local camera = require('openmw.camera')
local nearby = require('openmw.nearby')
local util = require('openmw.util')
local core = require('openmw.core')
local cfg = require('scripts.n9.cfg')
local scanned = 0
local startedAt = nil

local function scan()
    print('N9 scan start')
    local from = camera.getPosition()
    local yaw0 = self.rotation:getAnglesZYX()
    local best = {}
    for pi = -4, 19 do
        for yi = -12, 12 do
            local pitch, yaw = pi * 0.08, yaw0 + yi * 0.08
            local dir = util.vector3(math.sin(yaw) * math.cos(pitch), math.cos(yaw) * math.cos(pitch), -math.sin(pitch))
            local ok, res = pcall(nearby.castRay, from, from + dir * 190, { ignore = self.object })
            if not ok and not best.err then best.err = { d = 0, pitch = 0, yaw = 0, z = 0 }; print('N9 scan error ' .. tostring(res)) end
            if ok and res and res.hit and not res.hitObject then best.terrain = best.terrain or { d = (res.hitPos - from):length(), pitch = pitch, yaw = yi * 0.08, z = res.hitPos.z - from.z } end
            if ok and res and res.hit and res.hitObject then
                local id = tostring(res.hitObject.recordId)
                local d = (res.hitPos - from):length()
                if not best[id] or d < best[id].d then best[id] = { d = d, pitch = pitch, yaw = yi * 0.08, z = res.hitPos.z - from.z, pos = res.hitPos } end
            end
        end
    end
    for id, b in pairs(best) do
        print(string.format('N9 scan hit %s dist %.0f pitch %.2f yawoff %.2f dz %.0f at %.1f,%.1f,%.1f', id, b.d, b.pitch, b.yaw, b.z, b.pos and b.pos.x or 0, b.pos and b.pos.y or 0, b.pos and b.pos.z or 0))
    end
end

-- Task 0224: cfg.autoaim makes the player turn to the nearest spot (within the activation reach) where a ray hits the
-- object cfg.base, re-aimed every half second, so a run does not depend on where physics left the player.
local goalYaw, goalPitch = nil, nil
local lastAim = -1
local function autoaim()
    local now = core.getRealTime()
    if now - lastAim >= 0.5 then
        lastAim = now
        local from = camera.getPosition()
        local yaw0 = self.rotation:getAnglesZYX()
        local bestD, byaw, bpitch = math.huge, nil, nil
        local function hit(pitch, yaw)
            local dir = util.vector3(math.sin(yaw) * math.cos(pitch), math.cos(yaw) * math.cos(pitch), -math.sin(pitch))
            local ok, res = pcall(nearby.castRay, from, from + dir * 150, { ignore = self.object })
            if ok and res and res.hit and res.hitObject and tostring(res.hitObject.recordId) == cfg.base then
                return (res.hitPos - from):length()
            end
            return nil
        end
        for pi = -4, 19 do
            for yi = -12, 12 do
                local pitch, yaw = pi * 0.08, yaw0 + yi * 0.08
                local d = hit(pitch, yaw)
                -- a spot counts only when the four rays round it hit the same object (a margin for the turn's error)
                if d and d < bestD and hit(pitch + 0.05, yaw) and hit(pitch - 0.05, yaw) and hit(pitch, yaw + 0.05)
                    and hit(pitch, yaw - 0.05) then
                    bestD, byaw, bpitch = d, yaw, pitch
                end
            end
        end
        if byaw then goalYaw, goalPitch = byaw, bpitch end
    end
    if goalYaw then
        local dyaw = goalYaw - self.rotation:getAnglesZYX()
        dyaw = (dyaw + math.pi) % (2 * math.pi) - math.pi
        self.controls.yawChange = math.max(-0.05, math.min(0.05, dyaw))
        self.controls.pitchChange = math.max(-0.05, math.min(0.05, goalPitch - camera.getPitch()))
    end
end

return {
    engineHandlers = {
        onFrame = function()
            if cfg.sneak then self.controls.sneak = true end
            if cfg.autoaim then autoaim() elseif cfg.pitch ~= 0 then
                local delta = cfg.pitch - camera.getPitch()
                self.controls.pitchChange = math.max(-0.05, math.min(0.05, delta))
            end
            startedAt = startedAt or core.getRealTime()
            if cfg.scan and scanned < 1 and core.getRealTime() - startedAt > cfg.scan + scanned then
                scanned = scanned + 1
                print('N9 scan #' .. scanned .. ' at ' .. tostring(camera.getPosition()))
                scan()
            end
        end,
    },
}
