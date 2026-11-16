-- Skyrim SE shouts, the player half (task 0205): the cooldown indicator. Needs `[GUI] rmlui` (without it `openeden.ui`
-- is absent and this script does nothing) and the pack's `core.skyrimse.shouts` namespace (the builtin line carries
-- REQUIRES=). The shout itself is the pack's: the control is the action bound to the = key (A_CycleSpellRight).
local okUi, ui = pcall(require, 'openeden.ui')
local okShouts, shouts = pcall(require, 'core.skyrimse.shouts')
if not (okUi and okShouts) then
  return {}
end

local hud = require('openeden.skyrimse.shouthud').create{ ui = ui, shouts = shouts }

return {
  engineHandlers = {
    onFrame = function()
      hud.update()
    end,
  },
}
