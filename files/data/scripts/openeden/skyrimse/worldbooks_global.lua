-- Skyrim SE books lying in the world, the global half (task 0234). The engine sends `openeden_skyrimse_item_activated`
-- for every placed item the player activates; for a BOOK this script reads it in place (the pack applies the first
-- reading's effect and tells the Papyrus host OnRead) and asks the player's window to open, instead of picking it up
-- (pickup_global.lua leaves books to this script). The window's Take button comes back as `SkyrimBookTake` and goes
-- through the pickup path, so an owned book is theft. Needs the pack's `core.skyrimse.worldbooks` namespace.
local world = require('openmw.world')
local okWb, wb = pcall(require, 'core.skyrimse.worldbooks')
if not okWb then
  return {}
end

return {
  eventHandlers = {
    -- data: placement and base (hex text), kind ('item'), name.
    openeden_skyrimse_item_activated = function(data)
      if not wb.is_book(data.base) then return end
      print('[skyrimse worldbooks] reading ' .. tostring(data.placement) .. ' ' .. tostring(data.name))
      wb.read(data.placement, data.base)
      local player = world.players[1]
      if player then
        player:sendEvent('SkyrimBookOpen', {
          item = data.base, placement = data.placement, takeable = wb.can_take(data.base) })
      end
    end,
    -- From the window's Take button: the book goes to the inventory through the pickup path (theft when owned).
    SkyrimBookTake = function(data)
      local okPick, pick = pcall(require, 'core.skyrimse.pickup')
      if okPick then
        pick.pickup(data.placement, data.item)
      else
        print('[skyrimse worldbooks] no pickup namespace: the book stays')
      end
    end,
  },
}
