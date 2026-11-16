-- The Skyrim SE barter window (task 0189, milestone M-S6), written with `openeden.ui` (decision 0015).
--
-- Two columns: left the player's items the merchant will buy (with what it pays), right the merchant's stock (with what
-- the player pays). The module holds the whole window logic and talks only to what it is handed, so it runs over the
-- headless UI backend in tests:
--   env.ui        the `openeden.ui` library
--   env.inv       the `core.skyrimse.inventory` package (names, counts, the revision)
--   env.barter    the `core.skyrimse.barter` package (state, prices, gold, the last result)
--   env.buy(item, count), env.sell(item, count)   ask the pack to trade (a global script runs the Mutate call)
--   env.close()   ends the barter and leaves the interface mode (the host script's job)
-- Ids are text: an item is 8 hex digits. Rows are keyed by item, count and price, so a changed row is a new row (a List
-- row is built once).

local M = {}

local ROW_HEIGHT = 22
local LIST_HEIGHT = 420
local WINDOW_WIDTH = 400
local BUTTON_ALL_WIDTH = 40
local NAME_WIDTH = 210
local DETAIL_WIDTH = 70

local function rowLabel(r)
  local count = r.count > 1 and string.format('  x%d', r.count) or ''
  return string.format('%s%s', r.name, count)
end

function M.create(env)
  local ui, inv, barter = env.ui, env.inv, env.barter
  local self = {}

  local playerRows = ui.Value({})
  local vendorRows = ui.Value({})
  local title = ui.Value('')
  local playerGold = ui.Value('')
  local vendorGold = ui.Value('')
  local status = ui.Value('')
  local docs = {}
  local isOpen = false
  local lastRevision, lastTrades, lastState = nil, nil, nil

  local function rows(owner, priceOf, countOf)
    local out = {}
    local gold = inv.gold_item()
    for _, id in ipairs(inv.items(owner)) do
      if id ~= gold then
        local count = countOf(id)
        local price = priceOf(id)
        if count > 0 and price > 0 then
          out[#out + 1] = {
            id = id,
            key = id .. ':' .. count .. ':' .. price,
            count = count,
            price = price,
            name = inv.name(id),
          }
        end
      end
    end
    return out
  end

  local function refresh()
    lastRevision, lastTrades, lastState = inv.revision(), barter.trades(), barter.state()
    local state = lastState
    if state == 'ready' then
      playerRows:set(rows('player', barter.sell_price, barter.sellable))
      vendorRows:set(rows(barter.owner(), barter.buy_price, function(id) return inv.count(barter.owner(), id) end))
      playerGold:set(string.format('Your gold %d', inv.count('player', inv.gold_item())))
      vendorGold:set(string.format('Merchant gold %d', barter.vendor_gold()))
      status:set(barter.last_result())
    else
      playerRows:set({})
      vendorRows:set({})
      playerGold:set(string.format('Your gold %d', inv.count('player', inv.gold_item())))
      vendorGold:set('')
      if state == 'refused' then
        status:set(barter.reason())
      else
        status:set('Opening...')
      end
    end
  end

  local function makeList(valueRows, act, label)
    return ui.List{
      items = valueRows,
      key = function(r) return r.key end,
      rowHeight = ROW_HEIGHT,
      height = LIST_HEIGHT,
      width = WINDOW_WIDTH - 24,
      render = function(r)
        return ui.Row{
          ui.Button{ text = rowLabel(r), class = 'item-name', style = { width = NAME_WIDTH },
            onClick = function() act(r, 1) end },
          ui.Text{ text = string.format('%dg', r.price), class = 'item-detail', style = { width = DETAIL_WIDTH } },
          ui.Button{ text = label, class = 'item-all', style = { width = BUTTON_ALL_WIDTH },
            onClick = function() act(r, r.count) end },
        }
      end,
    }
  end

  local function buildPlayer()
    return ui.Document{ name = 'skyrim-barter-player', class = 'panel',
      style = { position = 'absolute', left = 24, top = 24, width = WINDOW_WIDTH },
      build = function()
        return ui.Column{
          ui.Text{ text = 'Your items', class = 'title' },
          ui.Text{ text = playerGold, class = 'info' },
          makeList(playerRows, function(r, n) env.sell(r.id, n) end, 'All'),
        }
      end }
  end

  local function buildVendor()
    return ui.Document{ name = 'skyrim-barter-vendor', class = 'panel',
      style = { position = 'absolute', left = 24 + WINDOW_WIDTH + 24, top = 24, width = WINDOW_WIDTH },
      build = function()
        return ui.Column{
          ui.Text{ text = title, class = 'title' },
          ui.Text{ text = vendorGold, class = 'info' },
          ui.Text{ text = status, class = 'info' },
          makeList(vendorRows, function(r, n) env.buy(r.id, n) end, 'All'),
          ui.Row{
            ui.Button{ text = 'Close', class = 'btn', onClick = function() env.close() end },
          },
        }
      end }
  end

  --- Opens the window for the merchant `name` (the session is started by the host script).
  function self.open(name)
    self.dismiss()
    isOpen = true
    title:set(name or '')
    refresh()
    docs[#docs + 1] = buildPlayer()
    docs[#docs + 1] = buildVendor()
  end

  --- Takes the windows down (the mode was left). Safe when nothing is open.
  function self.dismiss()
    for _, d in ipairs(docs) do d:destroy() end
    docs = {}
    isOpen = false
    lastRevision, lastTrades, lastState = nil, nil, nil
  end

  function self.isOpen() return isOpen end

  --- One call a frame: re-reads the lists when the inventory's revision, the trade count or the session state moved. A
  --- frame where nothing changed makes three cheap reads and no UI call.
  function self.update()
    if not isOpen then return end
    if inv.revision() ~= lastRevision or barter.trades() ~= lastTrades or barter.state() ~= lastState then
      refresh()
    end
  end

  return self
end

return M
