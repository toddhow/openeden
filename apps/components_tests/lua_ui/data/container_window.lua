-- Geometry corpus, family "container_window": Container, Window, templates with a slot and the image widgets.
-- Test data written for the Lua UI compatibility plan (task 0112); it contains no game text.
local ui, util = ...
local v2 = util.vector2
local T = ui.TYPE

local function box(w, h, props, external)
    local p = props or {}
    p.size = v2(w, h)
    return { props = p, external = external }
end

return {
    {
        name = 'container_fits_children',
        layout = {
            layer = 'Windows', type = T.Container, props = { position = v2(10, 10) },
            content = ui.content {
                { props = { position = v2(5, 5), size = v2(20, 10) } },
                { props = { position = v2(0, 30), size = v2(7, 7) } },
                { props = { relativeSize = v2(1, 1) } },
            },
        },
    },
    {
        name = 'container_with_template_and_slot',
        layout = {
            layer = 'Windows', type = T.Container, props = { position = v2(10, 10) },
            template = {
                content = ui.content {
                    { props = { size = v2(2, 2) } },
                    { external = { slot = true }, props = { position = v2(2, 2), relativeSize = v2(1, 1) } },
                    { props = { relativePosition = v2(1, 1), position = v2(0, 0), size = v2(2, 2) } },
                },
            },
            content = ui.content { box(30, 20), box(10, 40, { position = v2(5, 5) }) },
        },
    },
    {
        name = 'window_with_caption_and_action',
        layout = {
            layer = 'Windows', type = T.Window,
            props = { position = v2(20, 20), size = v2(200, 100), caption = 'Title' },
            template = {
                content = ui.content {
                    { type = T.Text, external = { caption = true, action = true },
                      props = { text = 'Title', textSize = 16 } },
                    { external = { action = true, move = v2(0, 0), resize = v2(1, 1) },
                      props = { relativePosition = v2(1, 1), anchor = v2(1, 1), size = v2(8, 8) } },
                },
            },
            content = ui.content { box(50, 50, { position = v2(0, 20) }) },
        },
    },
    {
        name = 'image_sizes',
        layout = {
            layer = 'Windows', type = T.Widget, props = { size = v2(300, 300) },
            content = ui.content {
                { type = T.Image, props = { resource = ui.texture { path = 'textures/a.dds' },
                                            size = v2(40, 40) } },
                { type = T.Image, props = { resource = ui.texture { path = 'textures/b.dds',
                                                                    offset = v2(4, 4), size = v2(8, 8) },
                                            position = v2(50, 0), size = v2(40, 40) } },
                { type = T.Image, props = { resource = ui.texture { path = 'textures/a.dds' },
                                            tileH = true, tileV = true, position = v2(100, 0), size = v2(50, 50) } },
                { type = T.Image, props = { position = v2(160, 0), size = v2(10, 10) } },
                { type = T.Image, props = { resource = ui.texture { path = 'white' }, color = util.color.rgb(1, 0, 0),
                                            position = v2(180, 0), size = v2(20, 20), alpha = 0.5 } },
                { type = T.Image, props = { resource = ui.texture { path = 'textures/a.dds' },
                                            relativeSize = v2(1, 0.2), relativePosition = v2(0, 0.5), tileH = true } },
            },
        },
    },
}
