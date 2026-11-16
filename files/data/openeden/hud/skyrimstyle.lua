-- The Skyrim-style combat HUD (task 0244; decision 0015; the numbers come from the pack, game_packs/skyrimse/include/
-- game_skyrimse/hud_style.hpp). Original art in the spirit of the TrueHUD mod's layout, nothing copied: the player's
-- three bars at the bottom left beside an emblem with the level, a wide bar at the top centre for a boss (or the enemy
-- fought last), and small bars over the heads of nearby engaged actors.
--
--   local hud = require('openeden.hud.skyrimstyle').create(require('openeden.ui'), require('core.skyrimse.hudstyle'), env)
--   hud:update()   -- once a frame; builds the documents the first frame combat runs, then follows the numbers
--
-- `style` is the script package the pack declares: `active()`, `player()` (health, maximum, magicka, maximum, stamina,
-- maximum, level), `boss()` (shown, health, maximum, level, is a boss, dead), `boss_name()`, `bars()` (13 numbers an
-- actor: id, x, y, z, health, maximum, magicka, maximum, stamina, maximum, level, ally, flags) and `bar_names()`.
-- `env` is what only the script can know: `now()` (real seconds), `screen()` (width and height in layout units) and
-- `project(x, y, z)`, which returns where a world point is on the screen as fractions of its size, or nil when it is
-- behind the camera or off the screen.
--
-- No clip masks, filters or gradients (our RmlUi renderer has none, decision 0015): a frame is nested boxes. Every
-- time and size below is ours and named. A frame in which nothing visible changed makes no protocol call: widths are
-- whole pixels and positions whole pixels.
local M = {}

-- sizes in layout units (dp)
M.PLAYER_WIDTH = 260
M.PLAYER_HEIGHT = 16
M.PLAYER_SMALL_HEIGHT = 12
M.BOSS_WIDTH = 420
M.BOSS_HEIGHT = 14
M.ACTOR_WIDTH = 90
M.ACTOR_HEIGHT = 6
M.ACTOR_SMALL_HEIGHT = 3
M.CAP = 4 -- the end caps of a frame stick out of the bar by this much on both sides
M.ACTOR_SLOTS = 8 -- as many as the pack's kMaxActorBars
M.BOSS_TOP = 48
M.ACTOR_ABOVE = 26 -- pixels between the point and the top of the bar's name line
-- the damage trail: the lost part of a bar stays pale for TRAIL_DELAY seconds after a hit, then drains at TRAIL_RATE
-- (a share of the whole bar a second)
M.TRAIL_DELAY = 0.5
M.TRAIL_RATE = 0.4

local FRAME = 'rgb(168, 154, 120)'
local BACKING = 'rgba(0, 0, 0, 150)' -- alpha is 0 to 255 in RmlUi
local TRAIL = 'rgb(226, 216, 198)'
local COLOURS = {
  health = 'rgb(176, 42, 42)',
  magicka = 'rgb(46, 92, 190)',
  stamina = 'rgb(58, 160, 64)',
  boss = 'rgb(190, 52, 40)',
  foe = 'rgb(176, 42, 42)',
  ally = 'rgb(70, 170, 100)',
}
-- RmlUi's rgba() alpha is 0 to 255 (0.85 would be invisible): 217 is 85 percent.
M.CROSSHAIR = 'rgba(255, 255, 255, 217)'

local function clamp01(x)
  if x ~= x then return 0 end
  if x < 0 then return 0 elseif x > 1 then return 1 end
  return x
end

local function fraction(value, max)
  value, max = tonumber(value) or 0, tonumber(max) or 0
  if max <= 0 or value ~= value or max ~= max then return 0 end
  return clamp01(value / max)
end

local function px(frac, width)
  return string.format('%dpx', math.floor(clamp01(frac) * width + 0.5))
end

-- The trail of one bar: `trail` is the fraction the pale part reaches (never below the value). A drop holds it for
-- TRAIL_DELAY, then it drains; a heal takes it up with the fill at once.
function M.newTrail(value, now)
  value = clamp01(value)
  return { value = value, trail = value, holdUntil = -1, last = now or 0 }
end

