-- Scenarios for stairs.lua (WhiterunBanneredMare). The two stair models are WRIntFloorDirtStairs01 at (-576,-512,64) and
-- (-1152,-576,64) (OPENEDEN_NEUTRAL_COLLISION_TRACE).
return {
    scans = {
        { name = 'stairs A', cx = -576, cy = -512, z0 = 64, zTop = 300, zBottom = 0, half = 360, step = 24 },
        { name = 'stairs B', cx = -1152, cy = -576, z0 = 64, zTop = 300, zBottom = 0, half = 360, step = 24 },
    },
    scenarios = {
        { "stairs B up (+x)", -1340, -252, 70, math.pi / 2 },
        { "stairs B down (-x)", -1070, -252, 305, -math.pi / 2 },
        -- Task 0159: from the top of the west stairs' second flight (z 328): east into the wall, south along the strip.
        { "stairs B top east", -1052, -258, 334, math.pi / 2 },
        { "stairs B top south", -1040, -262, 334, math.pi },
        { "east block west (-x) at y -258", -1000, -258, 334, -math.pi / 2 },
        { "east block west (-x) at y -300", -1000, -300, 334, -math.pi / 2 },
        { "stairs B top east y -230", -1052, -230, 334, math.pi / 2 },
    },
}
