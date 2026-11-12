#include "skyrimtreesway.hpp"

#include <algorithm>
#include <cmath>

#include <components/lod/clustersway.hpp>
#include <components/render/cloudvolumemath.hpp>

namespace MWRender::SkyrimTreeSway
{
    float windScale(float baseWindSpeed)
    {
        const float speed = std::isfinite(baseWindSpeed) ? std::clamp(baseWindSpeed, 0.f, 1.f) : 0.f;
        return 0.3f + 0.7f * speed;
    }

    std::uint8_t weightByte(float alpha, float objectHeight)
    {
        if (!std::isfinite(alpha) || !std::isfinite(objectHeight) || objectHeight <= 0.f || alpha <= 0.f)
            return 0;
        const float a = std::min(alpha, 1.f);
        const float share = std::clamp(objectHeight * a * a / Lod::swayWeightScale, 0.f, 1.f);
        return static_cast<std::uint8_t>(std::lround(std::sqrt(share) * 255.f));
    }

    Driver::Driver()
        : mUniform(vsg::mat4Value::create())
    {
        // The matrix is all zero: no push, angle 0, no boost, until the first update. Written every frame, so dynamic.
        mUniform->value() = vsg::mat4(0.f);
        mUniform->properties.dataVariance = vsg::DataVariance::DYNAMIC_DATA;
    }

    void Driver::update(double seconds, float baseWindSpeed)
    {
        if (std::isfinite(seconds) && seconds > 0.0)
            mSeconds += seconds;
        // The cloud volume's wind direction, so trees lean the way the clouds drift (the engine has no other wind direction).
        const float push = lean * windScale(baseWindSpeed) * Lod::swayWeightScale;
        const float x = Render::CloudVolume::windDirection[0] * push;
        const float y = Render::CloudVolume::windDirection[1] * push;
        const float angle = Lod::swayClockAngle(mSeconds);
        auto& matrix = mUniform->value();
        if (matrix(0, 0) != x || matrix(0, 1) != y || matrix(0, 2) != angle || matrix(0, 3) != 0.f)
        {
            matrix(0, 0) = x;
            matrix(0, 1) = y;
            matrix(0, 2) = angle;
            matrix(0, 3) = 0.f;
            mUniform->dirty();
        }
    }
}
