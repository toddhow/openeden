#ifndef CSM_MODULE_LABELSTROKE_H
#define CSM_MODULE_LABELSTROKE_H

#include <cstddef>
#include <vector>

#include <apps/opencs/model/module/overlaybuffer.hpp>
#include <apps/opencs/model/physics/debuglinedrawer.hpp>

namespace CSMModule
{
    /// Text labels as overlay lines (task 0091 stage d, toddhow's answer 2: one overlay path). A label is drawn with a
    /// built-in stroke font (the glyphs are line segments written in labelstroke.cpp), so it needs no font file, goes
    /// through the same geometry as every other overlay line, and is part of a captured frame. Pure: no Qt, no vsg.
    ///
    /// Screen space: the strokes lie in the plane facing the camera and are sized so that a character is
    /// kLabelPixelHeight pixels tall at any distance; the layout therefore depends on the camera and is done again
    /// when the camera moves (the view calls layoutLabels when the view matrix changed).

    /// Character height on screen, in pixels, and the other design values of the font (in glyph units, where a glyph is
    /// kGlyphWidth wide and kGlyphHeight tall).
    inline constexpr double kLabelPixelHeight = 14.0;
    inline constexpr double kGlyphWidth = 4.0;
    inline constexpr double kGlyphHeight = 6.0;
    /// The distance from one character's start to the next, in glyph widths.
    inline constexpr double kGlyphAdvance = 1.5;
    /// Where the text starts relative to its anchor point, in pixels (right and up), so that it does not sit on the
    /// point it names.
    inline constexpr double kLabelOffsetPixels = 6.0;
    /// Most lines all labels of one drawing may add (they share the view's line capacity with the other overlay lines).
    inline constexpr std::size_t kMaxLabelLines = 8192;

    /// The camera, in the world's unit (the same as the label positions).
    struct LabelCamera
    {
        double mEye[3] = { 0, 0, 0 };
        double mRight[3] = { 1, 0, 0 };
        double mUp[3] = { 0, 0, 1 };
        double mForward[3] = { 0, 1, 0 };
        /// World length of one pixel per unit of distance along mForward: 2 * tan(fov / 2) / viewport height.
        double mPixelSlope = 0.001;
    };

    /// Whether the font has a glyph of its own for the character (lower case counts as upper case). Any other
    /// printable ASCII character draws as a question mark box.
    bool hasGlyph(char c);
    /// The number of line segments of the character's glyph (0 for a space).
    std::size_t glyphSegmentCount(char c);

    /// Appends the lines of one label to \a out, at most \a maxLines in total in \a out. \return how many were added.
    /// A label behind the camera (or on its plane) adds none.
    std::size_t appendLabelLines(const OverlayLabel& label, const LabelCamera& camera,
        std::vector<CSMPhysics::DebugLine>& out, std::size_t maxLines);

    /// All labels, in order, up to kMaxLabelLines lines in total (the rest are dropped).
    std::vector<CSMPhysics::DebugLine> layoutLabels(const std::vector<OverlayLabel>& labels, const LabelCamera& camera);

    /// Adds the lines of data.mLabels to data.mLines (not past kMaxOverlayLines in all). Does not touch the labels.
    void addLabelLines(OverlayData& data, const LabelCamera& camera);
}

#endif
