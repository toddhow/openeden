-- The Skyrim SE skills and perks window (task 0200), written with `openeden.ui` (decision 0015).
--
-- The window logic only; it talks to what it is handed, so it runs over the headless UI backend in tests:
--   env.ui     the `openeden.ui` library
--   env.prog   the `core.skyrimse.progression` package (Read functions; the pack owns the state, this only draws it)
--   env.levelUp(name)   asks for a level up on "health", "magicka" or "stamina" (a global script runs the Mutate call)
--   env.buyPerk(id)     asks to buy the next rank of a perk
--   env.close()         leaves the interface mode (the host script's job)
-- Left: the level line and the 18 skills (a click selects one). Right: the perk nodes of the selected skill in the
-- tree's order (row, then column); a click on a node buys its next rank when the player may. Perk ids are 8 hex digits.
-- The window re-reads the pack only when `prog.revision()` moved or the selection changed.

local M = {}

local SKILL_ROW_HEIGHT = 26
local SKILL_LIST_HEIGHT = 18 * SKILL_ROW_HEIGHT
local SKILL_WIDTH = 210
local PERK_WIDTH = 480
local PERK_ROW_HEIGHT = 30
local PERK_LIST_HEIGHT = 400
local INDENT_PER_COLUMN = 14

function M.create(env)
  local ui, prog = env.ui, env.prog
  local self = {}

  local skillRows = ui.Value({})
  local perkRows = ui.Value({})
  local levelText = ui.Value('')
  local xpText = ui.Value('')
  local pointsText = ui.Value('')
  local statusText = ui.Value('')
  local skillTitle = ui.Value('')
  local readyText = ui.Value('')
  local levelUpVisible = ui.Value(false)
  local doc = nil
  local isOpen = false
  local selected = 0
  local lastRevision = nil

  local function refresh()
    lastRevision = prog.revision()
    levelText:set(string.format('Level %d', prog.level()))
    xpText:set(string.format('Experience %d / %d', prog.level_xp(), prog.level_need()))
    pointsText:set(string.format('Perk points %d', prog.perk_points()))
    statusText:set(prog.last_result())
    local ready = prog.level_up_ready()
    levelUpVisible:set(ready)
    readyText:set(ready and 'You can level up:' or '')
    local rows = {}
    for slot = 0, prog.skill_count() - 1 do
      local mark = slot == selected and '> ' or '  '
      rows[#rows + 1] = {
        slot = slot,
        key = string.format('%d:%d:%d', slot, prog.skill_level(slot), slot == selected and 1 or 0),
        label = string.format('%s%s  %d', mark, prog.skill_name(slot), prog.skill_level(slot)),
      }
    end
    skillRows:set(rows)
    skillTitle:set(string.format('%s  %d  (%d / %d)', prog.skill_name(selected), prog.skill_level(selected),
      prog.skill_xp(selected), prog.skill_need(selected)))
    local perks = {}
    for _, id in ipairs(prog.perks(selected)) do
      local rank, ranks = prog.perk_rank(id), prog.perk_ranks(id)
      local can = prog.perk_can_buy(id)
      local state
      if rank >= ranks and ranks > 0 then state = 'owned'
      elseif can then state = 'buy'
      else state = prog.perk_reason(id) end
      perks[#perks + 1] = {
        id = id,
        row = prog.perk_row(id),
        col = prog.perk_col(id),
        name = prog.perk_name(id),
        rank = rank,
        ranks = ranks,
        can = can,
        state = state,
      }
    end
    table.sort(perks, function(a, b)
      if a.row ~= b.row then return a.row < b.row end
      if a.col ~= b.col then return a.col < b.col end
      return a.id < b.id
    end)
    for _, p in ipairs(perks) do
      p.key = string.format('%s:%d:%s', p.id, p.rank, p.can and 'y' or 'n')
      p.label = string.format('%s%s  %d/%d', string.rep(' ', math.min(p.col, 8)), p.name, p.rank, p.ranks)
    end
    perkRows:set(perks)
  end

  local function build()
    return ui.Document{ name = 'skyrim-skills', class = 'panel',
      style = { position = 'absolute', left = 24, top = 24, width = SKILL_WIDTH + PERK_WIDTH + 36 },
      build = function()
        return ui.Column{
          ui.Text{ text = 'Skills', class = 'title' },
          ui.Row{
            ui.Text{ text = levelText, class = 'info', style = { width = 120 } },
            ui.Text{ text = xpText, class = 'info', style = { width = 200 } },
            ui.Text{ text = pointsText, class = 'info', style = { width = 160 } },
            ui.Text{ text = statusText, class = 'info' },
          },
          ui.Row{
            ui.Text{ text = readyText, class = 'info', style = { width = 150 } },
            ui.Button{ text = 'Health +10', class = 'btn', visible = levelUpVisible,
              onClick = function() env.levelUp('health') end },
            ui.Button{ text = 'Magicka +10', class = 'btn', visible = levelUpVisible,
              onClick = function() env.levelUp('magicka') end },
            ui.Button{ text = 'Stamina +10', class = 'btn', visible = levelUpVisible,
              onClick = function() env.levelUp('stamina') end },
          },
          ui.Row{
            ui.List{
              items = skillRows,
              key = function(r) return r.key end,
              rowHeight = SKILL_ROW_HEIGHT,
              height = SKILL_LIST_HEIGHT,
              width = SKILL_WIDTH,
              render = function(r)
                return ui.Button{ text = r.label, class = 'quest-name', style = { width = SKILL_WIDTH - 12 },
                  onClick = function() self.select(r.slot) end }
              end,
            },
            ui.Column{
              style = { width = PERK_WIDTH, ['margin-left'] = 12 },
              ui.Text{ text = skillTitle, class = 'title' },
              ui.List{
                items = perkRows,
                key = function(r) return r.key end,
                rowHeight = PERK_ROW_HEIGHT,
                height = PERK_LIST_HEIGHT,
                width = PERK_WIDTH,
                render = function(r)
                  return ui.Row{
                    ui.Button{ text = r.label, class = 'item-name', style = { width = 250 },
                      onClick = function() env.buyPerk(r.id) end },
                    ui.Text{ text = r.state, class = 'item-detail', style = { width = 190 } },
                  }
                end,
              },
            },
          },
          ui.Row{
            ui.Button{ text = 'Close', class = 'btn', onClick = function() env.close() end },
          },
        }
      end }
  end

  function self.select(slot)
    selected = slot
    refresh()
  end

  --- Opens the window (a second call rebuilds it).
  function self.open()
    self.dismiss()
    isOpen = true
    selected = 0
    refresh()
    doc = build()
  end

  --- Takes the window down (the mode was left). Safe when nothing is open.
  function self.dismiss()
    if doc then doc:destroy() end
    doc = nil
    isOpen = false
    lastRevision = nil
  end

  function self.isOpen() return isOpen end
  function self.selected() return selected end

  --- One call a frame: re-reads the pack when its revision moved.
  function self.update()
    if not isOpen then return end
    if prog.revision() ~= lastRevision then refresh() end
  end

  return self
end

return M
