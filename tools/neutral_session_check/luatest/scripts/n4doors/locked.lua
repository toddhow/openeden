-- Start in Angarvunde01: a locked door to Angarvunde02 is refused (it opens with OPENEDEN_NEUTRAL_UNLOCK_DOORS=1).
local common = require('scripts.n4doors.common')
return common.run({
    { at = 6, what = 'report' }, { at = 6, what = 'shot' },
    { at = 8, what = 'door', base = 'skyrimse:b175020', near = { 2432, -7488, -992 } },
    { at = 9, what = 'shot' }, { at = 12, what = 'report' },
    { at = 20, what = 'report' }, { at = 20, what = 'shot' },
})
