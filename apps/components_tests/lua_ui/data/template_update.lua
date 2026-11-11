-- Geometry corpus, family "template_update": layout templates, and layouts that change after creation (steps).
-- Test data written for the Lua UI compatibility plan (task 0112); it contains no game text.
local ui, util = ...
local v2 = util.vector2
local T = ui.TYPE

local function box(w, h, props, external)
    local p = props or {}
    p.size = v2(w, h)
    return { props = p, external = external }
end

-- steps share the layout table they change
local function movable()
    local layout = {
        layer = 'Windows', props = { position = v2(10, 10), size = v2(100, 100) },
        content = ui.content { box(20, 20, { position = v2(5, 5) }), box(20, 20, { position = v2(30, 5) }) },
    }
    return layout
end

local moveLayout = movable()
local textLayout = {
    layer = 'Windows', type = T.Flex, props = { horizontal = true },
    content = ui.content {
        { type = T.Text, props = { text = 'short', textSize = 12 } },
        box(10, 10),
    },
}
local contentLayout = {
    layer = 'Windows', type = T.Flex, props = {},
    content = ui.content { box(10, 10), box(20, 20), box(30, 30) },
}
local typeLayout = {
    layer = 'Windows', props = { size = v2(200, 100) },
    content = ui.content { box(10, 10), box(20, 20) },
}
local visibleLayout = {
    layer = 'Windows', type = T.Flex, props = {},
    content = ui.content { box(10, 10), { props = { size = v2(20, 20), visible = true } }, box(30, 30) },
}
local anchorLayout = {
    layer = 'Windows', props = { size = v2(200, 100) },
    content = ui.content { { props = { size = v2(20, 20), relativePosition = v2(0.5, 0.5), anchor = v2(0.5, 0.5) } } },
}

return {
    {
        name = 'template_props_are_defaults',
        layout = {
            layer = 'Windows',
            template = { props = { size = v2(30, 30), position = v2(5, 5) } },
            props = { position = v2(50, 50) },
        },
    },
    {
        name = 'template_content_is_drawn_with_the_widget',
        layout = {
            layer = 'Windows', props = { size = v2(100, 60) },
            template = {
                content = ui.content {
                    { props = { relativeSize = v2(1, 1) } },
                    { props = { relativeSize = v2(0.5, 0.5), relativePosition = v2(0.5, 0.5) } },
                },
            },
            content = ui.content { box(10, 10) },
        },
    },
    {
        name = 'template_slot_takes_the_content',
        layout = {
            layer = 'Windows', props = { size = v2(100, 60) },
            template = {
                content = ui.content {
                    { props = { relativeSize = v2(1, 1) } },
                    { external = { slot = true }, props = { position = v2(4, 4), relativeSize = v2(1, 1),
                                                            size = v2(-8, -8) } },
                },
            },
            content = ui.content { box(10, 10), box(20, 20, { position = v2(10, 0) }) },
        },
    },
    {
        name = 'template_type_gives_the_widget_type',
        layout = {
            layer = 'Windows',
            template = { type = T.Flex, props = { horizontal = true } },
            content = ui.content { box(10, 10), box(20, 20) },
        },
    },
    {
        name = 'template_inside_template',
        layout = {
            layer = 'Windows', props = { size = v2(80, 80) },
            template = {
                content = ui.content {
                    { props = { relativeSize = v2(1, 1) },
                      template = { content = ui.content { { props = { size = v2(5, 5) } } } } },
                },
            },
        },
    },
    {
        name = 'step_move_and_resize',
        layout = moveLayout,
        steps = {
            function() moveLayout.props.position = v2(50, 60) end,
            function() moveLayout.props.size = v2(10, 10) end,
            function() moveLayout.content[1].props.position = v2(0, 0) end,
        },
    },
    {
        name = 'step_text_change_resizes_the_flex',
        layout = textLayout,
        steps = {
            function() textLayout.content[1].props.text = 'a much longer line of text' end,
            function() textLayout.content[1].props.textSize = 24 end,
            function() textLayout.content[1].props.text = '' end,
        },
    },
    {
        name = 'step_content_add_remove_reorder',
        layout = contentLayout,
        steps = {
            function() contentLayout.content:add(box(5, 40)) end,
            function() contentLayout.content = ui.content { box(30, 30), box(10, 10) } end,
            function() contentLayout.content = ui.content { box(30, 30), box(10, 10), box(1, 1), box(2, 2) } end,
            function() contentLayout.content = ui.content {} end,
            function() contentLayout.content = ui.content { box(7, 7) } end,
        },
    },
    {
        name = 'step_child_type_changes',
        layout = typeLayout,
        steps = {
            function() typeLayout.content[1] = { type = T.Text, props = { text = 'hi', textSize = 12 } } end,
            function() typeLayout.content[2] = { type = T.Flex, props = {},
                content = ui.content { box(3, 3), box(4, 4) } } end,
            function() typeLayout.content[1] = box(10, 10) end,
        },
    },
    {
        name = 'step_visibility_does_not_move_siblings',
        layout = visibleLayout,
        steps = {
            function() visibleLayout.content[2].props.visible = false end,
            function() visibleLayout.content[2].props.visible = true end,
        },
    },
    {
        name = 'step_anchor_follows_new_size',
        layout = anchorLayout,
        steps = {
            function() anchorLayout.props.size = v2(300, 300) end,
            function() anchorLayout.content[1].props.size = v2(50, 10) end,
        },
    },
}
