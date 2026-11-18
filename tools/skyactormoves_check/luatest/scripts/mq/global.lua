-- tools/skyactormoves_check (task 0286): logs, every cfg.every updates, where the player is and the cfg.near nearest
-- objects within cfg.radius (record id, position), plus the actors of cfg.watch (record ids such as 'skyrimse:b1234')
-- wherever they stand in the player's cell; asks the menu script to save once at update cfg.saveAt (the storage flag
-- MQ/saveNow). Every line starts with MQ and holds no wall clock.
local world = require('openmw.world')
local storage = require('openmw.storage')
local cfg = require('scripts.mq.cfg')

local updates = 0
local asked = false

local function sample()
    local player = world.players[1]
    local p = player.position
    print(string.format('MQ u=%d cell=%s player=%.0f,%.0f,%.0f', updates, tostring(player.cell.name), p.x, p.y, p.z))
    local list = {}
    local watched = {}
    for _, o in ipairs(player.cell:getAll()) do
        local ok, rec = pcall(function() return o.recordId end)
        if ok then
            local dx, dy, dz = o.position.x - p.x, o.position.y - p.y, o.position.z - p.z
            local d = math.sqrt(dx * dx + dy * dy + dz * dz)
            if cfg.watch[rec] then watched[#watched + 1] = { rec, o.position } end
            if d <= cfg.radius then list[#list + 1] = { d, rec, o.position } end
        end
    end
    table.sort(list, function(a, b) return a[1] < b[1] end)
    for i = 1, math.min(cfg.near, #list) do
        local e = list[i]
        print(string.format('MQ   %s d=%.0f at %.0f,%.0f,%.0f', e[2], e[1], e[3].x, e[3].y, e[3].z))
    end
    table.sort(watched, function(a, b) return a[1] < b[1] end)
    for _, w in ipairs(watched) do
        print(string.format('MQW %s at %.0f,%.0f,%.0f', w[1], w[2].x, w[2].y, w[2].z))
    end
end

return {
    engineHandlers = {
        onUpdate = function()
            if world.players[1] == nil then return end
            updates = updates + 1
            if updates % cfg.every == 0 and updates >= cfg.from then sample() end
            if cfg.saveAt > 0 and updates >= cfg.saveAt and not asked then
                asked = true
                print('MQ save request at u=' .. updates)
                storage.globalSection('MQ'):set('saveNow', true)
            end
        end,
    },
}
