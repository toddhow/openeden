#include "labelstroke.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

namespace CSMModule
{
    namespace
    {
        // The stroke font: each glyph is one or more polylines on a 4 x 6 grid (x to the right, y up, origin at the
        // bottom left of the character). A polyline is "x,y x,y ..." and ';' starts the next one. Written for this
        // project (nothing copied from a font), the shapes are plain capitals as a plotter would draw them.
        struct GlyphSource
        {
            char mCharacter;
            const char* mStrokes;
        };

        const GlyphSource sGlyphSources[] = {
            { 'A', "0,0 0,4 2,6 4,4 4,0; 0,3 4,3" },
            { 'B', "0,0 0,6 3,6 4,5 4,4 3,3 0,3; 3,3 4,2 4,1 3,0 0,0" },
            { 'C', "4,5 3,6 1,6 0,5 0,1 1,0 3,0 4,1" },
            { 'D', "0,0 0,6 3,6 4,5 4,1 3,0 0,0" },
            { 'E', "4,6 0,6 0,0 4,0; 0,3 3,3" },
            { 'F', "4,6 0,6 0,0; 0,3 3,3" },
            { 'G', "4,5 3,6 1,6 0,5 0,1 1,0 3,0 4,1 4,3 2,3" },
            { 'H', "0,0 0,6; 4,0 4,6; 0,3 4,3" },
            { 'I', "1,6 3,6; 2,6 2,0; 1,0 3,0" },
            { 'J', "4,6 4,1 3,0 1,0 0,1" },
            { 'K', "0,0 0,6; 4,6 0,3 4,0" },
            { 'L', "0,6 0,0 4,0" },
            { 'M', "0,0 0,6 2,3 4,6 4,0" },
            { 'N', "0,0 0,6 4,0 4,6" },
            { 'O', "1,0 0,1 0,5 1,6 3,6 4,5 4,1 3,0 1,0" },
            { 'P', "0,0 0,6 3,6 4,5 4,4 3,3 0,3" },
            { 'Q', "1,0 0,1 0,5 1,6 3,6 4,5 4,1 3,0 1,0; 2,2 4,0" },
            { 'R', "0,0 0,6 3,6 4,5 4,4 3,3 0,3; 2,3 4,0" },
            { 'S', "4,5 3,6 1,6 0,5 0,4 1,3 3,3 4,2 4,1 3,0 1,0 0,1" },
            { 'T', "0,6 4,6; 2,6 2,0" },
            { 'U', "0,6 0,1 1,0 3,0 4,1 4,6" },
            { 'V', "0,6 2,0 4,6" },
            { 'W', "0,6 1,0 2,3 3,0 4,6" },
            { 'X', "0,6 4,0; 4,6 0,0" },
            { 'Y', "0,6 2,3 4,6; 2,3 2,0" },
            { 'Z', "0,6 4,6 0,0 4,0" },
            { '0', "1,0 0,1 0,5 1,6 3,6 4,5 4,1 3,0 1,0; 0,1 4,5" },
            { '1', "1,5 2,6 2,0; 1,0 3,0" },
            { '2', "0,5 1,6 3,6 4,5 4,4 0,0 4,0" },
            { '3', "0,5 1,6 3,6 4,5 4,4 3,3 1,3; 3,3 4,2 4,1 3,0 1,0 0,1" },
            { '4', "3,0 3,6 0,2 4,2" },
            { '5', "4,6 0,6 0,3 3,3 4,2 4,1 3,0 1,0 0,1" },
            { '6', "4,5 3,6 1,6 0,5 0,1 1,0 3,0 4,1 4,2 3,3 0,3" },
            { '7', "0,6 4,6 1,0" },
            { '8', "1,3 0,4 0,5 1,6 3,6 4,5 4,4 3,3 1,3 0,2 0,1 1,0 3,0 4,1 4,2 3,3" },
            { '9', "0,1 1,0 3,0 4,1 4,5 3,6 1,6 0,5 0,4 1,3 4,3" },
            { '.', "1,0 2,0 2,1 1,1 1,0" },
            { '_', "0,0 4,0" },
            { '-', "0,3 4,3" },
            { ':', "2,1 2,2; 2,4 2,5" },
            { '/', "0,0 4,6" },
            { '+', "0,3 4,3; 2,1 2,5" },
            { '?', "0,5 1,6 3,6 4,5 4,4 2,3 2,2; 2,0 2,1" },
            { ' ', "" },
        };

        struct Segment
        {
            float mX0, mY0, mX1, mY1;
        };

        struct Font
        {
            std::vector<Segment> mGlyphs[128];
            bool mKnown[128] = {};

