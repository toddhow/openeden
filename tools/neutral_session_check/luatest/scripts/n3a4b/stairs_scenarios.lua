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
    },
}
