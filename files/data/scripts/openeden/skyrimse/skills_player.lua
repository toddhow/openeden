-- Skyrim SE skills and perks window, the player half (task 0200): draws the pack's progression with `openeden.ui` (see
-- openeden/skyrimse/skills_windows.lua for the window logic). Needs `[GUI] rmlui`; without it `openeden.ui` is absent
-- and this script does nothing, and without the pack's `core.skyrimse.progression` namespace it is not even started.
--
-- The key is K (not bound by the game): the first press opens the window in GUI mode with no MyGUI window, the second
-- press, Escape or the Close button leave.
local core = require('openmw.core')
local input = require('openmw.input')
local I = require('openmw.interfaces')

local okUi, ui = pcall(require, 'openeden.ui')
local okProg, prog = pcall(require, 'core.skyrimse.progression')
if not (okUi and okProg) then
  return {}
end
local windows = require('openeden.skyrimse.skills_windows')

local MODE = 'Interface'
local view
local seenMode, waited = false, 0

local function leave()
  I.UI.setMode() -- drops every mode; the window follows in onFrame
end

view = windows.create{
  ui = ui,
  prog = prog,
  levelUp = function(name) core.sendGlobalEvent('SkyrimSkillsLevelUp', { vital = name }) end,
  buyPerk = function(id) core.sendGlobalEvent('SkyrimSkillsBuyPerk', { id = id }) end,
  close = leave,
}

local function show()
  view.open()
  seenMode, waited = false, 0
  I.UI.setMode(MODE, { windows = {} })
end

return {
  engineHandlers = {
    onKeyPress = function(key)
      if key.code ~= input.KEY.K then return end
      if view.isOpen() then
        leave()
      elseif prog.ready() and I.UI.getMode() == nil then
        show()
      end
    end,
    onFrame = function()
      if view.isOpen() then
        if I.UI.getMode() ~= nil then
          seenMode = true
        elseif seenMode or waited > 120 then
          view.dismiss()
        else
          waited = waited + 1
        end
      end
      view.update()
    end,
  },
}