            Font()
            {
                for (const GlyphSource& source : sGlyphSources)
                {
                    const unsigned char index = static_cast<unsigned char>(source.mCharacter);
                    mKnown[index] = true;
                    std::vector<Segment>& segments = mGlyphs[index];
                    const char* p = source.mStrokes;
                    bool havePrevious = false;
                    float px = 0, py = 0;
                    while (*p != '\0')
                    {
                        if (*p == ' ')
                        {
                            ++p;
                            continue;
                        }
                        if (*p == ';')
                        {
                            havePrevious = false;
                            ++p;
                            continue;
                        }
                        char* end = nullptr;
                        const float x = std::strtof(p, &end);
                        p = end + 1; // the comma
                        const float y = std::strtof(p, &end);
                        p = end;
                        if (havePrevious)
                            segments.push_back({ px, py, x, y });
                        px = x;
                        py = y;
                        havePrevious = true;
                    }
                }
            }
        };

        const Font& font()
        {
            static const Font sFont;
            return sFont;
        }

        /// The glyph that draws \a c: itself, its upper case, or the question mark.
        const std::vector<Segment>& glyphOf(char c)
        {
            const Font& f = font();
            unsigned char u = static_cast<unsigned char>(c);
            if (u >= 'a' && u <= 'z')
                u = static_cast<unsigned char>(u - 'a' + 'A');
            if (u < 128 && f.mKnown[u])
                return f.mGlyphs[u];
            return f.mGlyphs[static_cast<unsigned char>('?')];
        }
    }

    bool hasGlyph(char c)
    {
        unsigned char u = static_cast<unsigned char>(c);
        if (u >= 'a' && u <= 'z')
            u = static_cast<unsigned char>(u - 'a' + 'A');
        return u < 128 && font().mKnown[u];
    }

    std::size_t glyphSegmentCount(char c)
    {
        return glyphOf(c).size();
    }

    std::size_t appendLabelLines(const OverlayLabel& label, const LabelCamera& camera,
        std::vector<CSMPhysics::DebugLine>& out, std::size_t maxLines)
    {
        // Depth along the view direction decides the size of a pixel at the anchor.
        double toAnchor[3];
        double depth = 0.0;
        for (int i = 0; i < 3; ++i)
        {
            toAnchor[i] = static_cast<double>(label.mPosition[i]) - camera.mEye[i];
            depth += toAnchor[i] * camera.mForward[i];
        }
        if (!(depth > 0.0) || !std::isfinite(depth))
            return 0;

        const double pixelWorld = camera.mPixelSlope * depth;
        const double unit = kLabelPixelHeight / kGlyphHeight * pixelWorld; // one glyph grid step, in world units
        const double advance = kGlyphAdvance * kGlyphWidth * unit;
        const double originRight = kLabelOffsetPixels * pixelWorld;
        const double originUp = kLabelOffsetPixels * pixelWorld;

        std::size_t added = 0;
        double cursor = originRight;
        for (const char c : label.mText)
        {
            for (const Segment& segment : glyphOf(c))
            {
                if (out.size() >= maxLines)
                    return added;
                CSMPhysics::DebugLine line;
                const double r0 = cursor + segment.mX0 * unit;
                const double u0 = originUp + segment.mY0 * unit;
                const double r1 = cursor + segment.mX1 * unit;
                const double u1 = originUp + segment.mY1 * unit;
                for (int i = 0; i < 3; ++i)
                {
                    line.mFrom[i] = static_cast<float>(label.mPosition[i] + camera.mRight[i] * r0 + camera.mUp[i] * u0);
                    line.mTo[i] = static_cast<float>(label.mPosition[i] + camera.mRight[i] * r1 + camera.mUp[i] * u1);
                }
                for (int i = 0; i < 4; ++i)
                    line.mColour[i] = label.mColour[i];
                out.push_back(line);
                ++added;
            }
            cursor += advance;
        }
        return added;
    }

    std::vector<CSMPhysics::DebugLine> layoutLabels(const std::vector<OverlayLabel>& labels, const LabelCamera& camera)
    {
        std::vector<CSMPhysics::DebugLine> lines;
        for (const OverlayLabel& label : labels)
            appendLabelLines(label, camera, lines, kMaxLabelLines);
        return lines;
    }

    void addLabelLines(OverlayData& data, const LabelCamera& camera)
    {
        std::vector<CSMPhysics::DebugLine> labelLines = layoutLabels(data.mLabels, camera);
        const std::size_t room = data.mLines.size() < kMaxOverlayLines ? kMaxOverlayLines - data.mLines.size() : 0;
        if (labelLines.size() > room)
            labelLines.resize(room);
        data.mLines.insert(data.mLines.end(), labelLines.begin(), labelLines.end());
    }
}
