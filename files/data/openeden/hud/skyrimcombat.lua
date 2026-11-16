-- The Skyrim combat HUD (task 0180, stage S5-11 of research note 0056; decision 0015).
--
--   local hud = require('openeden.hud.skyrimcombat').create(require('openeden.ui'), require('core.skyrimse.combat'))
--   hud:update()   -- once a frame; builds the documents the first frame combat is running, then follows the numbers
--
-- `combat` is the script package the Skyrim pack declares (game_packs/skyrimse/include/game_skyrimse/hud.hpp):
-- `active()`, `hud()` (ten numbers: player health, maximum, magicka, maximum, stamina, maximum, enemy shown, enemy
-- health, maximum, enemy dead) and `enemy_name()`. Three small documents, by the rule of note 0045 13.5: the player's
-- bars, the enemy's bar and the crosshair. A bar is a fraction in half percents, so a frame in which nothing visible
-- changed makes no protocol call. The looks are inline and this file's own (bar colours are meaning, not theme): nothing
-- in the shipped style sheets or the core components is touched.
local M = {}

local BAR_WIDTH, BAR_HEIGHT = 220, 14
local ENEMY_WIDTH = 360
local TRACK = 'rgba(0, 0, 0, 140)' -- alpha is 0 to 255 in RmlUi: 140 is 55 percent
local COLOURS = {
  health = 'rgb(176, 42, 42)',
  magicka = 'rgb(42, 79, 176)',
  stamina = 'rgb(58, 154, 58)',
  enemy = 'rgb(176, 42, 42)',
}
-- RmlUi's rgba() alpha is 0 to 255 (0.85 would be invisible): 217 is 85 percent.
local CROSSHAIR = 'rgba(255, 255, 255, 217)'

-- value / maximum as a percent string in half-percent steps, clamped to 0..100; a missing or zero maximum is empty.
local function percent(value, max)
  value, max = tonumber(value) or 0, tonumber(max) or 0
  if max <= 0 or value ~= value or max ~= max then return '0%' end
  local f = value / max
  if f < 0 then f = 0 elseif f > 1 then f = 1 end
  return string.format('%g%%', math.floor(f * 200 + 0.5) / 2)
end

function M.create(ui, combat)
  local self = { built = false, docs = {} }

  -- reactive state: strings and booleans only, so an unchanged frame sets nothing
  local pct = { health = ui.Value('0%'), magicka = ui.Value('0%'), stamina = ui.Value('0%'), enemy = ui.Value('0%') }
  local shown = { health = ui.Value(false), magicka = ui.Value(true), stamina = ui.Value(true) }
  local enemyShown = ui.Value(false)
  local enemyName = ui.Value('')

  local function bar(kind)
    return ui.Frame{
      class = 'combat-bar combat-bar-' .. kind,
      visible = shown[kind],
      style = { width = BAR_WIDTH, height = BAR_HEIGHT, ['margin-top'] = 6, ['background-color'] = TRACK },
      ui.Frame{
        class = 'combat-fill combat-fill-' .. kind,
        style = { height = '100%', width = pct[kind], ['background-color'] = COLOURS[kind] },
      },
    }
  end

  local function build()
    self.built = true
    self.docs[#self.docs + 1] = ui.Hud{
      name = 'combat-player',
      style = { position = 'absolute', left = 24, bottom = 24, width = BAR_WIDTH },
      build = function()
        return ui.Column{ bar('health'), bar('magicka'), bar('stamina') }
      end }
    self.docs[#self.docs + 1] = ui.Hud{
      name = 'combat-enemy',
      style = { position = 'absolute', left = '50%', top = 28, width = ENEMY_WIDTH, ['margin-left'] = -math.floor(ENEMY_WIDTH / 2) },
      build = function()
        return ui.Column{
          class = 'combat-enemy-box',
          visible = enemyShown,
          ui.Text{ class = 'combat-enemy-name', text = enemyName,
            style = { ['text-align'] = 'center', ['margin-bottom'] = 4 } },
          ui.Frame{
            style = { width = '100%', height = BAR_HEIGHT, ['background-color'] = TRACK },
            ui.Frame{
              class = 'combat-fill combat-fill-enemy',
              style = { height = '100%', width = pct.enemy, ['background-color'] = COLOURS.enemy },
            },
          },
        }
      end }
    self.docs[#self.docs + 1] = ui.Hud{
      name = 'combat-crosshair',
      style = { position = 'absolute', left = '50%', top = '50%', width = 0, height = 0 },
      build = function()
        return ui.Stack{
          class = 'combat-crosshair',
          ui.Frame{ style = { position = 'absolute', left = -9, top = -1, width = 18, height = 2,
            ['background-color'] = CROSSHAIR } },
          ui.Frame{ style = { position = 'absolute', left = -1, top = -9, width = 2, height = 18,
            ['background-color'] = CROSSHAIR } },
        }
      end }
  end

  function self:update()
    if not self.built then
      if not combat.active() then return end
      build()
    end
    local v = combat.hud()
    if type(v) ~= 'table' then v = {} end
    pct.health:set(percent(v[1], v[2]))
    pct.magicka:set(percent(v[3], v[4]))
    pct.stamina:set(percent(v[5], v[6]))
    shown.health:set((tonumber(v[2]) or 0) > 0)
    shown.magicka:set((tonumber(v[4]) or 0) > 0)
    shown.stamina:set((tonumber(v[6]) or 0) > 0)
    local enemy = (tonumber(v[7]) or 0) ~= 0
    enemyShown:set(enemy)
    if enemy then
      pct.enemy:set(percent(v[8], v[9]))
      enemyName:set(tostring(combat.enemy_name() or ''))
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