function M.stepTrail(t, value, now)
  value = clamp01(value)
  if value >= t.trail then
    t.trail = value
  else
    if value < t.value then t.holdUntil = now + M.TRAIL_DELAY end
    if now > t.holdUntil then
      local from = math.max(t.last, t.holdUntil)
      local drained = M.TRAIL_RATE * math.max(0, now - from)
      t.trail = math.max(value, t.trail - drained)
    end
  end
  t.value = value
  t.last = now
  return t.trail
end

function M.create(ui, style, env)
  local self = { built = false, docs = {} }

  -- reactive state: strings and booleans only, so an unchanged frame sets nothing
  local kinds = { 'health', 'magicka', 'stamina' }
  local pfill, ptrail, pshown = {}, {}, {}
  for _, k in ipairs(kinds) do
    pfill[k] = ui.Value('0px')
    ptrail[k] = ui.Value('0px')
    pshown[k] = ui.Value(k ~= 'health')
  end
  local level = ui.Value('1')
  local bossShown, bossIsBoss = ui.Value(false), ui.Value(false)
  local bossName, bossLevel = ui.Value(''), ui.Value('')
  local bossFill, bossTrail = ui.Value('0px'), ui.Value('0px')

  local slots = {}
  for i = 1, M.ACTOR_SLOTS do
    slots[i] = {
      shown = ui.Value(false), name = ui.Value(''), level = ui.Value(''),
      left = ui.Value('0px'), top = ui.Value('0px'),
      fill = ui.Value('0px'), trail = ui.Value('0px'), colour = ui.Value(COLOURS.foe),
      magickaShown = ui.Value(false), staminaShown = ui.Value(false),
      magickaFill = ui.Value('0px'), staminaFill = ui.Value('0px'),
      id = nil, trailState = nil,
    }
  end
  local ptrails = {}
  local bossTrailState

  -- a framed bar: dark backing, a pale trail, the fill, an edge above and below and a cap at each end
  local function framedBar(o)
    local w, h, cap = o.width, o.height, M.CAP
    local function edge(y)
      return ui.Frame{ class = 'style-edge',
        style = { position = 'absolute', left = cap, top = y, width = w, height = 2, ['background-color'] = FRAME } }
    end
    local function capBox(x)
      return ui.Frame{ class = 'style-cap',
        style = { position = 'absolute', left = x, top = 0, width = cap, height = h + 8, ['background-color'] = FRAME } }
    end
    return ui.Stack{
      class = o.class, visible = o.visible,
      style = { width = w + 2 * cap, height = h + 8, ['margin-top'] = o.gap or 0 },
      ui.Frame{ class = 'style-backing',
        style = { position = 'absolute', left = cap, top = 2, width = w, height = h + 4, ['background-color'] = BACKING } },
      ui.Frame{ class = o.trailClass,
        style = { position = 'absolute', left = cap, top = 4, width = o.trail, height = h, ['background-color'] = TRAIL } },
      ui.Frame{ class = o.fillClass,
        style = { position = 'absolute', left = cap, top = 4, width = o.fill, height = h,
          ['background-color'] = o.colour } },
      edge(0), edge(h + 6), capBox(0), capBox(w + cap),
    }
  end

  local function build()
    self.built = true
    self.docs[#self.docs + 1] = ui.Hud{
      name = 'style-player',
      style = { position = 'absolute', left = 24, bottom = 24 },
      build = function()
        local bars = {}
        for _, k in ipairs(kinds) do
          bars[#bars + 1] = framedBar{
            class = 'style-bar style-bar-' .. k, visible = pshown[k],
            width = M.PLAYER_WIDTH, height = k == 'health' and M.PLAYER_HEIGHT or M.PLAYER_SMALL_HEIGHT, gap = 2,
            fill = pfill[k], trail = ptrail[k], colour = COLOURS[k],
            fillClass = 'style-fill style-fill-' .. k, trailClass = 'style-trail style-trail-' .. k }
        end
        return ui.Row{
          style = { ['align-items'] = 'flex-end' },
          -- the emblem: a framed square with the level in it
          ui.Stack{ class = 'style-emblem',
            style = { width = 44, height = 44, ['margin-right'] = 8, ['border-width'] = 2, ['border-color'] = FRAME,
              ['background-color'] = BACKING },
            ui.Text{ class = 'style-emblem-level', text = level,
              style = { ['text-align'] = 'center', ['margin-top'] = 12 } } },
          ui.Column(bars),
        }
      end }
    self.docs[#self.docs + 1] = ui.Hud{
      name = 'style-boss',
      style = { position = 'absolute', left = '50%', top = M.BOSS_TOP, width = M.BOSS_WIDTH + 2 * M.CAP,
        ['margin-left'] = -math.floor(M.BOSS_WIDTH / 2) - M.CAP },
      build = function()
        return ui.Column{
          class = ui.Computed(function()
            return bossIsBoss:get() and 'style-boss-box' or 'style-boss-box style-boss-enemy'
          end),
          visible = bossShown,
          ui.Row{
            style = { ['margin-bottom'] = 2 },
            ui.Text{ class = 'style-boss-name', text = bossName, style = { ['margin-right'] = 10 } },
            ui.Text{ class = 'style-boss-level', text = bossLevel },
          },
          framedBar{ class = 'style-bar-boss', width = M.BOSS_WIDTH, height = M.BOSS_HEIGHT,
            fill = bossFill, trail = bossTrail, colour = COLOURS.boss,
            fillClass = 'style-fill style-fill-boss', trailClass = 'style-trail style-trail-boss' },
        }
      end }
    for i = 1, M.ACTOR_SLOTS do
      local s = slots[i]
      self.docs[#self.docs + 1] = ui.Hud{
        name = 'style-actor-' .. i,
        style = { position = 'absolute', left = s.left, top = s.top, width = M.ACTOR_WIDTH + 2 * M.CAP },
        build = function()
          return ui.Column{
            class = 'style-actor-box style-actor-box-' .. i, visible = s.shown,
            ui.Row{
              ui.Text{ class = 'style-actor-name-' .. i, text = s.name, style = { ['margin-right'] = 6 } },
              ui.Text{ class = 'style-actor-level-' .. i, text = s.level },
            },
            framedBar{ class = 'style-actor-bar', width = M.ACTOR_WIDTH, height = M.ACTOR_HEIGHT,
              fill = s.fill, trail = s.trail, colour = s.colour,
              fillClass = 'style-fill style-actor-fill-' .. i, trailClass = 'style-trail style-actor-trail-' .. i },
            framedBar{ class = 'style-actor-magicka-' .. i, visible = s.magickaShown, gap = 1,
              width = M.ACTOR_WIDTH, height = M.ACTOR_SMALL_HEIGHT, fill = s.magickaFill, trail = ui.Value('0px'),
              colour = COLOURS.magicka, fillClass = 'style-fill', trailClass = 'style-trail' },
            framedBar{ class = 'style-actor-stamina-' .. i, visible = s.staminaShown, gap = 1,
              width = M.ACTOR_WIDTH, height = M.ACTOR_SMALL_HEIGHT, fill = s.staminaFill, trail = ui.Value('0px'),
              colour = COLOURS.stamina, fillClass = 'style-fill', trailClass = 'style-trail' },
          }
        end }
    end
    self.docs[#self.docs + 1] = ui.Hud{
      name = 'style-crosshair',
      style = { position = 'absolute', left = '50%', top = '50%', width = 0, height = 0 },
      build = function()
        return ui.Stack{
          class = 'style-crosshair',
          ui.Frame{ style = { position = 'absolute', left = -9, top = -1, width = 18, height = 2,
            ['background-color'] = M.CROSSHAIR } },
          ui.Frame{ style = { position = 'absolute', left = -1, top = -9, width = 2, height = 18,
            ['background-color'] = M.CROSSHAIR } },
        }
      end }
  end

  local function numbers(t, i)
    return tonumber(t[i]) or 0
  end

  local function updatePlayer(now)
    local v = style.player()
    if type(v) ~= 'table' then v = {} end
    local pairsOf = { health = { 1, 2 }, magicka = { 3, 4 }, stamina = { 5, 6 } }
    for _, k in ipairs(kinds) do
      local value, max = pairsOf[k][1], pairsOf[k][2]
      local f = fraction(v[value], v[max])
      local t = ptrails[k]
      if t == nil then t = M.newTrail(f, now); ptrails[k] = t end
      pfill[k]:set(px(f, M.PLAYER_WIDTH))
      ptrail[k]:set(px(M.stepTrail(t, f, now), M.PLAYER_WIDTH))
      pshown[k]:set(numbers(v, max) > 0)
    end
    level:set(string.format('%d', math.floor(numbers(v, 7))))
  end

  local function updateBoss(now)
    local v = style.boss()
    if type(v) ~= 'table' then v = {} end
    local shown = numbers(v, 1) ~= 0
    bossShown:set(shown)
    if not shown then
      bossTrailState = nil
      return
    end
    bossIsBoss:set(numbers(v, 5) ~= 0)
    bossName:set(tostring(style.boss_name() or ''))
    bossLevel:set('Lv ' .. string.format('%d', math.floor(numbers(v, 4))))
    local f = fraction(v[2], v[3])
    if bossTrailState == nil then bossTrailState = M.newTrail(f, now) end
    bossFill:set(px(f, M.BOSS_WIDTH))
    bossTrail:set(px(M.stepTrail(bossTrailState, f, now), M.BOSS_WIDTH))
  end

  local function updateActors(now)
    local flat = style.bars()
    local names = style.bar_names()
    if type(flat) ~= 'table' then flat = {} end
    if type(names) ~= 'table' then names = {} end
    local W, H = env.screen()
    local count = math.floor(#flat / 13)
    -- who is wanted this frame, projected
    local wanted, byId = {}, {}
    for n = 1, count do
      local o = (n - 1) * 13
      local id = flat[o + 1]
      local nx, ny = env.project(flat[o + 2], flat[o + 3], flat[o + 4])
      if nx ~= nil then
        wanted[#wanted + 1] = { id = id, o = o, nx = nx, ny = ny, name = names[n] or '' }
        byId[id] = wanted[#wanted]
      end
    end
    -- a slot whose actor is no longer wanted is freed
    for _, s in ipairs(slots) do
      if s.id ~= nil and byId[s.id] == nil then
        s.id = nil
        s.trailState = nil
        s.shown:set(false)
      end
    end
    local claimed = {}
    for _, s in ipairs(slots) do
      if s.id ~= nil then claimed[s.id] = s end
    end
    for _, w in ipairs(wanted) do
      local slot = claimed[w.id]
      if slot == nil then
        for _, s in ipairs(slots) do
          if s.id == nil then slot = s; break end
        end
        if slot == nil then break end -- more bars than slots: the rest are not drawn
        slot.id = w.id
        claimed[w.id] = slot
        slot.trailState = nil
      end
      local o = w.o
      local f = fraction(flat[o + 5], flat[o + 6])
      if slot.trailState == nil then slot.trailState = M.newTrail(f, now) end
      slot.shown:set(true)
      slot.name:set(tostring(w.name))
      slot.level:set(string.format('%d', math.floor(numbers(flat, o + 11))))
      slot.left:set(string.format('%dpx', math.floor(w.nx * W - M.ACTOR_WIDTH / 2 - M.CAP + 0.5)))
      slot.top:set(string.format('%dpx', math.floor(w.ny * H - M.ACTOR_ABOVE + 0.5)))
      slot.fill:set(px(f, M.ACTOR_WIDTH))
      slot.trail:set(px(M.stepTrail(slot.trailState, f, now), M.ACTOR_WIDTH))
      slot.colour:set(numbers(flat, o + 12) ~= 0 and COLOURS.ally or COLOURS.foe)
      local flags = math.floor(numbers(flat, o + 13))
      slot.magickaShown:set(flags % 2 == 1)
      slot.staminaShown:set(math.floor(flags / 2) % 2 == 1)
      slot.magickaFill:set(px(fraction(flat[o + 7], flat[o + 8]), M.ACTOR_WIDTH))
      slot.staminaFill:set(px(fraction(flat[o + 9], flat[o + 10]), M.ACTOR_WIDTH))
    end
  end

  function self:update()
    if not self.built then
      if not style.active() then return end
      build()
    end
    local now = env.now()
    updatePlayer(now)
    updateBoss(now)
    updateActors(now)
  end

  function self:destroy()
    for _, doc in ipairs(self.docs) do doc:destroy() end
    self.docs = {}
    self.built = false
  end

  return self
end

return M
