-- tools/neutral_session_check (task 0204): the player half of "n8crime". The offer window pauses the simulation, so a
-- global script's clock stops; this script runs on frames and uses real time: it takes a picture of the offer 1 s after it
-- appears and sends the configured choice (the same global event the window's buttons send) cfg.choiceDelay s after it.
local core = require('openmw.core')
local debug = require('openmw.debug')
local okCrime, crime = pcall(require, 'core.skyrimse.crime')
local cfg = require('scripts.n8.cfg')
if not okCrime then return {} end

local I = require('openmw.interfaces')
local nextMode = 0
local offerAt, shotDone, chosen, last = nil, false, false, nil
return {
    engineHandlers = {
        onFrame = function()
            local state = crime.state()
            local now = core.getRealTime()
            if now >= nextMode then -- task 0258: what the GUI mode stack is, every 2 real seconds
                nextMode = now + 2
                print(string.format('N8CRIME mode %s state %s offer_mode %s', tostring(I.UI.getMode()), state,
                    crime.offer_mode and crime.offer_mode() or '-'))
            end
            if state ~= last then
                print(string.format('N8CRIME player sees state %s bounty %d', state, crime.bounty()))
                last = state
            end
            if state == 'offer' then
                offerAt = offerAt or now
                if not shotDone and now >= offerAt + 1 then
                    shotDone = true
                    debug.takeScreenshot()
                end
                if not chosen and cfg.choice ~= '' and now >= offerAt + cfg.choiceDelay then
                    chosen = true
                    print(string.format('N8CRIME choose %s', cfg.choice))
                    core.sendGlobalEvent('SkyrimCrimeChoose', { choice = cfg.choice })
                end
            elseif offerAt then
                offerAt, shotDone, chosen = nil, false, false
            end
        end,
    },
}
