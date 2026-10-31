#include "debug_draw_service.hpp"

#include <algorithm>

namespace openeden::extension
{
    DebugDrawServiceImpl::DebugDrawServiceImpl()
    {
        mPoints.reserve(DefaultCapacity);
        mLines.reserve(DefaultCapacity);
        mBoxes.reserve(DefaultCapacity);
        mSpheres.reserve(DefaultCapacity);
        mTexts.reserve(DefaultCapacity);
    }

    void DebugDrawServiceImpl::drawPoint(std::string_view extensionId, const openeden::sdk::Vector3& pos,
        const openeden::sdk::Color4f& color, float size, bool depthTest, float duration)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mPoints.size() < MaxPrimitivesPerCategory)
        {
            mPoints.push_back({ std::string(extensionId), pos, color, size, depthTest, duration });
        }
    }

    void DebugDrawServiceImpl::drawLine(std::string_view extensionId, const openeden::sdk::Vector3& from,
        const openeden::sdk::Vector3& to, const openeden::sdk::Color4f& color, float lineWidth, bool depthTest,
        float duration)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mLines.size() < MaxPrimitivesPerCategory)
        {
            mLines.push_back({ std::string(extensionId), from, to, color, lineWidth, depthTest, duration });
        }
    }

    void DebugDrawServiceImpl::drawBox(std::string_view extensionId, const openeden::sdk::Vector3& min,
        const openeden::sdk::Vector3& max, const openeden::sdk::Color4f& color, bool depthTest, float duration)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mBoxes.size() < MaxPrimitivesPerCategory)
        {
            mBoxes.push_back({ std::string(extensionId), min, max, color, depthTest, duration });
        }
    }

    void DebugDrawServiceImpl::drawSphere(std::string_view extensionId, const openeden::sdk::Vector3& center,
        float radius, const openeden::sdk::Color4f& color, bool depthTest, float duration)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mSpheres.size() < MaxPrimitivesPerCategory)
        {
            mSpheres.push_back({ std::string(extensionId), center, radius, color, depthTest, duration });
        }
    }

    void DebugDrawServiceImpl::drawText(std::string_view extensionId, const openeden::sdk::Vector3& pos,
        std::string_view text, const openeden::sdk::Color4f& color, float scale, bool depthTest, float duration)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mTexts.size() < MaxPrimitivesPerCategory)
        {
            mTexts.push_back({ std::string(extensionId), pos, std::string(text), color, scale, depthTest, duration });
        }
    }

    openeden::sdk::DebugSnapshot DebugDrawServiceImpl::extractSnapshot() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        openeden::sdk::DebugSnapshot snapshot;
        snapshot.points.reserve(mPoints.size());
        snapshot.points = mPoints;
        snapshot.lines.reserve(mLines.size());
        snapshot.lines = mLines;
        snapshot.boxes.reserve(mBoxes.size());
        snapshot.boxes = mBoxes;
        snapshot.spheres.reserve(mSpheres.size());
        snapshot.spheres = mSpheres;
        snapshot.texts.reserve(mTexts.size());
        snapshot.texts = mTexts;
        return snapshot;
    }

    template <typename T>
    void DebugDrawServiceImpl::advancePrimitives(std::vector<T>& primitives, float dt)
    {
        primitives.erase(std::remove_if(primitives.begin(), primitives.end(),
                             [dt](T& item) {
                                 if (item.duration <= 0.0f)
                                 {
                                     return true; // Transient: remove on frame boundary
                                 }
                                 item.duration -= dt;
                                 return item.duration <= 0.0f; // Expired persistent
                             }),
            primitives.end());
    }

    void DebugDrawServiceImpl::endFrame(float dt)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        advancePrimitives(mPoints, dt);
        advancePrimitives(mLines, dt);
        advancePrimitives(mBoxes, dt);
        advancePrimitives(mSpheres, dt);
        advancePrimitives(mTexts, dt);
    }

    void DebugDrawServiceImpl::clear()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mPoints.clear();
        mLines.clear();
        mBoxes.clear();
        mSpheres.clear();
        mTexts.clear();
    }

    void DebugDrawServiceImpl::clearExtension(std::string_view extensionId)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto match = [extensionId](const auto& item) { return item.extensionId == extensionId; };
        mPoints.erase(std::remove_if(mPoints.begin(), mPoints.end(), match), mPoints.end());
        mLines.erase(std::remove_if(mLines.begin(), mLines.end(), match), mLines.end());
        mBoxes.erase(std::remove_if(mBoxes.begin(), mBoxes.end(), match), mBoxes.end());
        mSpheres.erase(std::remove_if(mSpheres.begin(), mSpheres.end(), match), mSpheres.end());
        mTexts.erase(std::remove_if(mTexts.begin(), mTexts.end(), match), mTexts.end());
    }
} // namespace openeden::extension
