-- The Skyrim SE message box (task 0237): the text and buttons of a Papyrus Message.Show, for example the standing
-- stones' "choose this sign?". Written with `openeden.ui` (decision 0015). The module holds the window logic and talks only
-- to what it is handed, so it runs over the headless UI backend in tests:
--   env.ui         the `openeden.ui` library
--   env.messages   the `core.skyrimse.messages` package (active, text, button_count, button, revision)
--   env.choose(n)  the player pressed button n (1-based); the host script sends it to the global script
local M = {}

local WINDOW_WIDTH = 460

function M.create(env)
  local ui, messages = env.ui, env.messages
  local self = {}
  local doc
  local text = ui.Value('')
  local isOpen = false
  local lastRevision

  local function build()
    local labels = {}
    for n = 1, messages.button_count() do labels[n] = messages.button(n) end
    return ui.Document{ name = 'skyrim-message', class = 'panel plain-font',
      style = { position = 'absolute', left = 24, top = 24, width = WINDOW_WIDTH },
      build = function()
        -- components are built inside the document's build function (they need its scope)
        local buttons = {}
        for n, label in ipairs(labels) do
          buttons[#buttons + 1] = ui.Button{ text = label, class = 'btn', onClick = function() env.choose(n) end }
        end
        return ui.Column{
          ui.Text{ text = text, class = 'info' },
          ui.Row(buttons),
        }
      end }
  end

  function self.open()
    self.dismiss()
    isOpen = true
    lastRevision = messages.revision()
    text:set(messages.text())
    doc = build()
  end

  function self.dismiss()
    if doc then doc:destroy() end
    doc = nil
    isOpen = false
    lastRevision = nil
  end

  function self.isOpen() return isOpen end

  --- One call a frame: a different message in the same revision run reopens the window.
  function self.update()
    if isOpen and messages.revision() ~= lastRevision then self.open() end
  end

  return self
end

return M
