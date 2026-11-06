#include "overlaybuffer.hpp"

#include <algorithm>
#include <cmath>

namespace CSMModule
{
    using namespace openeden::editor;

    namespace
    {
        // How far a point's cross reaches each way, in meters (a point has no size in the world).
        constexpr double cPointCrossHalfLengthM = 0.05;

        float clamp01(float value)
        {
            return std::min(1.f, std::max(0.f, value));
        }
    }

    OverlayBuffer::OverlayBuffer(const float origin[3], double unitsPerMeter)
        : mOrigin{ origin[0], origin[1], origin[2] }
        , mUnitsPerMeter(unitsPerMeter)
    {
    }

    bool OverlayBuffer::accepts(const Vec3& p) const
    {
        // Compared as "not within": a NaN is within nothing.
        return std::abs(p.x) <= kMaxOverlayCoordinateMetres && std::abs(p.y) <= kMaxOverlayCoordinateMetres
            && std::abs(p.z) <= kMaxOverlayCoordinateMetres;
    }

    bool OverlayBuffer::accepts(const Colour& c) const
    {
        return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b) && std::isfinite(c.a);
    }

    void OverlayBuffer::convert(const Vec3& meters, float out[3]) const
    {
        out[0] = static_cast<float>(meters.x * mUnitsPerMeter + mOrigin[0]);
        out[1] = static_cast<float>(meters.y * mUnitsPerMeter + mOrigin[1]);
        out[2] = static_cast<float>(meters.z * mUnitsPerMeter + mOrigin[2]);
    }

    void OverlayBuffer::addLine(const Vec3& a, const Vec3& b, const Colour& colour)
    {
        if (!accepts(a) || !accepts(b) || !accepts(colour))
        {
            ++mData.mRefused;
            return;
        }
        if (mData.mLines.size() >= kMaxOverlayLines)
        {
            ++mData.mDropped;
            return;
        }
        CSMPhysics::DebugLine line;
        convert(a, line.mFrom);
        convert(b, line.mTo);
        line.mColour[0] = clamp01(colour.r);
        line.mColour[1] = clamp01(colour.g);
        line.mColour[2] = clamp01(colour.b);
        line.mColour[3] = clamp01(colour.a);
        mData.mLines.push_back(line);
    }

    void OverlayBuffer::line(const Vec3& a, const Vec3& b, const Colour& colour)
    {
        addLine(a, b, colour);
    }

    void OverlayBuffer::point(const Vec3& p, const Colour& colour, float)
    {
        if (!accepts(p) || !accepts(colour))
        {
            ++mData.mRefused;
            return;
        }
        const double r = cPointCrossHalfLengthM;
        addLine({ p.x - r, p.y, p.z }, { p.x + r, p.y, p.z }, colour);
        addLine({ p.x, p.y - r, p.z }, { p.x, p.y + r, p.z }, colour);
        addLine({ p.x, p.y, p.z - r }, { p.x, p.y, p.z + r }, colour);
    }

    void OverlayBuffer::label(const Vec3& p, std::string_view text, const Colour& colour)
    {
        // Printable ASCII only: the stroke font has no more, and a label is data a module chose.
        const bool printable = std::all_of(text.begin(), text.end(),
            [](char c) { return static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7F; });
        if (text.empty() || text.size() > kMaxLabelLength || !printable || !accepts(p) || !accepts(colour))
        {
            ++mData.mRefused;
            return;
        }
        if (mData.mLabels.size() >= kMaxOverlayLabels)
        {
            ++mData.mDropped;
            return;
        }
        OverlayLabel label;
        convert(p, label.mPosition);
        label.mText.reserve(text.size());
        for (const char c : text)
            label.mText.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
        label.mColour[0] = clamp01(colour.r);
        label.mColour[1] = clamp01(colour.g);
        label.mColour[2] = clamp01(colour.b);
        label.mColour[3] = clamp01(colour.a);
        mData.mLabels.push_back(std::move(label));
    }

    void OverlayBuffer::triangle(const Vec3& a, const Vec3& b, const Vec3& c, const Colour& colour, OverlayDepth depth)
    {
        if (!accepts(a) || !accepts(b) || !accepts(c) || !accepts(colour))
        {
            ++mData.mRefused;
            return;
        }
        if (mData.mTriangles.size() >= kMaxOverlayTriangles)
        {
            ++mData.mDropped;
            return;
        }
        OverlayTriangle triangle;
        convert(a, triangle.mVertices[0]);
        convert(b, triangle.mVertices[1]);
        convert(c, triangle.mVertices[2]);
        triangle.mColour[0] = clamp01(colour.r);
        triangle.mColour[1] = clamp01(colour.g);
        triangle.mColour[2] = clamp01(colour.b);
        triangle.mColour[3] = clamp01(colour.a);
        triangle.mAlwaysOnTop = depth == OverlayDepth::AlwaysOnTop;
        mData.mTriangles.push_back(triangle);
    }
}
