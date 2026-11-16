-- The Skyrim sneak eye's player script (task 0227). Started only when the session's game pack declares the package
-- `core.skyrimse.sneak` (the REQUIRES tag in builtin.omwscripts, the pack setting `sneak`); needs `[GUI] rmlui`
-- (without it `openeden.ui` does not exist and this script does nothing).
local okUi, ui = pcall(require, 'openeden.ui')
if not okUi then return {} end
local core = require('openmw.core')
local sneak = require('core.skyrimse.sneak')
local hud = require('openeden.hud.skyrimsneak').create(ui, sneak, core.getRealTime)

return {
  engineHandlers = {
    onFrame = function() hud:update() end,
  },
}
