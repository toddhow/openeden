-- Start in WhiterunBanneredMare: the front door leads to the exterior and is refused.
local common = require('scripts.n4doors.common')
return common.run({
    { at = 6, what = 'report' }, { at = 6, what = 'shot' },
    { at = 8, what = 'door', base = 'skyrimse:b152263', near = { -128, -1152, 120 } },
    { at = 11, what = 'shot' }, { at = 12, what = 'report' },
})
