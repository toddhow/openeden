-- Task 0139: teleports the player (onGround) to a list of spots of WhiterunWorld, one every 4 s, and reports where it
-- is 3 s later, to see where the player stays put and where it jumps. Spots from the street out to the gate.
local common = require('scripts.n4doors.common')
local spots = {
    { 25000, -7600 }, { 23000, -7500 }, { 21500, -7433 }, { 20500, -7433 }, { 19900, -7433 }, { 19516, -7428 },
    { 19320, -7433 }, { 19000, -7433 }, { 18600, -7433 }, { 20500, -6000 }, { 20500, -9000 },
}
local steps = {
    { at = 6, what = 'report' },
    { at = 8, what = 'door', base = 'skyrimse:b152263', near = { -128, -1152, 120 } },
    { at = 16, what = 'report' },
}
local t = 18
for _, s in ipairs(spots) do
    steps[#steps + 1] = { at = t, what = 'tp', pos = { s[1], s[2], -2800 } }
    steps[#steps + 1] = { at = t + 0.2, what = 'trace', on = true, step = 0.7 }
    steps[#steps + 1] = { at = t + 3.5, what = 'report' }
    t = t + 4
end
return common.run(steps)
