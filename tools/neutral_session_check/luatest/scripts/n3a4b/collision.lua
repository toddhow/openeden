-- tools/neutral_session_check (task 0110, stage N3a-4b): proves collision from the model's triangles in a real game.
-- Phase 1 (frame 200): the player stands where the cell put it; the script logs the player's z and the height of the
--          first thing a ray finds straight below the player (the floor), so "stands on something, does not fall through
--          the floor" is two numbers.
-- Phase 2: the player is placed on open floor facing a known piece of furniture (the cell's own objects, positions taken
--          from OPENEDEN_NEUTRAL_COLLISION_TRACE), settles for 40 frames, and then walks toward it for 120 frames at
--          running speed. For each scenario the script logs where the player started and ended, the floor under it, and
--          how far the furniture was; a model with no collision would let it walk through to the far side.
-- Started only when the check is given "collision" as its screenshot argument.
local self = require('openmw.self')
local core = require('openmw.core')
local nearby = require('openmw.nearby')
local util = require('openmw.util')

local frame = 0
local STAND_FRAMES = 200
local SETTLE = 40
local WALK = 150 -- frames; a player that is not blocked moves about 135 units in this time (0.9 units a frame)

-- name, start x, y, z, heading yaw (0 = +y, pi/2 = +x), target description
local scenarios = {
    { 'round table', 0, -767, 72, math.pi / 2, 'CommonTableRound01 at 193.7,-767.1 (chairs at 208,-699 and 204,-830 beside it)' },
    { 'thin table', 200, -168, 72, math.pi / 2, 'NobleTableThin01 at 377.1,-167.8' },
    { 'bar counter', 110, -420, 72, math.pi / 2, 'BarStraight01 at 256,-416' },
    { 'table and chair', -454.8, -100, 72, math.pi, 'CommonChair02L at -464,-209 then CommonTableRound01 at -454.8,-277.2' },
}

local function floorBelow()
    local p = self.object.position
    local hit = nearby.castRay(p + util.vector3(0, 0, 10), p - util.vector3(0, 0, 400), { ignore = self.object, collisionType = nearby.COLLISION_TYPE.World })
    if hit.hit then return hit.hitPos.z end
    return nil
end

local function describe(hit, from)
    local o = hit.hitObject
    local what = '?'
    if o then
        what = string.format('%s at %.0f,%.0f,%.0f', tostring(o.recordId), o.position.x, o.position.y, o.position.z)
    end
    return string.format('%.1f units: %s', (hit.hitPos - from):length(), what)
end

-- Rays straight ahead at three heights and a 20 unit sphere at knee height: what the player is facing.
local function ahead()
    local base = self.object.position
    local yaw = self.object.rotation:getYaw()
    local dir = util.vector3(math.sin(yaw), math.cos(yaw), 0)
    local out = {}
    for _, h in ipairs({ 10, 40, 90 }) do
        local p = base + util.vector3(0, 0, h)
        local hit = nearby.castRay(p, p + dir * 250, { ignore = self.object, collisionType = nearby.COLLISION_TYPE.World })
        out[#out + 1] = string.format('h%d %s', h, hit.hit and describe(hit, p) or 'clear')
    end
    return table.concat(out, ' | ')
end

local index = 0
local phaseStart = 0
local startPos = nil
local maxZ = 0
local state = 'stand'

local function fmt(p) return string.format('%.1f,%.1f,%.2f', p.x, p.y, p.z) end

return {
    engineHandlers = {
        onFrame = function(dt)
            frame = frame + 1
            if frame <= STAND_FRAMES and frame % 50 == 0 then
                local f = floorBelow()
                print(string.format('N4B settle frame %d z=%.2f floor=%s', frame, self.object.position.z, f and string.format('%.2f', f) or 'none'))
            end
            if frame == STAND_FRAMES then
                local p = self.object.position
                local f = floorBelow()
                print(string.format('N4B stand at %s floor=%s', fmt(p), f and string.format('%.2f', f) or 'none'))
                state = 'next'
            end
            if state == 'next' then
                index = index + 1
                if index > #scenarios then
                    state = 'done'
                    print('N4B done')
                    return
                end
                local s = scenarios[index]
                core.sendGlobalEvent('N4BPlace', { x = s[2], y = s[3], z = s[4], yaw = s[5] })
                phaseStart = frame
                state = 'settle'
            elseif state == 'settle' then
                self.controls.movement = 0
                if frame - phaseStart >= SETTLE then
                    local s = scenarios[index]
                    local f = floorBelow()
                    startPos = self.object.position
                    print(string.format('N4B [%s] start %s floor=%s facing %s; target %s', s[1], fmt(startPos), f and string.format('%.2f', f) or 'none', ahead(), s[6]))
                    phaseStart = frame
                    maxZ = startPos.z
                    state = 'walk'
                end
            elseif state == 'walk' then
                self.controls.movement = 1
                self.controls.run = true
                maxZ = math.max(maxZ, self.object.position.z)
                if frame - phaseStart >= WALK then
                    local s = scenarios[index]
                    local p = self.object.position
                    local f = floorBelow()
                    local d = (util.vector3(p.x, p.y, 0) - util.vector3(startPos.x, startPos.y, 0)):length()
                    print(string.format('N4B [%s] end %s floor=%s moved %.1f units horizontally in %d frames, highest z %.2f; facing %s', s[1], fmt(p), f and string.format('%.2f', f) or 'none', d, WALK, maxZ, ahead()))
                    self.controls.movement = 0
                    state = 'next'
                end
            end
        end,
    },
}
