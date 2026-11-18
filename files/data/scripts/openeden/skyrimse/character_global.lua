-- Skyrim SE character creation, the global half (task 0225). Only a global script may call the pack's Mutate functions,
-- so the player's window sends its requests here, in the order it made them.
local okChr, chr = pcall(require, 'core.skyrimse.character')
if not okChr then
  return {}
end

return {
  eventHandlers = {
    SkyrimCharacterRequest = function(data)
      if type(data) ~= 'table' then return end
      if data.kind == 'race' and type(data.index) == 'number' then
        chr.choose_race(math.floor(data.index))
      elseif data.kind == 'female' then
        chr.choose_female(data.value == true)
      elseif data.kind == 'part' and type(data.part) == 'string' and type(data.delta) == 'number' then
        chr.step_part(data.part, math.floor(data.delta))
      elseif data.kind == 'name' and type(data.text) == 'string' then
        chr.set_name(data.text)
      elseif data.kind == 'finish' then
        chr.finish()
      elseif data.kind == 'power' and type(data.index) == 'number' and type(data.now) == 'number' then
        -- task 0228: the race's power (once a game day); the pack checks that it is ready
        chr.use_power(math.floor(data.index), math.floor(data.now))
      elseif data.kind == 'choosepower' and type(data.name) == 'string' then
        -- task 0281: the power the P key uses (the shout menu's Equip button); "" clears the choice
        chr.choose_power(data.name)
      end
    end,
  },
}
