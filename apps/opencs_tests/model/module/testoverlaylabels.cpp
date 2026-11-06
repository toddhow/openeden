// Task 0091, stage d: labels. Written from the spec alone, against stubs.
//
// Two parts. OverlayBuffer::label stores a label in the world's unit (module meters * units per meter + origin),
// upper-cases it, refuses empty text, text over kMaxLabelLength, a character that is not printable ASCII and a
// non-finite or far-out position or colour (all counted in mRefused), clamps the colour, and drops labels past
// kMaxOverlayLabels (counted in mDropped).
//
// The stroke font (labelstroke.hpp) turns a label into lines for a camera: every glyph is made of line segments inside
// its 4 x 6 box; strokes lie in the plane facing the camera; a character is kLabelPixelHeight pixels tall whatever the
// distance; the text starts kLabelOffsetPixels right of and above the anchor and advances kGlyphAdvance glyph widths
// per character; lower case equals upper case; unknown printable characters draw as a box with a question mark; a label
// behind the camera draws nothing; the total is capped.
#include "apps/opencs/model/module/labelstroke.hpp"
#include "apps/opencs/model/module/overlaybuffer.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace
{
    using namespace CSMModule;
    using openeden::editor::Colour;
    using openeden::editor::Vec3;

    constexpr float kOrigin[3] = { 100.f, 200.f, 300.f };
    constexpr double kUnitsPerMeter = 100.0;

    // ---- OverlayBuffer --------------------------------------------------------------------------------

    TEST(CSMOverlayLabels, aLabelIsStoredInWorldUnitsUpperCasedWithAClampedColour)
    {
        OverlayBuffer buffer(kOrigin, kUnitsPerMeter);
        buffer.label({ 1.0, 2.0, 3.0 }, "Spine_01", { 2.0f, -1.0f, 0.5f, 1.0f });
        const OverlayData& data = buffer.data();
        ASSERT_EQ(data.mLabels.size(), 1u);
        EXPECT_FLOAT_EQ(data.mLabels[0].mPosition[0], 200.f);
        EXPECT_FLOAT_EQ(data.mLabels[0].mPosition[1], 400.f);
        EXPECT_FLOAT_EQ(data.mLabels[0].mPosition[2], 600.f);
        EXPECT_EQ(data.mLabels[0].mText, "SPINE_01");
        EXPECT_FLOAT_EQ(data.mLabels[0].mColour[0], 1.f);
        EXPECT_FLOAT_EQ(data.mLabels[0].mColour[1], 0.f);
        EXPECT_FLOAT_EQ(data.mLabels[0].mColour[2], 0.5f);
        EXPECT_EQ(data.mRefused, 0u);
        EXPECT_TRUE(data.mLines.empty()); // the text is not turned into lines here
    }

    TEST(CSMOverlayLabels, badLabelsAreRefusedAndCounted)
    {
        OverlayBuffer buffer(kOrigin, kUnitsPerMeter);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        buffer.label({ 0, 0, 0 }, "", { 1, 1, 1, 1 }); // empty
        buffer.label({ 0, 0, 0 }, std::string(kMaxLabelLength + 1, 'A'), { 1, 1, 1, 1 }); // too long
        buffer.label({ 0, 0, 0 }, "caf\xC3\xA9", { 1, 1, 1, 1 }); // not ASCII
        buffer.label({ 0, 0, 0 }, "tab\there", { 1, 1, 1, 1 }); // a control character
        buffer.label({ nan, 0, 0 }, "X", { 1, 1, 1, 1 });
        buffer.label({ 2.0e6, 0, 0 }, "X", { 1, 1, 1, 1 }); // beyond kMaxOverlayCoordinateMetres
        buffer.label({ 0, 0, 0 }, "X", { nan, 1, 1, 1 });
        EXPECT_TRUE(buffer.data().mLabels.empty());
        EXPECT_EQ(buffer.data().mRefused, 7u);
        // The longest allowed text is accepted.
        buffer.label({ 0, 0, 0 }, std::string(kMaxLabelLength, 'A'), { 1, 1, 1, 1 });
        EXPECT_EQ(buffer.data().mLabels.size(), 1u);
    }

    TEST(CSMOverlayLabels, labelsPastTheCapAreDroppedAndCounted)
    {
        OverlayBuffer buffer(kOrigin, kUnitsPerMeter);
        for (std::size_t i = 0; i < kMaxOverlayLabels + 5; ++i)
            buffer.label({ 0, 0, 0 }, "A", { 1, 1, 1, 1 });
        EXPECT_EQ(buffer.data().mLabels.size(), kMaxOverlayLabels);
        EXPECT_EQ(buffer.data().mDropped, 5u);
    }

    // ---- the stroke font ------------------------------------------------------------------------------

    LabelCamera lookingAlongY(double distance)
    {
        LabelCamera camera;
        camera.mEye[0] = 0;
        camera.mEye[1] = -distance;
        camera.mEye[2] = 0;
        // right = +x, up = +z, forward = +y
        camera.mRight[0] = 1;
        camera.mRight[1] = 0;
        camera.mRight[2] = 0;
        camera.mUp[0] = 0;
        camera.mUp[1] = 0;
        camera.mUp[2] = 1;
        camera.mForward[0] = 0;
        camera.mForward[1] = 1;
        camera.mForward[2] = 0;
        camera.mPixelSlope = 0.002;
        return camera;
    }

    OverlayLabel label(const std::string& text, double x = 0, double y = 0, double z = 0)
    {
        OverlayLabel result;
        result.mPosition[0] = static_cast<float>(x);
        result.mPosition[1] = static_cast<float>(y);
        result.mPosition[2] = static_cast<float>(z);
        result.mText = text;
        result.mColour[0] = 0.25f;
        result.mColour[1] = 0.5f;
        result.mColour[2] = 0.75f;
        result.mColour[3] = 1.f;
        return result;
    }

    struct Bounds
    {
        double minX = 1e300, maxX = -1e300, minZ = 1e300, maxZ = -1e300;
    };

    Bounds boundsOf(const std::vector<CSMPhysics::DebugLine>& lines)
    {
        Bounds b;
        for (const CSMPhysics::DebugLine& line : lines)
            for (const float* p : { line.mFrom, line.mTo })
            {
                b.minX = std::min<double>(b.minX, p[0]);
                b.maxX = std::max<double>(b.maxX, p[0]);
                b.minZ = std::min<double>(b.minZ, p[2]);
                b.maxZ = std::max<double>(b.maxZ, p[2]);
            }
        return b;
    }

    TEST(CSMLabelStroke, everyLetterDigitAndTheUsualPunctuationHasAGlyphAndLowerCaseSharesIt)
    {
        for (char c = 'A'; c <= 'Z'; ++c)
        {
            EXPECT_TRUE(hasGlyph(c)) << c;
            EXPECT_GT(glyphSegmentCount(c), 0u) << c;
            EXPECT_EQ(glyphSegmentCount(static_cast<char>(c + 32)), glyphSegmentCount(c)) << c;
            EXPECT_TRUE(hasGlyph(static_cast<char>(c + 32))) << c;
        }
        for (char c = '0'; c <= '9'; ++c)
            EXPECT_GT(glyphSegmentCount(c), 0u) << c;
        for (const char c : { '.', '_', '-', ':', '/', '+' })
            EXPECT_TRUE(hasGlyph(c)) << c;
        EXPECT_TRUE(hasGlyph(' '));
        EXPECT_EQ(glyphSegmentCount(' '), 0u);
        // Printable but without a glyph of its own: drawn as the question mark.
        EXPECT_FALSE(hasGlyph('~'));
        EXPECT_EQ(glyphSegmentCount('~'), glyphSegmentCount('?'));
    }

    TEST(CSMLabelStroke, aCharacterIsKLabelPixelHeightTallOnTheScreenAtAnyDistance)
    {
        for (const double distance : { 2.0, 5.0, 40.0 })
        {
            const LabelCamera camera = lookingAlongY(distance);
            std::vector<CSMPhysics::DebugLine> lines;
            ASSERT_GT(appendLabelLines(label("H"), camera, lines, 1000), 0u);
            const Bounds b = boundsOf(lines);
            const double pixelWorld = camera.mPixelSlope * distance;
            EXPECT_NEAR((b.maxZ - b.minZ) / pixelWorld, kLabelPixelHeight, 1e-3) << distance;
            EXPECT_NEAR((b.maxX - b.minX) / pixelWorld, kLabelPixelHeight * kGlyphWidth / kGlyphHeight, 1e-3)
                << distance;
        }
    }

    TEST(CSMLabelStroke, theTextStartsAtTheAnchorPlusTheOffsetAndAdvancesPerCharacter)
    {
        const LabelCamera camera = lookingAlongY(10.0);
        const double pixelWorld = camera.mPixelSlope * 10.0;
        std::vector<CSMPhysics::DebugLine> one;
        appendLabelLines(label("I", 3.0, 0.0, 4.0), camera, one, 1000);
        std::vector<CSMPhysics::DebugLine> two;
        appendLabelLines(label("II", 3.0, 0.0, 4.0), camera, two, 1000);
        const Bounds b1 = boundsOf(one);
        const Bounds b2 = boundsOf(two);
        // The bottom of the text sits kLabelOffsetPixels above the anchor.
        EXPECT_NEAR((b1.minZ - 4.0) / pixelWorld, kLabelOffsetPixels, 1e-3);
        // The second character starts one advance (kGlyphAdvance glyph widths) after the first.
        const double glyphWorldWidth = kLabelPixelHeight / kGlyphHeight * kGlyphWidth * pixelWorld;
        EXPECT_NEAR(b2.maxX - b1.maxX, glyphWorldWidth * kGlyphAdvance, 1e-6 * 100);
        EXPECT_NEAR(b2.minX, b1.minX, 1e-9);
        // The text begins to the right of the anchor.
        EXPECT_GE(b1.minX, 3.0 + kLabelOffsetPixels * pixelWorld - 1e-6);
    }

    TEST(CSMLabelStroke, theStrokesLieInThePlaneFacingTheCameraAndKeepTheColour)
    {
        const LabelCamera camera = lookingAlongY(10.0);
        std::vector<CSMPhysics::DebugLine> lines;
        appendLabelLines(label("MESH 1", 1.0, 5.0, 2.0), camera, lines, 1000);
        ASSERT_FALSE(lines.empty());
        for (const CSMPhysics::DebugLine& line : lines)
        {
            // The same depth as the anchor: the plane through it perpendicular to the view direction.
            EXPECT_NEAR(line.mFrom[1], 5.0, 1e-6);
            EXPECT_NEAR(line.mTo[1], 5.0, 1e-6);
            EXPECT_FLOAT_EQ(line.mColour[0], 0.25f);
            EXPECT_FLOAT_EQ(line.mColour[1], 0.5f);
            EXPECT_FLOAT_EQ(line.mColour[2], 0.75f);
            EXPECT_FLOAT_EQ(line.mColour[3], 1.f);
        }
    }

    TEST(CSMLabelStroke, aRotatedCameraRotatesTheText)
    {
        // Looking along +x with right = +y, up = +z: the strokes spread along y and z, at constant x.
        LabelCamera camera;
        camera.mEye[0] = -10;
        camera.mRight[0] = 0;
        camera.mRight[1] = 1;
        camera.mRight[2] = 0;
        camera.mUp[0] = 0;
        camera.mUp[1] = 0;
        camera.mUp[2] = 1;
        camera.mForward[0] = 1;
        camera.mForward[1] = 0;
        camera.mForward[2] = 0;
        camera.mPixelSlope = 0.002;
        std::vector<CSMPhysics::DebugLine> lines;
        appendLabelLines(label("T", 0, 0, 0), camera, lines, 1000);
        ASSERT_FALSE(lines.empty());
        for (const CSMPhysics::DebugLine& line : lines)
        {
            EXPECT_NEAR(line.mFrom[0], 0.0, 1e-6);
            EXPECT_NEAR(line.mTo[0], 0.0, 1e-6);
        }
        double maxY = -1e300;
        for (const CSMPhysics::DebugLine& line : lines)
            maxY = std::max<double>(maxY, line.mFrom[1]);
        EXPECT_GT(maxY, 0.0);
    }

    TEST(CSMLabelStroke, aSpaceAddsNothingButStillAdvances)
    {
        const LabelCamera camera = lookingAlongY(10.0);
        std::vector<CSMPhysics::DebugLine> tight;
        std::vector<CSMPhysics::DebugLine> spaced;
        appendLabelLines(label("II"), camera, tight, 1000);
        appendLabelLines(label("I I"), camera, spaced, 1000);
        EXPECT_EQ(tight.size(), spaced.size());
        EXPECT_GT(boundsOf(spaced).maxX, boundsOf(tight).maxX);
    }

    TEST(CSMLabelStroke, lowerCaseDrawsLikeUpperCaseAndAnUnknownCharacterLikeAQuestionMark)
    {
        const LabelCamera camera = lookingAlongY(10.0);
        std::vector<CSMPhysics::DebugLine> lower;
        std::vector<CSMPhysics::DebugLine> upper;
        appendLabelLines(label("bone"), camera, lower, 1000);
        appendLabelLines(label("BONE"), camera, upper, 1000);
        ASSERT_EQ(lower.size(), upper.size());
        for (std::size_t i = 0; i < lower.size(); ++i)
            EXPECT_EQ(lower[i].mFrom[0], upper[i].mFrom[0]);
        std::vector<CSMPhysics::DebugLine> tilde;
        std::vector<CSMPhysics::DebugLine> question;
        appendLabelLines(label("~"), camera, tilde, 1000);
        appendLabelLines(label("?"), camera, question, 1000);
        EXPECT_EQ(tilde.size(), question.size());
    }

    TEST(CSMLabelStroke, everyGlyphStaysInsideItsBoxSoCharactersNeverOverlap)
    {
        const LabelCamera camera = lookingAlongY(10.0);
        const double pixelWorld = camera.mPixelSlope * 10.0;
        const double glyphWidth = kLabelPixelHeight / kGlyphHeight * kGlyphWidth * pixelWorld;
        for (char c = 'A'; c <= 'Z'; ++c)
        {
            std::vector<CSMPhysics::DebugLine> lines;
            appendLabelLines(label(std::string(1, c)), camera, lines, 1000);
            const Bounds b = boundsOf(lines);
            EXPECT_LE(b.maxX - b.minX, glyphWidth + 1e-6) << c;
            EXPECT_LE(b.maxZ - b.minZ, kLabelPixelHeight * pixelWorld + 1e-6) << c;
        }
    }

    TEST(CSMLabelStroke, aLabelBehindTheCameraOrOnItsPlaneDrawsNothing)
    {
        const LabelCamera camera = lookingAlongY(10.0);
        std::vector<CSMPhysics::DebugLine> lines;
        EXPECT_EQ(appendLabelLines(label("A", 0, -20.0, 0), camera, lines, 1000), 0u); // behind
        EXPECT_EQ(appendLabelLines(label("A", 0, -10.0, 0), camera, lines, 1000), 0u); // on the camera's plane
        EXPECT_TRUE(lines.empty());
    }

    TEST(CSMLabelStroke, theLineCapStopsTheLabelAndTheCountIsWhatWasAdded)
    {
        const LabelCamera camera = lookingAlongY(10.0);
        std::vector<CSMPhysics::DebugLine> full;
        const std::size_t all = appendLabelLines(label("MESH"), camera, full, 100000);
        ASSERT_GT(all, 4u);
        std::vector<CSMPhysics::DebugLine> capped;
        EXPECT_EQ(appendLabelLines(label("MESH"), camera, capped, 4), 4u);
        EXPECT_EQ(capped.size(), 4u);
        // The cap counts what is already in the vector.
        EXPECT_EQ(appendLabelLines(label("MESH"), camera, capped, 6), 2u);
        EXPECT_EQ(capped.size(), 6u);
        EXPECT_EQ(appendLabelLines(label("MESH"), camera, capped, 6), 0u);
    }

    TEST(CSMLabelStroke, layoutLabelsDrawsAllInOrderAndNeverMoreThanKMaxLabelLines)
    {
        const LabelCamera camera = lookingAlongY(10.0);
        std::vector<OverlayLabel> labels{ label("A", 0, 0, 0), label("B", 1, 0, 0) };
        const std::vector<CSMPhysics::DebugLine> two = layoutLabels(labels, camera);
        std::vector<CSMPhysics::DebugLine> a;
        std::vector<CSMPhysics::DebugLine> b;
        appendLabelLines(labels[0], camera, a, 1000);
        appendLabelLines(labels[1], camera, b, 1000);
        EXPECT_EQ(two.size(), a.size() + b.size());

        std::vector<OverlayLabel> many(kMaxOverlayLabels, label(std::string(kMaxLabelLength, 'W')));
        EXPECT_LE(layoutLabels(many, camera).size(), kMaxLabelLines);
    }

    TEST(CSMLabelStroke, addLabelLinesAppendsToTheDataWithinTheLineCapAndKeepsTheLabels)
    {
        const LabelCamera camera = lookingAlongY(10.0);
        OverlayData data;
        CSMPhysics::DebugLine existing{};
        data.mLines.assign(3, existing);
        data.mLabels.push_back(label("OK"));
        addLabelLines(data, camera);
        EXPECT_GT(data.mLines.size(), 3u);
        EXPECT_EQ(data.mLabels.size(), 1u);

        OverlayData nearlyFull;
        nearlyFull.mLines.assign(kMaxOverlayLines - 2, existing);
        nearlyFull.mLabels.push_back(label("WWWW"));
        addLabelLines(nearlyFull, camera);
        EXPECT_EQ(nearlyFull.mLines.size(), kMaxOverlayLines);
    }
}
