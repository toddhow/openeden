-- Skyrim SE skills and perks, the global half (task 0200). Only a global script may call the pack's Mutate functions, so
-- the player's window sends its requests here. Needs the pack's `core.skyrimse.progression` namespace (the builtin line
-- carries REQUIRES=).
local okProg, prog = pcall(require, 'core.skyrimse.progression')
if not okProg then
  return {}
end

return {
  eventHandlers = {
    SkyrimSkillsLevelUp = function(data) prog.choose_level_up(data.vital) end,
    SkyrimSkillsBuyPerk = function(data) prog.buy_perk(data.id) end,
  },
}
