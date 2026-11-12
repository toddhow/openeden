-- Task 0139: the gate scenario of task 0138 with a per-frame trace of the player around the tp, the gate door and the
-- door back (the case that drifted); the trace thins out two seconds after each action.
local common = require('scripts.n4doors.common')
local BASE = 'skyrimse:b330099'
return common.run({
    { at = 6, what = 'report' },
    { at = 8, what = 'door', base = 'skyrimse:b152263', near = { -128, -1152, 120 } },
    { at = 16, what = 'report' },
    { at = 17.9, what = 'trace', on = true, step = 0 },
    { at = 18, what = 'tp', pos = { 19320, -7433, -3547 } },
    { at = 20, what = 'trace', on = true, step = 1 },
    { at = 24, what = 'report' },
    { at = 24.4, what = 'trace', on = true, step = 0 },
    { at = 24.5, what = 'door', base = BASE, near = { 19367, -7433, -3547 } },
    { at = 26, what = 'trace', on = true, step = 1 },
    { at = 32, what = 'report' },
    { at = 32.9, what = 'trace', on = true, step = 0 },
    { at = 33, what = 'door', base = BASE, near = 'player' },
    { at = 35, what = 'trace', on = true, step = 1 },
    { at = 48, what = 'report' },
})
