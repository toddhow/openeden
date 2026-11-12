-- Task 0128: like locked.lua; run with OPENEDEN_NEUTRAL_KEYS=skyrimse:<hex key> and the locked door opens, then the way
-- back is listed (step 'near') so its base can be given to the 'door' steps of the second half.
local common = require('scripts.n4doors.common')
return common.run({
    { at = 6, what = 'report' }, { at = 6, what = 'shot' },
    { at = 8, what = 'door', base = 'skyrimse:b175020', near = { 2432, -7488, -992 } },
    { at = 11, what = 'report' }, { at = 11, what = 'shot' },
    -- the way back (the same base, 96 units from the arrival), then the first door again: it is open without asking
    { at = 13, what = 'door', base = 'skyrimse:b175020', near = { 2816, -7488, -992 } },
    { at = 17, what = 'report' },
    { at = 18, what = 'door', base = 'skyrimse:b175020', near = { 2432, -7488, -992 } },
    { at = 22, what = 'report' }, { at = 22, what = 'shot' },
})
