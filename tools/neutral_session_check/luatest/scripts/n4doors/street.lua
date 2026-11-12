-- Task 0138: start in WhiterunBanneredMare, leave by the front door into Whiterun's street, come back in by the same
-- door (the nearest one to where the player arrived), and go out and in once more.
local common = require('scripts.n4doors.common')
local BASE = 'skyrimse:b152263'
return common.run({
    { at = 6, what = 'report' }, { at = 6, what = 'shot' },
    { at = 8, what = 'door', base = BASE, near = { -128, -1152, 120 } },
    { at = 16, what = 'report' }, { at = 16, what = 'shot' },
    { at = 18, what = 'door', base = BASE, near = 'player' },
    { at = 26, what = 'report' }, { at = 26, what = 'shot' },
    { at = 28, what = 'door', base = BASE, near = { -128, -1152, 120 } },
    { at = 36, what = 'report' }, { at = 36, what = 'shot' },
    { at = 38, what = 'door', base = BASE, near = 'player' },
    { at = 46, what = 'report' }, { at = 46, what = 'shot' },
})
