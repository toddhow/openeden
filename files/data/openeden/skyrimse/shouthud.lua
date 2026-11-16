-- The Skyrim SE shout cooldown indicator (task 0205, milestone M-S7), written with `openeden.ui` (decision 0015): the
-- equipped shout's name, a bar that empties as the recovery runs down and the seconds left, or "ready". Next to the
-- combat HUD (openeden/hud/skyrimcombat.lua) and the compass. Nothing is shown while no shout is equipped.
--
-- This module holds the whole logic and talks only to what it is handed, so it runs over the headless UI backend in tests:
--   env.ui       the `openeden.ui` library
--   env.shouts   the `core.skyrimse.shouts` package (Read functions)
local M = {}

local WIDTH = 220
local BAR_HEIGHT = 8
local INNER = WIDTH - 4 -- the bar's room inside its 2 px border
local READY_COLOR = 'rgb(120, 190, 235)'
local COOLING_COLOR = 'rgb(225, 130, 50)'

--- The share of the recovery still to run, 0 to 1. Pure.
function M.fraction(cooldown, total)
  if type(cooldown) ~= 'number' or type(total) ~= 'number' or cooldown ~= cooldown or total ~= total then return 0 end
  if total <= 0 or cooldown <= 0 then return 0 end
  return math.min(1, cooldown / total)
end

--- The bar's inner width in pixels for a share. Pure.
function M.barWidth(fraction)
  return math.floor(math.max(0, math.min(1, fraction)) * INNER + 0.5)
end

--- The line under the bar. Pure.
function M.label(name, cooldown)
  if type(cooldown) == 'number' and cooldown == cooldown and cooldown > 0 then
    return string.format('%s  %.1f s', name, cooldown)
  end
  return name .. '  ready'
end

M.WIDTH = WIDTH

function M.create(env)
  local ui, shouts = env.ui, env.shouts
  local self = {}
  local text = ui.Value('')
  local width = ui.Value('0px')
  local color = ui.Value(READY_COLOR)
  local doc = nil

  local function build()
    return ui.Hud{
      name = 'skyrim-shout',
      style = { position = 'absolute', left = '50%', bottom = 96, width = WIDTH, ['margin-left'] = -math.floor(WIDTH / 2) },
      build = function()
        return ui.Column{
          ui.Text{ text = text, class = 'info', style = { width = WIDTH, ['text-align'] = 'center' } },
          ui.Stack{
            style = { width = WIDTH, height = BAR_HEIGHT, ['background-color'] = 'rgba(0, 0, 0, 160)' },
            ui.Frame{ style = { position = 'absolute', left = 2, top = 2, width = width, height = BAR_HEIGHT - 4,
              ['background-color'] = color } },
          },
        }
      end }
  end

  --- Builds the indicator when a shout is equipped and moves what is on it; gone when none is. Reads the pack every
  --- frame (a handful of numbers); the elements only change when a value does.
  function self.update()
    if not shouts.ready() then return end
    local name = shouts.equipped_name()
    if name == '' then
      if doc then doc:destroy(); doc = nil end
      return
    end
    if not doc then doc = build() end
    local cooldown, total = shouts.cooldown(), shouts.cooldown_total()
    text:set(M.label(name, cooldown))
    width:set(string.format('%dpx', M.barWidth(M.fraction(cooldown, total))))
    color:set(cooldown > 0 and COOLING_COLOR or READY_COLOR)
  end

  function self.destroy()
    if doc then doc:destroy() end
    doc = nil
  end

  function self.isBuilt() return doc ~= nil end
  function self.text() return text:get() end
  function self.barPixels() return width:get() end

  return self
end

return M
