-- Skyrim SE quest journal, the player half (task 0185): draws the pack's quest journal with `openeden.ui` (see
-- openeden/skyrimse/journal_windows.lua for the window logic). Needs `[GUI] rmlui`; without it `openeden.ui` is absent and
-- this script does nothing, and without the pack's `core.skyrimse.journal` namespace it is not even started.
--
-- The key is J, OpenMW's own journal key: the built-in input script puts the engine mode `Journal` on the mode stack when
-- J is pressed (and again when it is pressed while the journal is open). This script takes that mode as its key: it
-- replaces it with GUI mode and no MyGUI window, and shows its own window; the second J leaves. There is no second key
-- handler, so the two cannot race.
local I = require('openmw.interfaces')

local okUi, ui = pcall(require, 'openeden.ui')
local okJr, jr = pcall(require, 'core.skyrimse.journal')
if not (okUi and okJr) then
  return {}
end
local windows = require('openeden.skyrimse.journal_windows')

local MODE = 'Interface'
local ENGINE_JOURNAL = 'Journal'
local view
local seenMode, waited = false, 0

local function leave()
  I.UI.setMode() -- drops every mode; the window follows in onFrame
end

view = windows.create{ ui = ui, jr = jr, close = leave }

local function show()
  view.open()
  seenMode, waited = false, 0
  -- GUI mode with no MyGUI window at all: the cursor, the pause and the RmlUi overlay's input. The mode stack of this
  -- script follows a frame later, so onFrame waits for it before it watches for J or for the mode to go.
  I.UI.setMode(MODE, { windows = {} })
end

return {
  engineHandlers = {
    onFrame = function()
      local mode = I.UI.getMode()
      if view.isOpen() then
        if not seenMode then
          if mode == MODE then
            seenMode = true
          else
            waited = waited + 1
            if waited > 120 then view.dismiss() end
          end
        elseif mode == ENGINE_JOURNAL then
          leave() -- J again
        elseif mode == nil then
          view.dismiss() -- the mode was left (Escape, Close, or the J above)
        end
      elseif mode == ENGINE_JOURNAL and jr.ready() then
        show()
      end
      view.update()
    end,
  },
}
