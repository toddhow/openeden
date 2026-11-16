-- The Skyrim combat HUD's player script (task 0180). Started only when the session's game pack declares the package
-- `core.skyrimse.combat` (the REQUIRES tag in builtin.omwscripts); it draws nothing until combat runs in the session
-- (`[Game] neutral combat`, the package's active()) and needs `[GUI] rmlui` (without it `openeden.ui` does not exist
-- and this script does nothing).
local okUi, ui = pcall(require, 'openeden.ui')
if not okUi then return {} end
-- Task 0244: the restyled HUD (the pack setting `hud style`) draws everything this one does, so this one steps aside
if pcall(require, 'core.skyrimse.hudstyle') then return {} end
local combat = require('core.skyrimse.combat')
local hud = require('openeden.hud.skyrimcombat').create(ui, combat)

return {
  engineHandlers = {
    onFrame = function() hud:update() end,
  },
}
