-- Task 0127: activates the locked strongbox of the Bannered Mare (key 0, level 25): refused, as lock picking is not applied.
local common = require('scripts.n4doors.common')
return common.run({
    { at = 6, what = 'report' },
    { at = 8, what = 'door', base = 'skyrimse:b960290', near = { -828, -227, 95 } },
    { at = 9, what = 'shot' }, { at = 12, what = 'report' },
})
