-- Geometry corpus, family "text": text and text edit widgets measured with the DejaVu font of files/data/fonts.
-- Test data written for the Lua UI compatibility plan (task 0112); it contains no game text.
local ui, util = ...
local v2 = util.vector2
local T = ui.TYPE
local A = ui.ALIGNMENT

local function text(props, external)
    return { type = T.Text, props = props, external = external }
end

local function flexOf(props, content)
    return { type = T.Flex, props = props, content = ui.content(content) }
end

local aligns = { A.Start, A.Center, A.End }

local function alignGrid(widgetType)
    local cells = {}
    for i, h in ipairs(aligns) do
        for j, v in ipairs(aligns) do
            cells[#cells + 1] = {
                type = widgetType,
                props = {
                    text = 'xyz', textSize = 12, autoSize = false, size = v2(100, 40),
                    position = v2((i - 1) * 110, (j - 1) * 50), textAlignH = h, textAlignV = v,
                },
            }
        end
    end
    return cells
end

return {
    {
        name = 'sizes',
        layout = {
            layer = 'Windows', props = { size = v2(400, 200) },
            content = ui.content {
                text({ text = 'Hello', textSize = 10 }),
                text({ text = 'Hello', textSize = 16, position = v2(0, 20) }),
                text({ text = 'Hello', textSize = 24, position = v2(0, 50) }),
                text({ text = 'Hello, longer text!', textSize = 16, position = v2(0, 100) }),
                text({ text = '', textSize = 16, position = v2(0, 150) }),
            },
        },
    },
    {
        name = 'default_text_size',
        layout = { layer = 'Windows', type = T.Text, props = { text = 'abc' } },
    },
    {
        name = 'newlines_single_and_multiline',
        layout = {
            layer = 'Windows', props = { size = v2(400, 200) },
            content = ui.content {
                text({ text = 'one\ntwo\nthree', textSize = 14 }),
                text({ text = 'one\ntwo\nthree', textSize = 14, multiline = true, position = v2(100, 0) }),
            },
        },
    },
    {
        name = 'wrap',
        layout = {
            layer = 'Windows', props = { size = v2(400, 300) },
            content = ui.content {
                text({ text = 'the quick brown fox jumps over the lazy dog', textSize = 12, multiline = true,
                       wordWrap = true, autoSize = false, size = v2(80, 200) }),
                text({ text = 'the quick brown fox jumps over the lazy dog', textSize = 12, multiline = true,
                       wordWrap = true, autoSize = false, size = v2(160, 200), position = v2(100, 0) }),
                text({ text = 'the quick brown fox jumps over the lazy dog', textSize = 12, multiline = true,
                       wordWrap = true, size = v2(60, 10), position = v2(0, 220) }),
                text({ text = 'averyveryverylongwordthatdoesnotfit', textSize = 12, multiline = true,
                       wordWrap = true, autoSize = false, size = v2(50, 100), position = v2(300, 0) }),
            },
        },
    },
    {
        name = 'align_grid',
        layout = { layer = 'Windows', props = { size = v2(400, 200) }, content = ui.content(alignGrid(T.Text)) },
    },
    {
        name = 'text_in_flex',
        layout = {
            layer = 'Windows',
            type = T.Flex,
            props = { horizontal = true, autoSize = false, size = v2(300, 40), arrange = A.Center },
            content = ui.content {
                text({ text = 'left', textSize = 12 }),
                text({ text = 'grows', textSize = 12 }, { grow = 1 }),
                text({ text = 'big', textSize = 24 }),
            },
        },
    },
    {
        name = 'text_in_vertical_flex_with_stretch',
        layout = {
            layer = 'Windows',
            type = T.Flex,
            props = { autoSize = true, size = v2(100, 0) },
            content = ui.content {
                text({ text = 'first', textSize = 12 }, { stretch = 1 }),
                text({ text = 'second line', textSize = 12 }),
                flexOf({ horizontal = true }, { text({ text = 'a', textSize = 12 }), text({ text = 'b', textSize = 20 }) }),
            },
        },
    },
    {
        name = 'text_edit',
        layout = {
            layer = 'Windows', props = { size = v2(400, 300) },
            content = ui.content {
                { type = T.TextEdit, props = { text = 'edit me', textSize = 14, size = v2(100, 30) } },
                { type = T.TextEdit, props = { text = 'edit me', textSize = 14, autoSize = true,
                                               position = v2(0, 40) } },
                { type = T.TextEdit, props = { text = 'a read only line', textSize = 14, readOnly = true,
                                               autoSize = true, position = v2(0, 80) } },
                { type = T.TextEdit, props = { text = 'multi\nline text edit', textSize = 14, multiline = true,
                                               autoSize = true, position = v2(0, 120) } },
                { type = T.TextEdit, props = { text = 'wrapped text in a read only multiline box that wraps',
                                               textSize = 12, multiline = true, readOnly = true, wordWrap = true,
                                               autoSize = true, size = v2(100, 0), position = v2(150, 0) } },
            },
        },
    },
    {
        name = 'text_edit_align_grid',
        layout = { layer = 'Windows', props = { size = v2(400, 200) }, content = ui.content(alignGrid(T.TextEdit)) },
    },
}
