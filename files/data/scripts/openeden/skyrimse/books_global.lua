-- Skyrim SE books, the global half (task 0203). Only a global script may call the pack's Mutate functions, so the
-- inventory window's Read button sends its request here. Needs the pack's `core.skyrimse.lockbooks` namespace.
local okLb, lb = pcall(require, 'core.skyrimse.lockbooks')
if not okLb then
  return {}
end

return {
  eventHandlers = {
    SkyrimBookRead = function(data) lb.read_book(data.item) end,
  },
}
