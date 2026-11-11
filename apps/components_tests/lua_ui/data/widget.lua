-- Geometry corpus, family "widget": position, size, relative placement, anchors, visibility and alpha.
-- Test data written for the Lua UI compatibility plan (task 0112); it contains no game text.
local ui, util = ...
local v2 = util.vector2

local function w(props, content, extra)
    local layout = { props = props or {} }
    if content then layout.content = ui.content(content) end
    for k, v in pairs(extra or {}) do layout[k] = v end
    return layout
end

return {
    {
        name = 'absolute',
        layout = w({ position = v2(10, 20), size = v2(300, 200) }, {
            w({ position = v2(5, 6), size = v2(50, 60) }, {
                w({ position = v2(1, 2), size = v2(3, 4) }),
            }),
            w({ position = v2(-10, -20), size = v2(1, 1) }),
            w({ size = v2(0, 0) }),
        }, { layer = 'Windows' }),
    },
    {
        name = 'relative',
        layout = w({ size = v2(400, 300) }, {
            w({ relativeSize = v2(0.5, 0.25), relativePosition = v2(0.1, 0.2) }, {
                w({ relativeSize = v2(0.5, 0.5) }),
                w({ relativeSize = v2(1, 1), relativePosition = v2(0.5, 0.5) }),
            }),
            w({ relativeSize = v2(1, 1) }),
            w({ relativeSize = v2(2, 0.01), relativePosition = v2(-0.5, 1.5) }),
        }, { layer = 'Windows' }),
    },
    {
        name = 'relative_with_odd_parent_size',
        layout = w({ size = v2(101, 51) }, {
            w({ relativeSize = v2(0.5, 0.5), relativePosition = v2(0.5, 0.5) }),
            w({ relativeSize = v2(1 / 3, 1 / 3), relativePosition = v2(1 / 3, 2 / 3) }),
        }, { layer = 'Windows' }),
    },
    {
        name = 'anchor',
        layout = w({ size = v2(200, 100) }, {
            w({ size = v2(50, 20), relativePosition = v2(0.5, 0.5), anchor = v2(0.5, 0.5) }),
            w({ size = v2(50, 20), relativePosition = v2(1, 1), anchor = v2(1, 1) }),
            w({ size = v2(50, 20), relativePosition = v2(0, 0), anchor = v2(-0.5, 2) }),
            w({ size = v2(51, 21), relativePosition = v2(1.5, -0.5), anchor = v2(0.25, 0.75) }),
            w({ relativeSize = v2(0.5, 0.5), relativePosition = v2(1, 0), anchor = v2(1, 0) }),
        }, { layer = 'Windows' }),
    },
    {
        name = 'absolute_plus_relative',
        layout = w({ size = v2(200, 100) }, {
            w({ size = v2(10, 10), relativeSize = v2(0.5, 0.5) }),
            w({ position = v2(7, 7), relativePosition = v2(0.25, 0.25), size = v2(5, 5) }),
            w({ position = v2(-7, -7), relativePosition = v2(1, 1), size = v2(5, 5), anchor = v2(1, 1) }),
        }, { layer = 'Windows' }),
    },
    {
        name = 'visible_and_alpha',
        layout = w({ size = v2(100, 100), alpha = 0.5 }, {
            w({ size = v2(10, 10), visible = false }, {
                w({ size = v2(5, 5) }),
            }),
            w({ size = v2(10, 10), alpha = 0.5 }),
            w({ size = v2(10, 10), alpha = 0.25, inheritAlpha = false }),
            w({ size = v2(10, 10), alpha = 2 }),
        }, { layer = 'Windows' }),
    },
    {
        name = 'root_defaults',
        layout = w(nil, nil, { layer = 'Windows' }),
    },
    {
        name = 'root_in_hud_layer_fills_it',
        layout = w({ relativeSize = v2(1, 1) }, {
            w({ relativeSize = v2(0.5, 0.5), relativePosition = v2(0.5, 0.5), anchor = v2(0.5, 0.5) }),
        }, { layer = 'HUD' }),
    },
}
