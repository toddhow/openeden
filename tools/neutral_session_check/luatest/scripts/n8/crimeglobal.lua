-- tools/neutral_session_check (task 0204, crime): the global half of "n8crime". The player stands DIST units east of the
-- target actor (a witness, default Hulda b80803) and the script commits the crime the config names:
--   mode 'theft'   at T_CRIME takes the first non-gold item out of the owned container (inv.open, then inv.transfer)
--   mode 'assault' at T_CRIME the pack's player swing does the hitting (SETTINGS_EXTRA: combat test player swing)
-- It prints "N8CRIME t state bounty ..." every second, takes a picture when the state changes and every SHOT_EVERY
-- seconds, and at the first 'offer' waits CHOICE_DELAY seconds and then chooses cfg.choice (pay, jail, resist) through
-- the same event the window's buttons send (real time: the offer window pauses the simulation). cfg is written by check.sh from the N8_* variables.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')
local okCrime, crime = pcall(require, 'core.skyrimse.crime')
local okInv, inv = pcall(require, 'core.skyrimse.inventory')
local cfg = require('scripts.n8.cfg')
local storage = require('openmw.storage')
local saved = false

local start, stage, lastState, offerAt, nextPrint, nextShot, shots = nil, 0, nil, nil, 0, 0, 0
local px, py, pz

local function find(cell, tag)
    for _, o in ipairs(cell:getAll()) do
        if string.find(o.recordId, tag, 1, true) then return o end
    end
end

local function shot(player)
    shots = shots + 1
    player:sendEvent('N5Shot')
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil or not okCrime then return end
            start = start or core.getSimulationTime()
            local t = core.getSimulationTime() - start
            local actor = find(player.cell, cfg.target)
            if actor == nil then return end
            if not px then
                local p = actor.position
                px, py, pz = p.x + cfg.dist, p.y, p.z + 2
            end
            local inJail = crime.state() == 'jailed' or crime.state() == 'resisted'
            if not inJail then -- keep the player in front of the witness, facing it (not while the pack moves him to jail)
                local p = actor.position
                local st = crime.state()
                if cfg.guard ~= '' and (st == 'offer' or st == 'resisted') then -- face the guard once he is there
                    local g = find(player.cell, cfg.guard)
                    if g then p = g.position end
                end
                local yaw = math.atan2(p.x - px, p.y - py)
                player:teleport(player.cell, util.vector3(px, py, pz), { rotation = util.transform.rotateZ(yaw) })
            end
            if cfg.phase == 'save' and not saved and t >= cfg.saveT then
                saved = true
                print(string.format('N8CRIME t %.2f saving: state %s bounty %d stolen %d', t, crime.state(), crime.bounty(), crime.stolen_count()))
                storage.globalSection('N8CRIME'):set('saveNow', true)
            end
            if cfg.phase == 'load' and t >= 0 and not saved and t >= 1.0 then
                saved = true
                print(string.format('N8CRIME t %.2f loaded: state %s bounty %d stolen %d', t, crime.state(), crime.bounty(), crime.stolen_count()))
            end
            if cfg.mode == 'theft' and okInv and cfg.phase ~= 'load' then
                if stage == 0 and t >= cfg.tCrime - 1 then
                    inv.open(cfg.placement, cfg.base, 'container')
                    stage = 1
                elseif stage == 1 and t >= cfg.tCrime then
                    local items = inv.items(cfg.placement)
                    local gold = inv.gold_item()
                    for _, id in ipairs(items) do
                        if id ~= gold then
                            print(string.format('N8CRIME t %.2f steals %s x%d (%s)', t, id, inv.count(cfg.placement, id), inv.name(id)))
                            inv.transfer(cfg.placement, 'player', id, inv.count(cfg.placement, id))
                            break
                        end
                    end
                    stage = 2
                    shot(player)
                end
            end
            if t >= nextPrint then
                nextPrint = t + 1
                print(string.format('N8CRIME t %.2f state %s bounty %d offer %d stolen %d guard %s', t, crime.state(),
                    crime.bounty(), crime.offer_bounty(), crime.stolen_count(), crime.guard_name()))
            end
            local state = crime.state()
            if state ~= lastState then
                lastState = state
                print(string.format('N8CRIME t %.2f -> %s', t, state))
                shot(player)
                if state == 'offer' then offerAt = core.getRealTime() end
            end
            if t >= nextShot and shots < cfg.maxShots then
                nextShot = t + cfg.every
                shot(player)
            end
        end,
    },
}
