-- Prints what an extension's forwarded events carry, for tools/extension_data_check/check.sh to read.
return {
    eventHandlers = {
        openeden_openeden_test_extension_c_ping = function(data)
            print('EXTENSION_EVENT ' .. data.count .. ' ' .. data.value .. ' ' .. tostring(data.flag) .. ' ' .. data.text)
        end,
        openeden_openeden_test_extension_c_unmarked = function()
            print('EXTENSION_UNMARKED')
        end,
    },
}
