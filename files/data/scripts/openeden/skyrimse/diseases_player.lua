-- Skyrim SE diseases, the player half (task 0240): shows "You have contracted ..." and the cure messages as the game's
-- message line, and keeps a small status line of the diseases the player has (the game has no window that lists active
-- effects here). Started only when the pack declares `core.skyrimse.diseases` (the pack setting `diseases`); the status
-- line needs `[GUI] rmlui` (without `openeden.ui` only the messages show).
local ui = require('openmw.ui')
local okD, diseases = pcall(require, 'core.skyrimse.diseases')
if not okD then
  return {}
end
local okUi, rml = pcall(require, 'openeden.ui')

local shown = 0
local seenRevision = -1
local lineText, lineShown
local built = false

local function names()
  local out = {}
  for n = 1, diseases.count() do
    out[#out + 1] = diseases.name(n)
  end
  return table.concat(out, ', ')
end

local function build()
  built = true
  lineText = rml.Value('')
  lineShown = rml.Value(false)
  rml.Hud{
    name = 'diseases-status',
    style = { position = 'absolute', left = 16, bottom = 120, width = 420 },
    build = function()
      return rml.Text{ class = 'diseases-status', visible = lineShown, text = lineText,
        style = { ['text-align'] = 'left', color = 'rgb(214, 190, 96)' } }
    end }
end

return {
  engineHandlers = {
    onFrame = function()
      local count = diseases.notification_count()
      if count < shown then shown = 0 end -- a load: the service started over
      if count > shown then
        local first = math.max(shown + 1, count - 15)
        for n = first, count do
          local text = diseases.notification(n)
          if text ~= '' then ui.showMessage(text) end
        end
        shown = count
      end
      if okUi then
        local revision = diseases.revision()
        if revision ~= seenRevision then
          seenRevision = revision
          if not built and diseases.count() > 0 then build() end
          if built then
            lineShown:set(diseases.count() > 0)
            lineText:set('Diseased: ' .. names())
          end
        end
      end
    end,
  },
}
