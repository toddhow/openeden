-- Prints what a plugin's forwarded events carry, for tools/plugin_data_check/check.sh to read.
return {
    eventHandlers = {
        openeden_openeden_test_plugin_c_ping = function(data)
            print('PLUGIN_EVENT ' .. data.count .. ' ' .. data.value .. ' ' .. tostring(data.flag) .. ' ' .. data.text)
        end,
        openeden_openeden_test_plugin_c_unmarked = function()
            print('PLUGIN_UNMARKED')
        end,
    },
}
