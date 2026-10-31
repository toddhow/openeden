-- Plugins have no Lua API. A mod reacts to what a plugin does through the events the plugin marks for forwarding.
return {
    eventHandlers = {
        openeden_example_greeted = function(data)
            print('EXAMPLE_LUA count=' .. data.count .. ' text=' .. data.text)
        end,
    },
}
