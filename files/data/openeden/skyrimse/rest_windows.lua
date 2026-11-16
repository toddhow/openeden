-- The Skyrim SE wait and sleep window (task 0210, milestone M-S7), written with `openeden.ui` (decision 0015).
--
-- This module holds the whole window logic and talks only to what it is handed, so it runs over the headless UI backend
-- in tests:
--   env.ui       the `openeden.ui` library
--   env.rest     the `core.skyrimse.rest` package (Read functions: `can_rest`, `max_hours`, `rested`, `rested_hours_left`)
--   env.close()  leaves the interface mode (the host script's job)
--   env.send(t)  asks the global script to rest: t = { hours = n, sleeping = bool }
-- One window, three looks: wait (a slider of hours), sleep (the same, for a bed) and a message (a refusal or a notice,
-- with one OK button).
local M = {}

local WINDOW_WIDTH = 420
local MIN_HOURS = 1
local DEFAULT_WAIT_HOURS = 1
local DEFAULT_SLEEP_HOURS = 8

-- What a refusal word of the pack says, in the words the player reads.
local REFUSALS = {
  combat = 'You cannot rest while you are in combat.',
  enemies = 'You cannot rest with enemies nearby.',
  not_ready = 'You cannot rest yet.',
}

function M.refusalText(reason)
  return REFUSALS[reason] or ('You cannot rest (' .. tostring(reason) .. ').')
end

function M.create(env)
  local ui, rest = env.ui, env.rest
  local self = {}

  local kind = 'wait' -- 'wait', 'sleep' or 'message'
  local hours = ui.Value(DEFAULT_WAIT_HOURS)
  local title = ui.Value('Wait')
  local message = ui.Value('')
  local ratedText = ui.Value('')
  local hoursText = ui.Computed(function()
    local n = hours:get()
    return n == 1 and '1 hour' or (tostring(n) .. ' hours')
  end)
  local isMessage = ui.Value(false)
  local doc = nil
  local isOpen = false
  local maxHours = 24

  local function clamp(n)
    n = math.floor(tonumber(n) or MIN_HOURS)
    if n < MIN_HOURS then n = MIN_HOURS end
    if n > maxHours then n = maxHours end
    return n
  end

  local function refreshRested()
    if rest.rested() then
      ratedText:set(string.format('Rested: %.1f hours left', rest.rested_hours_left()))
    else
      ratedText:set('')
    end
  end

  local function build()
    local controls = function() return not isMessage:get() end
    return ui.Document{ name = 'skyrim-rest', class = 'panel',
      style = { position = 'absolute', left = 24, top = 24, width = WINDOW_WIDTH },
      build = function()
        return ui.Column{
          ui.Text{ text = title, class = 'title' },
          ui.Text{ text = message, class = 'info', style = { width = WINDOW_WIDTH - 24, ['white-space'] = 'pre-wrap' },
            visible = ui.Computed(function() return message:get() ~= '' end) },
          ui.Text{ text = hoursText, class = 'info', visible = ui.Computed(controls) },
          ui.Row{
            visible = ui.Computed(controls),
            ui.Button{ text = '-', class = 'btn', onClick = function() self.setHours(hours:get() - 1) end },
            ui.Slider{ value = hours, min = MIN_HOURS, max = maxHours, step = 1, width = 220,
              onChange = function(n) self.setHours(n) end },
            ui.Button{ text = '+', class = 'btn', onClick = function() self.setHours(hours:get() + 1) end },
          },
          ui.Text{ text = ratedText, class = 'info', visible = ui.Computed(function() return ratedText:get() ~= '' end) },
          ui.Row{
            ui.Button{ text = ui.Computed(function() return kind == 'sleep' and 'Sleep' or 'Wait' end), class = 'btn',
              visible = ui.Computed(controls), onClick = function() self.confirm() end },
            ui.Button{ text = ui.Computed(function() return isMessage:get() and 'OK' or 'Cancel' end), class = 'btn',
              onClick = function() env.close() end },
          },
        }
      end }
  end

  --- Opens the window: `what` is 'wait' or 'sleep'. A refusal of the pack shows its message instead of the controls.
  --- Returns true when the controls are shown.
  function self.open(what)
    self.dismiss()
    kind = what == 'sleep' and 'sleep' or 'wait'
    maxHours = math.max(MIN_HOURS + 1, math.floor(tonumber(rest.max_hours()) or 24))
    isOpen = true
    local reason = rest.can_rest()
    if reason ~= '' then
      kind = 'message'
      isMessage:set(true)
      title:set(what == 'sleep' and 'Sleep' or 'Wait')
      message:set(M.refusalText(reason))
    else
      isMessage:set(false)
      message:set('')
      title:set(kind == 'sleep' and 'Sleep' or 'Wait')
      hours:set(kind == 'sleep' and DEFAULT_SLEEP_HOURS or DEFAULT_WAIT_HOURS)
    end
    refreshRested()
    doc = build()
    return kind ~= 'message'
  end

  --- Opens a message with one OK button (an owned bed, say).
  function self.notice(text)
    self.dismiss()
    kind = 'message'
    isOpen = true
    isMessage:set(true)
    title:set('Sleep')
    message:set(tostring(text))
    refreshRested()
    doc = build()
  end

  function self.setHours(n)
    hours:set(clamp(n))
  end
  function self.hours() return hours:get() end
  function self.kind() return kind end
  function self.message() return message:get() end

  --- The Wait or Sleep button: asks the global script to rest, then leaves.
  function self.confirm()
    if not isOpen or kind == 'message' then return false end
    local reason = rest.can_rest()
    if reason ~= '' then
      -- the situation changed while the window was open (the world is paused, so this is rare): show why
      kind = 'message'
      isMessage:set(true)
      message:set(M.refusalText(reason))
      return false
    end
    env.send{ hours = clamp(hours:get()), sleeping = kind == 'sleep' }
    env.close()
    return true
  end

  --- Takes the window down (the mode was left). Safe when nothing is open.
  function self.dismiss()
    if doc then doc:destroy() end
    doc = nil
    isOpen = false
  end

  function self.isOpen() return isOpen end

  function self.update()
    if not isOpen then return end
    refreshRested()
  end

  return self
end

return M
