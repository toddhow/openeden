-- Start in DawnstarBarracks: leave through the door to DawnstarBarracksJail, come back, go again, come back.
local common = require('scripts.n4doors.common')
local BASE = 'skyrimse:b171184'
local BARRACKS = { 1025, 704, -256 }
local JAIL = { 1568, -613, 0 }
return common.run({
    { at = 6, what = 'report' }, { at = 6, what = 'shot' },
    { at = 8, what = 'door', base = BASE, near = BARRACKS },
    { at = 16, what = 'report' }, { at = 16, what = 'shot' },
    { at = 18, what = 'door', base = BASE, near = JAIL },
    { at = 26, what = 'report' }, { at = 26, what = 'shot' },
    { at = 28, what = 'door', base = BASE, near = BARRACKS },
    { at = 36, what = 'report' }, { at = 36, what = 'shot' },
    { at = 38, what = 'door', base = BASE, near = JAIL },
    { at = 46, what = 'report' }, { at = 46, what = 'shot' },
})
