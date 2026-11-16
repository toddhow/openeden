-- tools/neutral_session_check (task 0192): finds open ground for the crowd fight. After 6 s the player script casts a ray
-- down and 16 rays sideways (60 above the ground, up to 500 units) from every point of a 150 unit grid within 1800 units
-- of the player and prints the 8 points with the widest clear circle ("MANYSCAN open x y z clear").
local core = require('openmw.core')
local util = require('openmw.util')
local nearby = require('openmw.nearby')
local self = require('openmw.self')

local done = false
return {
    engineHandlers = {
        onFrame = function()
            if done or core.getRealTime() < 6 then return end
            done = true
            local p = self.position
            local spots = {}
            for gx = -1800, 1800, 150 do
                for gy = -1800, 1800, 150 do
                    local x, y = p.x + gx, p.y + gy
                    local down = nearby.castRay(util.vector3(x, y, p.z + 400), util.vector3(x, y, p.z - 400),
                        { collisionType = nearby.COLLISION_TYPE.HeightMap + nearby.COLLISION_TYPE.World })
                    if down.hit then
                        local z = down.hitPos.z
                        local clear = 500
                        for k = 0, 15 do
                            local a = k / 16 * 2 * math.pi
                            local r = nearby.castRay(util.vector3(x, y, z + 60),
                                util.vector3(x + math.cos(a) * 500, y + math.sin(a) * 500, z + 60),
                                { collisionType = nearby.COLLISION_TYPE.World })
                            if r.hit then clear = math.min(clear, (r.hitPos - util.vector3(x, y, z + 60)):length()) end
                        end
                        -- also the ground must be level over the circle: heights at 4 points 200 away
                        local flat = true
                        for k = 0, 3 do
                            local a = k / 4 * 2 * math.pi
                            local d = nearby.castRay(util.vector3(x + math.cos(a) * 200, y + math.sin(a) * 200, z + 200),
                                util.vector3(x + math.cos(a) * 200, y + math.sin(a) * 200, z - 200),
                                { collisionType = nearby.COLLISION_TYPE.HeightMap + nearby.COLLISION_TYPE.World })
                            if not d.hit or math.abs(d.hitPos.z - z) > 15 then flat = false end
                        end
                        if flat then spots[#spots + 1] = { x, y, z, clear } end
                    end
                end
            end
            table.sort(spots, function(a, b) if a[4] ~= b[4] then return a[4] > b[4] end return a[1] < b[1] end)
            for i = 1, math.min(8, #spots) do
                local s = spots[i]
                print(string.format('MANYSCAN open %.0f %.0f %.1f clear %.0f', s[1], s[2], s[3], s[4]))
            end
            print('MANYSCAN spots ' .. #spots)
        end,
    },
}
