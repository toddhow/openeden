-- The Skyrim sneak eye and the sneak attack message (task 0227; decision 0015, the style of task 0180's combat HUD).
--
--   local hud = require('openeden.hud.skyrimsneak').create(require('openeden.ui'), require('core.skyrimse.sneak'), now)
--   hud:update()   -- once a frame; builds the documents the first frame the player sneaks, then follows the numbers
--
-- `sneak` is the script package the Skyrim pack declares (game_packs/skyrimse/include/game_skyrimse/sneak.hpp):
-- `eye()` (-1 not sneaking, 0 hidden = closed eye, 1 caution = half open, 2 detected = open eye), `message()`,
-- `message_id()` (changes with every sneak attack) and `message_seconds()`. `now` returns real seconds (the script
-- passes `core.getRealTime`). Two small documents: the eye (a lid shape, a pupil and a word) and the message. The
-- looks are inline and this file's own (an eye colour is meaning, not theme).
local M = {}

local EYE_WIDTH, EYE_HEIGHT = 64, 28
-- lid height in pixels per state: closed is a line, half is a squint, open is the whole eye
local LID = { [0] = 4, [1] = 14, [2] = 28 }
local WORD = { [0] = 'Hidden', [1] = 'Caution', [2] = 'Detected' }
-- RmlUi's rgba() alpha is 0 to 255; the eye is white when hidden, amber on caution, red when seen
local COLOUR = { [0] = 'rgb(235, 235, 235)', [1] = 'rgb(232, 176, 60)', [2] = 'rgb(210, 52, 52)' }

function M.create(ui, sneak, now)
  local self = { built = false, docs = {}, shownId = 0, hideAt = 0 }

  local lidHeight = ui.Value(LID[0] .. 'px')
  local lidTop = ui.Value(((EYE_HEIGHT - LID[0]) / 2) .. 'px')
  local colour = ui.Value(COLOUR[0])
  local pupilShown = ui.Value(false)
  local word = ui.Value(WORD[0])
  local eyeShown = ui.Value(false)
  local messageShown = ui.Value(false)
  local messageText = ui.Value('')

  local function build()
    self.built = true
    self.docs[#self.docs + 1] = ui.Hud{
      name = 'sneak-eye',
      style = { position = 'absolute', left = '50%', bottom = 210, width = 120, ['margin-left'] = -60 },
      build = function()
        return ui.Column{
          class = 'sneak-eye-box',
          visible = eyeShown,
          style = { width = '100%', ['align-items'] = 'center' },
          ui.Stack{
            style = { width = EYE_WIDTH, height = EYE_HEIGHT },
            ui.Frame{
              class = 'sneak-eye-lid',
              style = { position = 'absolute', left = 0, top = lidTop, width = EYE_WIDTH, height = lidHeight,
                ['border-width'] = 2, ['border-color'] = colour, ['border-radius'] = '14px',
                ['background-color'] = 'rgba(0, 0, 0, 140)' },
            },
            ui.Frame{
              class = 'sneak-eye-pupil',
              visible = pupilShown,
              style = { position = 'absolute', left = 26, top = 8, width = 12, height = 12,
                ['border-radius'] = '6px', ['background-color'] = colour },
            },
          },
          ui.Text{ class = 'sneak-eye-word', text = word,
            style = { ['text-align'] = 'center', ['margin-top'] = 4 } },
        }
      end }
    self.docs[#self.docs + 1] = ui.Hud{
      name = 'sneak-message',
      style = { position = 'absolute', left = '50%', bottom = 290, width = 480, ['margin-left'] = -240 },
      build = function()
        return ui.Text{ class = 'sneak-message', visible = messageShown, text = messageText,
          style = { ['text-align'] = 'center' } }
      end }
  end

  function self:update()
    local eye = tonumber(sneak.eye()) or -1
    local id = tonumber(sneak.message_id()) or 0
    if not self.built then
      if eye < 0 and id == 0 then return end
      build()
      self.shownId = id -- a message from before the documents existed is not shown
    end
    eyeShown:set(eye >= 0)
    if eye >= 0 then
      local lid = LID[eye] or LID[0]
      lidHeight:set(lid .. 'px')
      lidTop:set(((EYE_HEIGHT - lid) / 2) .. 'px')
      colour:set(COLOUR[eye] or COLOUR[0])
      pupilShown:set(eye >= 1)
      word:set(WORD[eye] or WORD[0])
    end
    local t = now()
    if id ~= self.shownId then
      self.shownId = id
      messageText:set(tostring(sneak.message() or ''))
      messageShown:set(true)
      self.hideAt = t + (tonumber(sneak.message_seconds()) or 3)
    elseif messageShown:get() and t >= self.hideAt then
      messageShown:set(false)
    end
  end

  function self:destroy()
    for _, doc in ipairs(self.docs) do doc:destroy() end
    self.docs = {}
    self.built = false
  end

  return self
end

return M
