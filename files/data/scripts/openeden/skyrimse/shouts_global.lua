-- Skyrim SE shouts, the global half (task 0266). Only a global script may call the pack's Mutate functions, so the
-- shout menu's requests (the player script sends them) end here. Needs the pack's `core.skyrimse.shouts` namespace (the
-- builtin line carries REQUIRES=).
local okShouts, shouts = pcall(require, 'core.skyrimse.shouts')
if not okShouts then
  return {}
end

return {
  eventHandlers = {
    SkyrimShoutsUnlock = function(data) shouts.menu_unlock(data.id) end,
    SkyrimShoutsEquip = function(data) shouts.menu_equip(data.id) end,
  },
}
