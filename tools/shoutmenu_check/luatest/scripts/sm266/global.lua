-- tools/shoutmenu_check (task 0266): prints the shout menu through the pack's `core.skyrimse.shouts` namespace every two
-- seconds of simulation time and, in the "run" phase, asks the menu script to save the game 3 s after a shout got
-- equipped (the menu's Unlock and Equip buttons are pressed by the input script before that).
local core = require('openmw.core')
local storage = require('openmw.storage')
local cfg = require('scripts.sm266.cfg')
local shouts = require('core.skyrimse.shouts')

local t0, last, equippedAt, saved = nil, -10, nil, false

local function describe()
    local parts = {}
    for i = 0, shouts.menu_count() - 1 do
        local words = {}
        for w = 0, shouts.menu_word_count(i) - 1 do
            words[#words + 1] = string.format('%s/%s/%s', shouts.menu_word_name(i, w),
                shouts.menu_word_translation(i, w), shouts.menu_word_state(i, w))
        end
        parts[#parts + 1] = string.format('%s%s[%s]', shouts.menu_equipped(i) and '*' or '', shouts.menu_id(i),
            table.concat(words, ' '))
    end
    return table.concat(parts, ' ; ')
end

return {
    engineHandlers = {
        onUpdate = function()
            if not shouts.ready() then return end
            t0 = t0 or core.getSimulationTime()
            local t = core.getSimulationTime() - t0
            if t - last >= 2 then
                last = t
                print(string.format('SHOUTMENU %s t=%.1f equipped=%s souls=%d menu: %s', cfg.phase, t,
                    shouts.equipped_name(), shouts.souls(), describe()))
            end
            if shouts.equipped_name() ~= '' and not equippedAt then equippedAt = t end
            if cfg.phase == 'run' and equippedAt and t - equippedAt >= 3 and not saved then
                saved = true
                storage.globalSection('SM266'):set('saveNow', true)
            end
        end,
    },
}
