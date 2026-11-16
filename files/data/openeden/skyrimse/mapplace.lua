-- The place the Skyrim SE world map window shows (task 0223): the player's own place in an exterior; in an interior the
-- last exterior world the player stood in (and where, the door they went in by), else the exterior world of the interior's
-- location (no position then: the player marker stays hidden). The compass keeps the player's own place (indoors it shows
-- no markers); only the map window uses this.
--   env.place()      { x, y, yaw, world } of the player (world nil indoors), or nil
--   env.cellName()   the editor id of the cell the player stands in
--   env.map          the `core.skyrimse.map` package (interior_world), optional
-- create(env) returns a function that gives { x, y, yaw, world, indoors } or the player's own place.
local M = {}

function M.create(env)
  local lastExterior = nil
  local byCell = {} -- the pack's answer per interior (it walks the location tree)
  return function()
    local own = env.place()
    if not own then return nil end
    if own.world then
      lastExterior = { x = own.x, y = own.y, world = own.world }
      return own
    end
    if lastExterior then
      return { x = lastExterior.x, y = lastExterior.y, yaw = own.yaw, world = lastExterior.world, indoors = true }
    end
    local map = env.map
    if map and map.interior_world then
      local name = tostring(env.cellName() or '')
      local world = byCell[name]
      if world == nil then
        world = map.interior_world(name)
        if world and world > 0 then byCell[name] = world end -- an early 0 (data not read yet) is asked again
      end
      if world and world > 0 then
        return { yaw = own.yaw, world = world, indoors = true }
      end
    end
    return own
  end
end

return M
