-- The Skyrim SE guard offer (task 0204): a small window with the bounty, the guard and three buttons, written with
-- `openeden.ui` (decision 0015). The module holds the window logic and talks only to what it is handed, so it runs over
-- the headless UI backend in tests:
--   env.ui       the `openeden.ui` library
--   env.crime    the `core.skyrimse.crime` package (state, bounty, guard, message, revision)
--   env.choose(choice)   'pay', 'jail' or 'resist' (the host script sends it to the global script)
local M = {}

local WINDOW_WIDTH = 420

function M.create(env)
  local ui, crime = env.ui, env.crime
  local self = {}
  local doc
  local title = ui.Value('')
  local detail = ui.Value('')
  local status = ui.Value('')
  local payLabel = ui.Value('Pay')
  local isOpen = false
  local lastRevision

  local function refresh()
    lastRevision = crime.revision()
    local bounty = crime.offer_bounty()
    title:set(string.format('%s: "Stop right there, criminal scum!"', crime.guard_name()))
    detail:set(crime.message())
    payLabel:set(string.format('Pay %d gold', bounty))
    status:set(crime.can_pay() and '' or 'You cannot afford the fine.')
  end

  local function build()
    return ui.Document{ name = 'skyrim-crime-offer', class = 'panel',
      style = { position = 'absolute', left = 24, top = 24, width = WINDOW_WIDTH },
      build = function()
        return ui.Column{
          ui.Text{ text = title, class = 'title' },
          ui.Text{ text = detail, class = 'info' },
          ui.Text{ text = status, class = 'info' },
          ui.Row{
            ui.Button{ text = payLabel, class = 'btn', onClick = function() env.choose('pay') end },
            ui.Button{ text = 'Go to jail', class = 'btn', onClick = function() env.choose('jail') end },
            ui.Button{ text = 'Resist arrest', class = 'btn', onClick = function() env.choose('resist') end },
          },
        }
      end }
  end

  function self.open()
    self.dismiss()
    isOpen = true
    refresh()
    doc = build()
  end

  function self.dismiss()
    if doc then doc:destroy() end
    doc = nil
    isOpen = false
    lastRevision = nil
  end

  function self.isOpen() return isOpen end

  --- One call a frame: re-reads the texts when the pack's revision moved.
  function self.update()
    if isOpen and crime.revision() ~= lastRevision then refresh() end
  end

  return self
end

return M
