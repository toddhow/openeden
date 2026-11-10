#pragma once

#include <openeden/debug_draw.hpp>

#include <cstddef>
#include <vector>

namespace openeden::extension
{
    class DebugDrawServiceImpl;

    /**
     * @brief What the core draws of the extensions' debug submissions in one frame (task 0017 stage 5, W3 leftover).
     *
     * In plain words: extensions hand points, lines, boxes, spheres and labels to the DebugDrawService as data; the
     * service keeps them until the end of the frame. Just before that end, the engine calls collect() once, which
     * copies them into this object's fixed-size lists, and the renderer then turns the lists into lines. Nothing here
     * renders and nothing here allocates after construction: every list is reserved to its cap, and a primitive past
     * the cap is dropped and counted, never stored.
     *
     * Owned by the engine's extension host. collect() runs on the main thread inside the Ui phase (the service
     * itself is locked, so extension worker threads submitting at the same moment are safe). Positions stay in the
     * SDK's unit (meters); the renderer scales them to the game's world once, at its boundary.
     */
    class DebugDrawFrame
    {
    public:
        // Caps per frame. The worst case of every list full is about 103,000 line segments, which the renderer's two
        // debug geometries (one depth tested, one always on top) hold.
        static constexpr std::size_t MaxPoints = 4096;
        static constexpr std::size_t MaxLines = 16384;
        static constexpr std::size_t MaxBoxes = 2048;
        static constexpr std::size_t MaxSpheres = 1024;
        static constexpr std::size_t MaxLabels = 256;
        /// Longer label text is cut here (the label keeps its first characters).
        static constexpr std::size_t MaxLabelChars = 63;

        struct Point
        {
            openeden::sdk::Vector3 position;
            openeden::sdk::Color4f color;
            float size = 1.0f;
            bool depthTest = true;
        };

        struct Line
        {
            openeden::sdk::Vector3 from;
            openeden::sdk::Vector3 to;
            openeden::sdk::Color4f color;
            bool depthTest = true;
        };

        struct Box
        {
            openeden::sdk::Vector3 min;
            openeden::sdk::Vector3 max;
            openeden::sdk::Color4f color;
            bool depthTest = true;
        };

        struct Sphere
        {
            openeden::sdk::Vector3 center;
            float radius = 1.0f;
            openeden::sdk::Color4f color;
            bool depthTest = true;
        };

        struct Label
        {
            openeden::sdk::Vector3 position;
            openeden::sdk::Color4f color;
            bool depthTest = false;
            char text[MaxLabelChars + 1] = {};
        };

        /// How many primitives the last collect() found beyond the caps, per list.
        struct Dropped
        {
            std::size_t points = 0;
            std::size_t lines = 0;
            std::size_t boxes = 0;
            std::size_t spheres = 0;
            std::size_t labels = 0;
            [[nodiscard]] std::size_t total() const noexcept { return points + lines + boxes + spheres + labels; }
        };

        /// Reserves every list to its cap. This is the only allocation this class makes.
        DebugDrawFrame();

        /// Replaces the contents with the service's current primitives, up to the caps. Allocates nothing.
        void collect(const DebugDrawServiceImpl& service);

        void clear() noexcept;

        [[nodiscard]] const std::vector<Point>& points() const noexcept { return mPoints; }
        [[nodiscard]] const std::vector<Line>& lines() const noexcept { return mLines; }
        [[nodiscard]] const std::vector<Box>& boxes() const noexcept { return mBoxes; }
        [[nodiscard]] const std::vector<Sphere>& spheres() const noexcept { return mSpheres; }
        [[nodiscard]] const std::vector<Label>& labels() const noexcept { return mLabels; }

        [[nodiscard]] const Dropped& dropped() const noexcept { return mDropped; }

        [[nodiscard]] std::size_t primitiveCount() const noexcept
        {
            return mPoints.size() + mLines.size() + mBoxes.size() + mSpheres.size() + mLabels.size();
        }

        /// How many collect() calls dropped anything (the host logs the first one).
        [[nodiscard]] std::size_t overflowFrames() const noexcept { return mOverflowFrames; }

    private:
        std::vector<Point> mPoints;
        std::vector<Line> mLines;
        std::vector<Box> mBoxes;
        std::vector<Sphere> mSpheres;
        std::vector<Label> mLabels;
        Dropped mDropped;
        std::size_t mOverflowFrames = 0;
    };
}
