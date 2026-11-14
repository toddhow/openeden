#ifndef OPENEDEN_RENDER_GROUNDCOVERTILT_H
#define OPENEDEN_RENDER_GROUNDCOVERTILT_H

#include <array>
#include <cmath>

namespace Render::GroundcoverTilt
{
    /// A 3 x 3 rotation, row-major: m[row][column], applied as m * v.
    using Matrix3 = std::array<std::array<float, 3>, 3>;

    /// The CPU twin of `groundcoverTiltRotation` in files/shaders/lib/groundcover.glsl and .slang (task 0153 follow-up
    /// 2): a yaw about Z followed by the turn that takes +Z to the unit normal (tilt x, tilt y, sqrt(1 - |tilt|^2)),
    /// by Rodrigues' formula R = I + [k]x + [k]x^2 / (1 + n.z) with k = z x n. Change the three together.
    inline Matrix3 rotation(float tiltX, float tiltY, float yaw)
    {
        const float cy = std::cos(yaw);
        const float sy = std::sin(yaw);
        const Matrix3 yawRotation{ { { cy, -sy, 0.f }, { sy, cy, 0.f }, { 0.f, 0.f, 1.f } } };
        const float nz = std::sqrt(std::fmax(0.f, 1.f - (tiltX * tiltX + tiltY * tiltY)));
        const float kx = -tiltY;
        const float ky = tiltX;
        const float kz = 0.f;
        const Matrix3 K{ { { 0.f, -kz, ky }, { kz, 0.f, -kx }, { -ky, kx, 0.f } } };
        Matrix3 tilt{};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
            {
                float k2 = 0.f;
                for (int i = 0; i < 3; ++i)
                    k2 += K[r][i] * K[i][c];
                tilt[r][c] = (r == c ? 1.f : 0.f) + K[r][c] + k2 / (1.f + nz);
            }
        Matrix3 out{};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                for (int i = 0; i < 3; ++i)
                    out[r][c] += tilt[r][i] * yawRotation[i][c];
        return out;
    }
}

#endif
