-- Geometry corpus, family "flex": the Flex widget (grow, stretch, align, arrange, autoSize, nesting).
-- Test data written for the Lua UI compatibility plan (task 0112); it contains no game text.
local ui, util = ...
local v2 = util.vector2
local T = ui.TYPE
local A = ui.ALIGNMENT

local function box(w, h, external, props)
    local p = props or {}
    p.size = v2(w, h)
    return { props = p, external = external }
end

local function flex(props, content, external)
    return { type = T.Flex, props = props, content = ui.content(content or {}), external = external }
end

local function root(layout)
    layout.layer = 'Windows'
    return layout
end

-- nine flexes: align x arrange, each 100x60 and not auto sized
local function alignGrid(horizontal)
    local cells = {}
    local names = { A.Start, A.Center, A.End }
    for i, align in ipairs(names) do
        for j, arrange in ipairs(names) do
            cells[#cells + 1] = flex({
                horizontal = horizontal,
                autoSize = false,
                size = v2(100, 60),
                position = v2((i - 1) * 110, (j - 1) * 70),
                align = align,
                arrange = arrange,
            }, { box(20, 10), box(30, 20) })
        end
    end
    return cells
end

return {
    {
        name = 'horizontal',
        layout = root(flex({ horizontal = true, position = v2(10, 10) }, {
            box(10, 20), box(30, 5), box(7, 40),
        })),
    },
    {
        name = 'vertical',
        layout = root(flex({ position = v2(10, 10) }, {
            box(10, 20), box(30, 5), box(7, 40),
        })),
    },
    {
        name = 'align_arrange_horizontal',
        layout = root({ props = { size = v2(400, 300) }, content = ui.content(alignGrid(true)) }),
    },
    {
        name = 'align_arrange_vertical',
        layout = root({ props = { size = v2(400, 300) }, content = ui.content(alignGrid(false)) }),
    },
    {
        name = 'grow',
        layout = root(flex({ horizontal = true, autoSize = false, size = v2(100, 10) }, {
            box(10, 10, { grow = 1 }), box(20, 10), box(5, 10, { grow = 2 }),
        })),
    },
    {
        name = 'grow_fractions_truncate',
        layout = root(flex({ horizontal = true, autoSize = false, size = v2(101, 10) }, {
            box(0, 10, { grow = 0.3 }), box(0, 10, { grow = 0.3 }), box(0, 10, { grow = 0.4 }),
        })),
    },
    {
        name = 'grow_without_free_space',
        layout = root(flex({ horizontal = true, autoSize = false, size = v2(20, 10) }, {
            box(15, 10, { grow = 1 }), box(15, 10, { grow = 1 }),
        })),
    },
    {
        name = 'grow_negative_is_zero',
        layout = root(flex({ horizontal = true, autoSize = false, size = v2(100, 10) }, {
            box(10, 10, { grow = -5 }), box(10, 10, { grow = 1 }),
        })),
    },
    {
        name = 'stretch',
        layout = root(flex({ horizontal = true, autoSize = false, size = v2(100, 50) }, {
            box(10, 10, { stretch = 0.5 }),
            box(10, 10, { stretch = 1 }),
            box(10, 10, { stretch = 0 }),
            box(10, 10, { stretch = 2 }),
            box(10, 60, { stretch = 0.5 }),
        })),
    },
    {
        name = 'auto_size_on_and_off',
        layout = root({ props = { size = v2(200, 200) }, content = ui.content {
            flex({ horizontal = true, size = v2(5, 5), position = v2(0, 0) }, { box(40, 30), box(40, 10) }),
            flex({ horizontal = true, autoSize = false, size = v2(5, 5), position = v2(0, 100) },
                { box(40, 30), box(40, 10) }),
            flex({ horizontal = true, size = v2(100, 100), position = v2(100, 0) }, { box(40, 30) }),
        } }),
    },
    {
        name = 'relative_size_children',
        layout = root(flex({ horizontal = true, size = v2(100, 40) }, {
            box(10, 10),
            { props = { relativeSize = v2(0.5, 1) } },
            { props = { relativeSize = v2(0.25, 0.5), size = v2(3, 3) } },
        })),
    },
    {
        name = 'relative_size_children_fixed_flex',
        layout = root(flex({ horizontal = true, autoSize = false, size = v2(100, 40) }, {
            box(10, 10),
            { props = { relativeSize = v2(0.5, 1) } },
            { props = { relativeSize = v2(0.25, 0.5), size = v2(3, 3) }, external = { grow = 1 } },
        })),
    },
    {
        name = 'nested_three_deep',
        layout = root(flex({ position = v2(4, 4) }, {
            box(30, 10),
            flex({ horizontal = true, align = A.End }, {
                box(10, 10),
                flex({ autoSize = false, size = v2(20, 40), arrange = A.Center }, {
                    box(8, 8), box(12, 12, { grow = 1 }),
                }, { grow = 1 }),
            }),
            box(5, 5),
        })),
    },
    {
        name = 'empty',
        layout = root({ props = { size = v2(200, 100) }, content = ui.content {
            flex({ size = v2(30, 30) }),
            flex({ position = v2(50, 0) }),
            flex({ position = v2(100, 0), horizontal = true, autoSize = false, size = v2(10, 10) }),
        } }),
    },
    {
        name = 'in_sized_widget_with_relative_size',
        layout = root({ props = { size = v2(300, 100) }, content = ui.content {
            flex({ horizontal = true, relativeSize = v2(1, 0.5), autoSize = false }, {
                box(10, 10, { grow = 1 }), box(10, 10),
            }),
            flex({ horizontal = true, relativeSize = v2(1, 0.5), relativePosition = v2(0, 0.5) }, {
                box(10, 10, { grow = 1 }), box(10, 10),
            }),
        } }),
    },
}
