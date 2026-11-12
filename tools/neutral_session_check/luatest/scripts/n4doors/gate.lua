-- Task 0137: out of the Mare into Whiterun's street, to the Whiterun gate (a door of WhiterunWorld that leads to
-- Tamriel), through it, then back through the Tamriel side of the gate (locked in the data: needs
-- OPENEDEN_NEUTRAL_UNLOCK_DOORS=1 to open, else it is refused and the player stays in Tamriel).
local common = require('scripts.n4doors.common')
local BASE = 'skyrimse:b330099'
return common.run({
    { at = 6, what = 'report' },
    { at = 8, what = 'door', base = 'skyrimse:b152263', near = { -128, -1152, 120 } },
    { at = 16, what = 'report' },
    { at = 18, what = 'tp', pos = { 19320, -7433, -3547 } },
    { at = 22, what = 'report' }, { at = 22, what = 'shot' },
    { at = 24, what = 'door', base = BASE, near = { 19367, -7433, -3547 } },
    { at = 36, what = 'report' }, { at = 36, what = 'shot' },
    { at = 38, what = 'door', base = BASE, near = 'player' },
    { at = 48, what = 'report' }, { at = 48, what = 'shot' },
})
