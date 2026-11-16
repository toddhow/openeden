-- The Skyrim SE lock picking minigame window (task 0203), written with `openeden.ui` (decision 0015).
--
-- The window logic only; it talks to what it is handed, so it runs over the headless UI backend in tests:
--   env.ui       the `openeden.ui` library
--   env.lb       the `core.skyrimse.lockbooks` package (Read functions: the pack owns the minigame, this only draws it;
--                `lock_serial` counts every begin, so an old refusal is not taken for the answer to a new one)
--   env.begin(placement, level)       asks the pack to start an attempt with a new lockpick (a global script runs it)
--   env.input(angle, turning, dtMs)   one step of the minigame (angle in degrees, -90 to 90)
--   env.cancel()                      ends the attempt without using a lockpick
--   env.close()                       leaves the interface mode (the host script's job)
--
-- Controls: A and D (or the < and > buttons, 5 degrees a click) turn the pick, Space (or one click on Turn, which turns
-- for TURN_CLICK_SECONDS) turns the lock. Escape or Cancel leaves. A broken pick starts the next one when the player
-- has another; an opened lock closes the window after CLOSE_DELAY seconds.

local M = {}

local ANGLE_LIMIT = 90
local ANGLE_SPEED = 60 -- degrees a second with a key held
local NUDGE = 5 -- degrees a click on < or >
local TURN_CLICK_SECONDS = 0.4
local CLOSE_DELAY = 1.2
local ACK_FRAMES = 240 -- frames to wait for the pack to answer a begin
local WINDOW_WIDTH = 420

local function clamp(x, lo, hi)
  if x < lo then return lo elseif x > hi then return hi end
  return x
end

function M.create(env)
  local ui, lb = env.ui, env.lb
  local self = {}

  local title = ui.Value('')
  local status = ui.Value('')
  local angleText = ui.Value('')
  local picksText = ui.Value('')
  local turnValue = ui.Value(0)
  local strainValue = ui.Value(0)
  local doc = nil
  local isOpen = false

  local placement, level, name = nil, 0, ''
  local angle = 0
  local keys = { left = false, right = false, turn = false }
  local turnTimer = 0
  local waiting = false
  local waitFrames = 0
  local closeIn = nil
  local closed = false -- env.close was called: the host script takes the window down
  local placementHex = ''
  local serialAtBegin = nil

  local function say(text) status:set(text) end

  local function refreshTexts()
    angleText:set(string.format('Pick angle %+d', math.floor(angle + 0.5)))
    picksText:set(string.format('Lockpicks %d', lb.picks()))
    turnValue:set(lb.lock_progress())
    strainValue:set(lb.lock_strain())
  end

  local function start()
    waiting = true
    waitFrames = 0
    turnTimer = 0
    serialAtBegin = lb.lock_serial and lb.lock_serial() or nil
    env.begin(placement, level)
  end

  local function build()
    return ui.Document{ name = 'skyrim-lockpick', class = 'panel',
      style = { position = 'absolute', left = 24, top = 24, width = WINDOW_WIDTH },
      build = function()
        return ui.Column{
          ui.Text{ text = title, class = 'title' },
          ui.Text{ text = status, class = 'info' },
          ui.Text{ text = angleText, class = 'info' },
          ui.Text{ text = 'Lock', class = 'info' },
          ui.ProgressBar{ value = turnValue, max = 1, style = { width = WINDOW_WIDTH - 24 } },
          ui.Text{ text = 'Strain on the pick', class = 'info' },
          ui.ProgressBar{ value = strainValue, max = 1, style = { width = WINDOW_WIDTH - 24 } },
          ui.Text{ text = picksText, class = 'info' },
          ui.Row{
            ui.Button{ text = '<', class = 'btn', onClick = function() self.nudge(-NUDGE) end },
            ui.Button{ text = '>', class = 'btn', onClick = function() self.nudge(NUDGE) end },
            ui.Button{ text = 'Turn', class = 'btn', onClick = function() turnTimer = TURN_CLICK_SECONDS end },
            ui.Button{ text = 'Cancel', class = 'btn', onClick = function() self.cancel() end },
          },
          ui.Text{ text = 'A / D turn the pick, Space turns the lock, Esc leaves', class = 'info' },
        }
      end }
  end

  --- Opens the window for a lock and asks for the first attempt.
  function self.open(lockPlacement, lockLevel, lockName)
    self.dismiss()
    isOpen = true
    placement, level, name = lockPlacement, tonumber(lockLevel) or 0, lockName or ''
    placementHex = string.format('%x', tonumber(placement, 16) or 0)
    angle = 0
    keys.left, keys.right, keys.turn = false, false, false
    closeIn = nil
    closed = false
    title:set(name ~= '' and name or 'Locked')
    say('Starting...')
    refreshTexts()
    doc = build()
    start()
  end

  --- Takes the window down (the mode was left). Safe when nothing is open.
  function self.dismiss()
    if isOpen and lb.lock_state() == 'picking' then env.cancel() end
    if doc then doc:destroy() end
    doc = nil
    isOpen = false
    waiting = false
    closeIn = nil
  end

  function self.isOpen() return isOpen end
  function self.angle() return angle end
  function self.waiting() return waiting end

  function self.nudge(delta)
    angle = clamp(angle + delta, -ANGLE_LIMIT, ANGLE_LIMIT)
    refreshTexts()
  end

  --- Key state: which is 'left', 'right' or 'turn'.
  function self.setKey(which, down)
    if keys[which] ~= nil then keys[which] = down == true end
  end

  function self.cancel()
    if isOpen and lb.lock_state() == 'picking' then env.cancel() end
    closed = true
    env.close()
  end

  --- One call a frame with the frame's duration in seconds.
  function self.update(dt)
    if not isOpen or closed then return end
    dt = tonumber(dt) or 0
    local s = lb.lock_state()
    if closeIn ~= nil then
      closeIn = closeIn - dt
      if closeIn <= 0 then
        closeIn = nil
        closed = true
        env.close()
      end
      return
    end
    if waiting then
      waitFrames = waitFrames + 1
      local moved = serialAtBegin == nil or lb.lock_serial() ~= serialAtBegin
      local answered = moved and (s == 'refused' or (s == 'picking' and lb.lock_placement() == placementHex))
      if not answered then
        if waitFrames > ACK_FRAMES then
          waiting = false
          say('The lock did not answer.')
          closeIn = CLOSE_DELAY
        end
        return
      end
      waiting = false
    end
    if s == 'picking' then
      local dir = (keys.right and 1 or 0) - (keys.left and 1 or 0)
      if dir ~= 0 then angle = clamp(angle + dir * ANGLE_SPEED * dt, -ANGLE_LIMIT, ANGLE_LIMIT) end
      local turning = keys.turn or turnTimer > 0
      turnTimer = math.max(0, turnTimer - dt)
      say(turning and 'Turning...' or 'Find the sweet spot')
      env.input(angle, turning, dt * 1000)
    elseif s == 'opened' then
      say('Unlocked')
      closeIn = CLOSE_DELAY
    elseif s == 'broke' then
      if lb.picks() > 0 then
        say('The pick broke. A new pick.')
        start()
      else
        say('The pick broke. You have no more lockpicks.')
        closeIn = CLOSE_DELAY
      end
    elseif s == 'refused' then
      say(lb.lock_reason())
      closeIn = CLOSE_DELAY
    elseif s == 'cancelled' then
      closed = true
      env.close()
    end
    refreshTexts()
  end

  return self
end

return M
