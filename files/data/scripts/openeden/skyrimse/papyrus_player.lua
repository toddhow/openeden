-- Skyrim SE Papyrus notifications (task 0198): shows what a script's Debug.Notification said as the game's own message
-- line. The pack's host keeps a ring of the last few; this script shows each new one once, oldest first. Without the
-- pack's `core.skyrimse.papyrus` namespace it is not started.
local ui = require('openmw.ui')
local okP, papyrus = pcall(require, 'core.skyrimse.papyrus')
if not okP then
  return {}
end

local shown = 0

return {
  engineHandlers = {
    onFrame = function()
      local count = papyrus.notification_count()
      if count < shown then shown = 0 end -- a load: the host started over
      if count > shown then
        local first = math.max(shown + 1, count - 15)
        for n = first, count do
          local text = papyrus.notification(n)
          if text ~= '' then ui.showMessage(text) end
        end
        shown = count
      end
    end,
  },
}
