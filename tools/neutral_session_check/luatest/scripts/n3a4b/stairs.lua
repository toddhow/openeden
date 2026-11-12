-- tools/neutral_session_check (task 0128, milestone M-S1): the player walks up and down real stairs in a neutral
-- Skyrim interior. Started when the check is given "stairs" as its screenshot argument (use at least 1500 frames).
--
-- Phase 0 (SCAN): prints a height map of the floor round each stair model (rays from above, World collision), so the
--         stair's shape is read from the data and not guessed.
-- Phase 1 (WALK): for each scenario the player is put at the foot of a stair facing up it, settles, then walks for WALK
--         frames at running speed; the script logs the player's z every 10 frames and the highest z reached. A player that
--         climbs reaches the top height; one that is stopped by the first riser stays at the foot's height.
--         The reverse scenario starts at the top facing down and logs how far the player went down.
local self = require('openmw.self')
local core = require('openmw.core')
local nearby = require('openmw.nearby')
local util = require('openmw.util')
local debug = require('openmw.debug')

local SETTLE_FRAMES = 200
local SETTLE = 40
local WALK = 240

-- Filled in from the height maps: name, start x, y, z, heading yaw (0 = +y, pi/2 = +x, pi = -y, -pi/2 = -x)
local scenarios = {}
local ok, cfg = pcall(require, 'scripts.n3a4b.stairs_scenarios')
if ok then scenarios = cfg.scenarios end
local scans = {}
if ok then scans = cfg.scans end

local frame = 0
local state = 'scan'
local index = 0
local phaseStart = 0
local maxZ, minZ = -1e9, 1e9
local startPos

local function floorAt(x, y, zTop, zBottom)
    local hit = nearby.castRay(util.vector3(x, y, zTop), util.vector3(x, y, zBottom),
        { ignore = self.object, collisionType = nearby.COLLISION_TYPE.World })
    if hit.hit then return hit.hitPos.z end
    return nil
end

local function scan(s)
    local half, step = s.half or 360, s.step or 24
    print(string.format('N4S scan %s around %.0f,%.0f ray z %.0f..%.0f, one char per %d units, value (z - %.0f)/8 in base 36',
        s.name, s.cx, s.cy, s.zTop, s.zBottom, step, s.z0))
    local y = s.cy + half
    while y >= s.cy - half do
        local row = {}
        local x = s.cx - half
        while x <= s.cx + half do
            local z = floorAt(x, y, s.zTop, s.zBottom)
            if z == nil then
                row[#row + 1] = '.'
            else
                local v = math.floor((z - s.z0) / 8 + 0.5)
                if v < 0 then v = 0 end
                if v > 35 then v = 35 end
                row[#row + 1] = string.sub('0123456789abcdefghijklmnopqrstuvwxyz', v + 1, v + 1)
            end
            x = x + step
        end
        print(string.format('N4S %5.0f %s', y, table.concat(row)))
        y = y - step
    end
end

local function fmt(p) return string.format('%.1f,%.1f,%.2f', p.x, p.y, p.z) end

return {
    engineHandlers = {
        onFrame = function()
            frame = frame + 1
            if state == 'scan' then
                if frame == SETTLE_FRAMES then
                    print(string.format('N4S player at %s', fmt(self.object.position)))
                    for _, s in ipairs(scans) do scan(s) end
                    state = 'next'
                end
            elseif state == 'next' then
                index = index + 1
                if index > #scenarios then
                    state = 'done'
                    print('N4S done')
                    return
                end
                local s = scenarios[index]
                core.sendGlobalEvent('N4BPlace', { x = s[2], y = s[3], z = s[4], yaw = s[5] })
                phaseStart = frame
                state = 'settle'
            elseif state == 'settle' then
                self.controls.movement = 0
                if frame - phaseStart >= SETTLE then
                    startPos = self.object.position
                    maxZ, minZ = startPos.z, startPos.z
                    print(string.format('N4S [%s] start %s', scenarios[index][1], fmt(startPos)))
                    phaseStart = frame
                    state = 'walk'
                end
            elseif state == 'walk' then
                self.controls.movement = 1
                self.controls.run = true
                local p = self.object.position
                maxZ = math.max(maxZ, p.z)
                minZ = math.min(minZ, p.z)
                local n = frame - phaseStart
                if n == 100 then debug.takeScreenshot() end
                if n % 20 == 0 then
                    print(string.format('N4S [%s] +%d at %s', scenarios[index][1], n, fmt(p)))
                end
                if n >= WALK then
                    local d = (util.vector3(p.x, p.y, 0) - util.vector3(startPos.x, startPos.y, 0)):length()
                    print(string.format('N4S [%s] end %s moved %.1f horizontally, z from %.2f: highest %.2f lowest %.2f',
                        scenarios[index][1], fmt(p), d, startPos.z, maxZ, minZ))
                    self.controls.movement = 0
                    state = 'next'
                end
            end
        end,
    },
}
