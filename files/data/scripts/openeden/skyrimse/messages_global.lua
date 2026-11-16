-- Skyrim SE message boxes, the global half (task 0237). Only a global script may call the pack's Mutate functions, so
-- the player's window sends its button here.
local okM, messages = pcall(require, 'core.skyrimse.messages')
if not okM then
  return {}
end

return {
  eventHandlers = {
    -- From the engine (Neutral::activate on a named Activator): data has placement and base (hex text). The pack ignores a
    -- base that is not a standing stone or a shrine.
    openeden_skyrimse_resource_activated = function(data)
      messages.activate(data.placement, data.base)
    end,
    SkyrimMessageChoose = function(data)
      local n = math.floor(tonumber(data.button) or 0)
      if n >= 1 then
        messages.choose(n)
        print('[skyrimse messages] button ' .. n)
      end
    end,
  },
}
