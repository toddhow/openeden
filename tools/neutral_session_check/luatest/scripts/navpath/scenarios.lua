-- Queries for navpath (WhiterunBanneredMare). Coordinates come from OPENEDEN_NEUTRAL_COLLISION_TRACE: the front door
-- WRDragonDoor01 stands at (-128,-1152), the bar (BarStraight01) at (256,-480) with stools at x 185, the stair
-- WRIntFloorDirtStairs01 at (-1152,-576) climbs along +x from (-1340,-252) at z 70 to (-1070,-252) at z 305.
local door = { -128, -1040, 66 }
local bar = { 150, -420, 66 }
local footOfStairs = { -1340, -252, 66 }
local topOfStairs = { -1070, -252, 306 }
return {
    interior = {
        { name = 'door to bar', from = door, to = bar },
        { name = 'bar to door', from = bar, to = door },
        { name = 'door to foot of stairs', from = door, to = footOfStairs },
        { name = 'foot of stairs to top', from = footOfStairs, to = topOfStairs },
        { name = 'top of stairs to door', from = topOfStairs, to = door },
        { name = 'door to top of stairs', from = door, to = topOfStairs },
    },
    street = {},
    fast = false,
}
