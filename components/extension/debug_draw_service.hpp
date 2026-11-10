#pragma once

#include <openeden/debug_draw.hpp>

#include <mutex>
#include <string_view>
#include <vector>

namespace openeden::extension
{
    /**
     * @brief Engine implementation of decoupled debug overlay submission service.
     */
    class DebugDrawServiceImpl : public openeden::sdk::DebugDrawService
    {
    public:
        static constexpr std::size_t DefaultCapacity = 4096;
        static constexpr std::size_t MaxPrimitivesPerCategory = 32768;

        DebugDrawServiceImpl();
        ~DebugDrawServiceImpl() override = default;

        // Non-copyable
        DebugDrawServiceImpl(const DebugDrawServiceImpl&) = delete;
        DebugDrawServiceImpl& operator=(const DebugDrawServiceImpl&) = delete;

        // Movable
        DebugDrawServiceImpl(DebugDrawServiceImpl&&) noexcept = default;
        DebugDrawServiceImpl& operator=(DebugDrawServiceImpl&&) noexcept = default;

        void drawPoint(std::string_view extensionId, const openeden::sdk::Vector3& pos,
            const openeden::sdk::Color4f& color, float size = 1.0f, bool depthTest = true,
            float duration = 0.0f) override;

        void drawLine(std::string_view extensionId, const openeden::sdk::Vector3& from,
            const openeden::sdk::Vector3& to, const openeden::sdk::Color4f& color, float lineWidth = 1.0f,
            bool depthTest = true, float duration = 0.0f) override;

        void drawBox(std::string_view extensionId, const openeden::sdk::Vector3& min, const openeden::sdk::Vector3& max,
            const openeden::sdk::Color4f& color, bool depthTest = true, float duration = 0.0f) override;

        void drawSphere(std::string_view extensionId, const openeden::sdk::Vector3& center, float radius,
            const openeden::sdk::Color4f& color, bool depthTest = true, float duration = 0.0f) override;

        void drawText(std::string_view extensionId, const openeden::sdk::Vector3& pos, std::string_view text,
            const openeden::sdk::Color4f& color, float scale = 1.0f, bool depthTest = false,
            float duration = 0.0f) override;

        [[nodiscard]] openeden::sdk::DebugSnapshot extractSnapshot() const override;

        void endFrame(float dt = 0.0f) override;

        void clear() override;

        void clearExtension(std::string_view extensionId) override;

        /// Calls `visitor(points, lines, boxes, spheres, texts)` with the stored primitives while the service is
        /// locked, without copying them (the engine's per-frame collection, DebugDrawFrame). The visitor must not call
        /// back into the service.
        template <typename Visitor>
        void withPrimitives(Visitor&& visitor) const
        {
            std::lock_guard<std::mutex> lock(mMutex);
            visitor(mPoints, mLines, mBoxes, mSpheres, mTexts);
        }

    private:
        template <typename T>
        void advancePrimitives(std::vector<T>& primitives, float dt);

        mutable std::mutex mMutex;
        std::vector<openeden::sdk::DebugPoint> mPoints;
        std::vector<openeden::sdk::DebugLine> mLines;
        std::vector<openeden::sdk::DebugBox> mBoxes;
        std::vector<openeden::sdk::DebugSphere> mSpheres;
        std::vector<openeden::sdk::DebugText> mTexts;
    };
} // namespace openeden::extension
