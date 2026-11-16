-- task 0232: stands the player before the Bannered Mare's ground floor interior door (wrdoor02, base 606610), facing it;
-- the activate key arrives through OPENEDEN_RMLUI_INPUT_SCRIPT ("seqdoor": [GUI] rmlui = true, use at least 3000 frames).
local common = require('scripts.act.common')
return common.run({ base = 'skyrimse:b606610', near = { -128, 384, 64 }, from = { -128, 200 }, dist = 110 })
