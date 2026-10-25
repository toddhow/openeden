#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::sdk
{
    struct Vector3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct Color4f
    {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    struct DebugPoint
    {
        std::string pluginId;
        Vector3 position;
        Color4f color;
        float size = 1.0f;
        bool depthTest = true;
        float duration = 0.0f;
    };

    struct DebugLine
    {
        std::string pluginId;
        Vector3 start;
        Vector3 end;
        Color4f color;
        float lineWidth = 1.0f;
        bool depthTest = true;
        float duration = 0.0f;
    };

    struct DebugBox
    {
        std::string pluginId;
        Vector3 min;
        Vector3 max;
        Color4f color;
        bool depthTest = true;
        float duration = 0.0f;
    };

    struct DebugSphere
    {
        std::string pluginId;
        Vector3 center;
        float radius = 1.0f;
        Color4f color;
        bool depthTest = true;
        float duration = 0.0f;
    };

    struct DebugText
    {
        std::string pluginId;
        Vector3 position;
        std::string text;
        Color4f color;
        float scale = 1.0f;
        bool depthTest = false;
        float duration = 0.0f;
    };

    struct DebugSnapshot
    {
        std::vector<DebugPoint> points;
        std::vector<DebugLine> lines;
        std::vector<DebugBox> boxes;
        std::vector<DebugSphere> spheres;
        std::vector<DebugText> texts;

        [[nodiscard]] bool empty() const noexcept
        {
            return points.empty() && lines.empty() && boxes.empty() && spheres.empty() && texts.empty();
        }

        [[nodiscard]] std::size_t totalCount() const noexcept
        {
            return points.size() + lines.size() + boxes.size() + spheres.size() + texts.size();
        }
    };

    /**
     * @brief Service managing decoupled debug overlay geometry submissions.
     */
    class DebugDrawService
    {
    public:
        virtual ~DebugDrawService() = default;

        virtual void drawPoint(std::string_view pluginId, const Vector3& pos, const Color4f& color, float size = 1.0f,
            bool depthTest = true, float duration = 0.0f)
            = 0;

        virtual void drawLine(std::string_view pluginId, const Vector3& from, const Vector3& to, const Color4f& color,
            float lineWidth = 1.0f, bool depthTest = true, float duration = 0.0f)
            = 0;

        virtual void drawBox(std::string_view pluginId, const Vector3& min, const Vector3& max, const Color4f& color,
            bool depthTest = true, float duration = 0.0f)
            = 0;

        virtual void drawSphere(std::string_view pluginId, const Vector3& center, float radius, const Color4f& color,
            bool depthTest = true, float duration = 0.0f)
            = 0;

        virtual void drawText(std::string_view pluginId, const Vector3& pos, std::string_view text,
            const Color4f& color, float scale = 1.0f, bool depthTest = false, float duration = 0.0f)
            = 0;

        [[nodiscard]] virtual DebugSnapshot extractSnapshot() const = 0;
        virtual void endFrame(float dt = 0.0f) = 0;
        virtual void clear() = 0;
        virtual void clearPlugin(std::string_view pluginId) = 0;
    };
} // namespace openeden::sdk
