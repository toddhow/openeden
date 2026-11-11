-- Geometry corpus, family "mwui": layouts built from the real I.MWUI templates of files/data/scripts/omw/mwui.
-- The scripts are loaded unchanged; openmw.ui, openmw.util and openmw.core are the harness stand-ins (every
-- texture is 16 x 16 there). Test data written for the Lua UI compatibility plan (task 0112).
local ui, util = ...
local v2 = util.vector2
local T = ui.TYPE
local I = require('scripts.omw.mwui.init').interface
local templates = I.templates

local function textLine(t)
    return { template = templates.textNormal, props = { text = t } }
end

return {
    {
        unorderedChildren = true,
        name = 'text_templates',
        layout = {
            layer = 'Windows', type = T.Flex,
            content = ui.content {
                { template = templates.textHeader, props = { text = 'Header' } },
                textLine('Normal line'),
                { template = templates.textParagraph, props = { text = 'a paragraph of text that wraps around' } },
            },
        },
    },
    {
        unorderedChildren = true,
        name = 'padding_around_content',
        layout = {
            layer = 'Windows', template = templates.padding, props = { position = v2(10, 10) },
            content = ui.content { textLine('padded') },
        },
    },
    {
        unorderedChildren = true,
        name = 'interval_and_flex',
        layout = {
            layer = 'Windows', type = T.Flex, props = { horizontal = true },
            content = ui.content {
                textLine('one'),
                { template = templates.interval },
                textLine('two'),
            },
        },
    },
    {
        unorderedChildren = true,
        name = 'box_with_borders',
        layout = {
            layer = 'Windows', template = templates.box, props = { position = v2(20, 20) },
            content = ui.content {
                { template = templates.padding, content = ui.content { textLine('inside a box') } },
            },
        },
    },
    {
        unorderedChildren = true,
        name = 'box_transparent_and_thick',
        layout = {
            layer = 'Windows', type = T.Flex, props = { position = v2(0, 0) },
            content = ui.content {
                { template = templates.boxTransparent, content = ui.content { textLine('transparent') } },
                { template = templates.boxThick, content = ui.content { textLine('thick') } },
                { template = templates.boxTransparentThick, content = ui.content { textLine('thick transparent') } },
            },
        },
    },
    {
        unorderedChildren = true,
        name = 'lines_in_a_flex',
        layout = {
            layer = 'Windows', type = T.Flex, props = { size = v2(120, 0), autoSize = true },
            content = ui.content {
                textLine('above'),
                { template = templates.horizontalLine, props = { relativeSize = v2(1, 0) } },
                textLine('below'),
            },
        },
    },
    {
        unorderedChildren = true,
        name = 'disabled_filter',
        layout = {
            layer = 'Windows', template = templates.disabled, props = { size = v2(50, 20) },
            content = ui.content { textLine('off') },
        },
    },
    {
        unorderedChildren = true,
        name = 'text_edit_line_and_box',
        layout = {
            layer = 'Windows', type = T.Flex,
            content = ui.content {
                { template = templates.textEditLine, props = { text = 'line', size = v2(100, 20) } },
                { template = templates.textEditBox, props = { text = 'box', size = v2(100, 40) } },
            },
        },
    },
}
