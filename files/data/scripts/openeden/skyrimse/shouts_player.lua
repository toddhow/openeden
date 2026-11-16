-- Skyrim SE shouts, the player half (task 0205): the cooldown indicator. Needs `[GUI] rmlui` for the indicator (without
-- it `openeden.ui` is absent and only the messages show) and the pack's `core.skyrimse.shouts` namespace (the builtin
-- line carries REQUIRES=). The shout itself is the pack's: the control is the action bound to the = key
-- (A_CycleSpellRight). Task 0253: the messages the pack announces (a dragon's soul absorbed) show as the game's message.
local ui = require('openmw.ui')
local okShouts, shouts = pcall(require, 'core.skyrimse.shouts')
if not okShouts then
  return {}
end
local okUi, rml = pcall(require, 'openeden.ui')

local hud = nil
if okUi then
  hud = require('openeden.skyrimse.shouthud').create{ ui = rml, shouts = shouts }
end

local shown = 0

return {
  engineHandlers = {
    onFrame = function()
      if hud then
        hud.update()
      end
      local ok, count = pcall(shouts.notification_count)
      if not ok then return end
      if count < shown then shown = 0 end -- a load: the state started over
      if count > shown then
        local first = math.max(shown + 1, count - 15)
        for n = first, count do
          local text = shouts.notification(n)
          if text ~= '' then ui.showMessage(text) end
        end
        shown = count
      end
    end,
  },
}
