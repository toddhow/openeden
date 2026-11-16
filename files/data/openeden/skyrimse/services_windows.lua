-- The Skyrim SE dialogue service windows (task 0219, milestone M-S6), written with `openeden.ui` (decision 0015):
-- training, renting a room and gifts. Each is one small panel; the pack owns every number and this module only draws
-- them. It holds the whole window logic and talks only to what it is handed, so it runs over the headless UI backend in
-- tests:
--   env.ui        the `openeden.ui` library
--   env.inv       the `core.skyrimse.inventory` package (names, counts, the revision), for the gift window
--   env.services  the `core.skyrimse.services` package (state, the training and rent numbers, the last result)
--   env.train(), env.rent()   ask the pack (a global script runs the Mutate call)
--   env.give(owner, item, count)   moves items from the player to the NPC's inventory owner (a global script)
--   env.close()   ends the service and leaves the interface mode (the host script's job)
-- Ids are text. Rows of the gift list are keyed by item and count, so a changed row is a new row (a List row is built
-- once).

local M = {}

local ROW_HEIGHT = 22
local LIST_HEIGHT = 420
local WINDOW_WIDTH = 400
local NAME_WIDTH = 230
local DETAIL_WIDTH = 60
local BUTTON_ALL_WIDTH = 40

local REASONS = {
  gold = 'You do not have enough gold.',
  skill = 'This trainer cannot teach you more.',
  limit = 'You cannot train any more at this level.',
  not_ready = 'Not ready yet.',
}

function M.create(env)
  local ui, inv, services = env.ui, env.inv, env.services
  local self = {}

  local title = ui.Value('')
  local line1 = ui.Value('')
  local line2 = ui.Value('')
  local line3 = ui.Value('')
  local line4 = ui.Value('')
  local status = ui.Value('')
  local giftRows = ui.Value({})
  local docs = {}
  local isOpen = false
  local kind = nil -- 'training', 'rent' or 'gift' once the session is ready
  local lastRevision, lastInvRevision, lastState = nil, nil, nil

  local function giftList()
    local out = {}
    local gold = inv.gold_item()
    for _, id in ipairs(inv.items('player')) do
      if id ~= gold then
        local count = inv.count('player', id)
        if count > 0 then
          out[#out + 1] = { id = id, key = id .. ':' .. count, count = count, name = inv.name(id) }
        end
      end
    end
    return out
  end

  local function refresh()
    lastRevision, lastState = services.revision(), services.state()
    lastInvRevision = inv.revision()
    local state = lastState
    title:set(services.name())
    if state == 'training' then
      kind = 'training'
      line1:set('Skill: ' .. services.skill_name())
      line2:set(string.format('Level %d (this trainer teaches up to %d)', services.skill_level(), services.max_level()))
      line3:set(string.format('Cost %d gold   Your gold %d', services.cost(), services.gold()))
      line4:set(string.format('Trainings left at this level: %d', services.trains_left()))
      local reason = services.can_train()
      local last = services.last_result()
      if last ~= '' then
        status:set(last)
      else
        status:set(REASONS[reason] or '')
      end
    elseif state == 'rent' then
      kind = 'rent'
      line1:set('Rent a room')
      line2:set(string.format('%d gold for %d hours', services.rent_cost(), services.rent_hours()))
      line3:set(string.format('Your gold %d', services.gold()))
      local rs = services.rent_state()
      if rs == 'active' then
        line4:set(string.format('Your room is rented: %.1f hours left', services.rent_hours_left()))
      elseif rs == 'no_gold' then
        line4:set('You do not have enough gold.')
      else
        line4:set('')
      end
      status:set(services.last_result())
    elseif state == 'gift' then
      kind = 'gift'
      giftRows:set(giftList())
      line1:set(string.format('Your gold %d', inv.count('player', inv.gold_item())))
      status:set('')
    elseif state == 'refused' then
      kind = nil
      line1:set('')
      line2:set('')
      line3:set('')
      line4:set('')
      status:set(services.reason())
    else
      kind = nil
      status:set('Opening...')
    end
  end

  local function panel(name, childrenFn)
    return ui.Document{ name = name, class = 'panel',
      style = { position = 'absolute', left = 24, top = 24, width = WINDOW_WIDTH },
      build = function() return ui.Column(childrenFn()) end }
  end

  local function buildTraining()
    return panel('skyrim-training', function() return {
      ui.Text{ text = title, class = 'title' },
      ui.Text{ text = line1, class = 'info' },
      ui.Text{ text = line2, class = 'info' },
      ui.Text{ text = line3, class = 'info' },
      ui.Text{ text = line4, class = 'info' },
      ui.Text{ text = status, class = 'info' },
      ui.Row{
        ui.Button{ text = 'Train', class = 'btn', onClick = function() env.train() end },
        ui.Button{ text = 'Close', class = 'btn', onClick = function() env.close() end },
      },
    } end)
  end

  local function buildRent()
    return panel('skyrim-rent', function() return {
      ui.Text{ text = title, class = 'title' },
      ui.Text{ text = line1, class = 'info' },
      ui.Text{ text = line2, class = 'info' },
      ui.Text{ text = line3, class = 'info' },
      ui.Text{ text = line4, class = 'info' },
      ui.Text{ text = status, class = 'info' },
      ui.Row{
        ui.Button{ text = 'Rent', class = 'btn', onClick = function() env.rent() end },
        ui.Button{ text = 'Close', class = 'btn', onClick = function() env.close() end },
      },
    } end)
  end

  local function buildGift()
    return panel('skyrim-gift', function() return {
      ui.Text{ text = title, class = 'title' },
      ui.Text{ text = 'Give items', class = 'info' },
      ui.Text{ text = line1, class = 'info' },
      ui.List{
        items = giftRows,
        key = function(r) return r.key end,
        rowHeight = ROW_HEIGHT,
        height = LIST_HEIGHT,
        width = WINDOW_WIDTH - 24,
        render = function(r)
          local label = r.count > 1 and string.format('%s  x%d', r.name, r.count) or r.name
          return ui.Row{
            ui.Button{ text = label, class = 'item-name', style = { width = NAME_WIDTH },
              onClick = function() env.give(services.owner(), r.id, 1) end },
            ui.Text{ text = '', class = 'item-detail', style = { width = DETAIL_WIDTH } },
            ui.Button{ text = 'All', class = 'item-all', style = { width = BUTTON_ALL_WIDTH },
              onClick = function() env.give(services.owner(), r.id, r.count) end },
          }
        end,
      },
      ui.Row{ ui.Button{ text = 'Close', class = 'btn', onClick = function() env.close() end } },
    } end)
  end

  local function build()
    for _, d in ipairs(docs) do d:destroy() end
    docs = {}
    if kind == 'training' then
      docs[1] = buildTraining()
    elseif kind == 'rent' then
      docs[1] = buildRent()
    elseif kind == 'gift' then
      docs[1] = buildGift()
    else
      docs[1] = panel('skyrim-service', function() return {
        ui.Text{ text = title, class = 'title' },
        ui.Text{ text = status, class = 'info' },
        ui.Row{ ui.Button{ text = 'Close', class = 'btn', onClick = function() env.close() end } },
      } end)
    end
  end

  --- Opens the window for the session the host script started (the pack's state decides which panel).
  function self.open()
    self.dismiss()
    isOpen = true
    refresh()
    build()
    self.shown = kind or 'waiting'
  end

  --- Takes the windows down (the mode was left). Safe when nothing is open.
  function self.dismiss()
    for _, d in ipairs(docs) do d:destroy() end
    docs = {}
    isOpen = false
    kind = nil
    self.shown = nil
    lastRevision, lastInvRevision, lastState = nil, nil, nil
  end

  function self.isOpen() return isOpen end

  --- One call a frame: re-reads the numbers when the pack's revision or the state moved; a state that settled (pending
  --- to ready or refused) builds the right panel. A frame where nothing changed makes three cheap reads.
  function self.update()
    if not isOpen then return end
    local state = services.state()
    if services.revision() == lastRevision and state == lastState
        and (state ~= 'gift' or inv.revision() == lastInvRevision) then
      return
    end
    local before = kind
    refresh()
    if kind ~= before or (kind == nil and self.shown ~= 'waiting') then
      build()
      self.shown = kind or 'waiting'
    end
  end

  return self
end

return M
