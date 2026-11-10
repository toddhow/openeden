#include "debug_draw_frame.hpp"

#include "debug_draw_service.hpp"

#include <algorithm>
#include <cstring>

namespace openeden::extension
{
    namespace
    {
        // Copies at most `cap` items of `source` through `convert` into `target` (already reserved to its cap, so
        // push_back never allocates) and returns how many did not fit.
        template <typename Source, typename Target, typename Convert>
        std::size_t copyUpTo(
            const std::vector<Source>& source, std::vector<Target>& target, std::size_t cap, Convert&& convert)
        {
            const std::size_t count = std::min(source.size(), cap);
            for (std::size_t i = 0; i < count; ++i)
                target.push_back(convert(source[i]));
            return source.size() - count;
        }
    }

    DebugDrawFrame::DebugDrawFrame()
    {
        mPoints.reserve(MaxPoints);
        mLines.reserve(MaxLines);
        mBoxes.reserve(MaxBoxes);
        mSpheres.reserve(MaxSpheres);
        mLabels.reserve(MaxLabels);
    }

    void DebugDrawFrame::collect(const DebugDrawServiceImpl& service)
    {
        clear();
        service.withPrimitives(
            [this](const std::vector<openeden::sdk::DebugPoint>& points,
                const std::vector<openeden::sdk::DebugLine>& lines, const std::vector<openeden::sdk::DebugBox>& boxes,
                const std::vector<openeden::sdk::DebugSphere>& spheres,
                const std::vector<openeden::sdk::DebugText>& texts) {
                mDropped.points = copyUpTo(points, mPoints, MaxPoints, [](const openeden::sdk::DebugPoint& p) {
                    return Point{ p.position, p.color, p.size, p.depthTest };
                });
                mDropped.lines = copyUpTo(lines, mLines, MaxLines, [](const openeden::sdk::DebugLine& l) {
                    return Line{ l.start, l.end, l.color, l.depthTest };
                });
                mDropped.boxes = copyUpTo(boxes, mBoxes, MaxBoxes, [](const openeden::sdk::DebugBox& b) {
                    return Box{ b.min, b.max, b.color, b.depthTest };
                });
                mDropped.spheres = copyUpTo(spheres, mSpheres, MaxSpheres, [](const openeden::sdk::DebugSphere& s) {
                    return Sphere{ s.center, s.radius, s.color, s.depthTest };
                });
                mDropped.labels = copyUpTo(texts, mLabels, MaxLabels, [](const openeden::sdk::DebugText& t) {
                    Label label;
                    label.position = t.position;
                    label.color = t.color;
                    label.depthTest = t.depthTest;
                    const std::size_t length = std::min(t.text.size(), MaxLabelChars);
                    std::memcpy(label.text, t.text.data(), length);
                    label.text[length] = '\0';
                    return label;
                });
            });
        if (mDropped.total() > 0)
            ++mOverflowFrames;
    }

    void DebugDrawFrame::clear() noexcept
    {
        mPoints.clear();
        mLines.clear();
        mBoxes.clear();
        mSpheres.clear();
        mLabels.clear();
        mDropped = {};
    }
}
