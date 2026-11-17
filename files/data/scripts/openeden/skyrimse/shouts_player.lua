-- Skyrim SE shouts, the player half (task 0205): the cooldown indicator. Needs `[GUI] rmlui` for the indicator (without
-- it `openeden.ui` is absent and only the messages show) and the pack's `core.skyrimse.shouts` namespace (the builtin
-- line carries REQUIRES=). The shout itself is the pack's: the control is the action bound to the = key
-- (A_CycleSpellRight). Task 0253: the messages the pack announces (a dragon's soul absorbed) show as the game's message.
--
-- Task 0266: the shout menu. The key H (not bound by the game) opens the window in GUI mode with no MyGUI window: the
-- shouts the player has learned a word of, their words, the dragon souls, and the Unlock and Equip buttons (see
-- openeden/skyrimse/shout_windows.lua). The second press of H, Escape or the Close button leave. The requests go to
-- shouts_global.lua, because only a global script may call the pack's Mutate functions.
local core = require('openmw.core')
local input = require('openmw.input')
local I = require('openmw.interfaces')
local ui = require('openmw.ui')
local okShouts, shouts = pcall(require, 'core.skyrimse.shouts')
if not okShouts then
  return {}
end
local okUi, rml = pcall(require, 'openeden.ui')

local hud = nil
local menu = nil
local MODE = 'Interface'
local seenMode, waited = false, 0
if okUi then
  hud = require('openeden.skyrimse.shouthud').create{ ui = rml, shouts = shouts }
  menu = require('openeden.skyrimse.shout_windows').create{
    ui = rml,
    shouts = shouts,
    unlock = function(id) core.sendGlobalEvent('SkyrimShoutsUnlock', { id = id }) end,
    equip = function(id) core.sendGlobalEvent('SkyrimShoutsEquip', { id = id }) end,
    close = function() I.UI.setMode() end, -- drops every mode; the window follows in onFrame
  }
end

local shown = 0

return {
  engineHandlers = {
    onKeyPress = function(key)
      if not menu or key.code ~= input.KEY.H then return end
      if menu.isOpen() then
        I.UI.setMode()
      elseif shouts.ready() and I.UI.getMode() == nil then
        menu.open()
        seenMode, waited = false, 0
        I.UI.setMode(MODE, { windows = {} })
      end
    end,
    onFrame = function()
      if hud then
        hud.update()
      end
      if menu then
        if menu.isOpen() then
          if I.UI.getMode() ~= nil then
            seenMode = true
          elseif seenMode or waited > 120 then
            menu.dismiss()
          else
            waited = waited + 1
          end
        end
        menu.update()
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
