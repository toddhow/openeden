// Task 0153 follow-up 2: the maths of the neutral grass's tilt rotation (the CPU twin of the shaders' function).
#include <components/render/groundcovertilt.hpp>

#include <gtest/gtest.h>

namespace
{
    using Render::GroundcoverTilt::Matrix3;
    using Render::GroundcoverTilt::rotation;

    std::array<float, 3> apply(const Matrix3& m, float x, float y, float z)
    {
        return { m[0][0] * x + m[0][1] * y + m[0][2] * z, m[1][0] * x + m[1][1] * y + m[1][2] * z,
            m[2][0] * x + m[2][1] * y + m[2][2] * z };
    }

    float dot(const std::array<float, 3>& a, const std::array<float, 3>& b)
    {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    }

    TEST(GroundcoverTilt, withNoTiltItIsAYawAboutZ)
    {
        const float yaw = 0.7f;
        const auto x = apply(rotation(0.f, 0.f, yaw), 1.f, 0.f, 0.f);
        EXPECT_NEAR(x[0], std::cos(yaw), 1e-6f);
        EXPECT_NEAR(x[1], std::sin(yaw), 1e-6f);
        EXPECT_NEAR(x[2], 0.f, 1e-6f);
        const auto z = apply(rotation(0.f, 0.f, yaw), 0.f, 0.f, 1.f);
        EXPECT_NEAR(z[2], 1.f, 1e-6f);
    }

    TEST(GroundcoverTilt, upGoesToTheGivenNormalWhateverTheYaw)
    {
        for (float yaw : { 0.f, 1.f, 2.5f, -3.f })
            for (auto [tx, ty] : { std::pair{ 0.3f, 0.f }, std::pair{ -0.2f, 0.4f }, std::pair{ 0.f, -0.8f } })
            {
                const auto up = apply(rotation(tx, ty, yaw), 0.f, 0.f, 1.f);
                EXPECT_NEAR(up[0], tx, 1e-5f);
                EXPECT_NEAR(up[1], ty, 1e-5f);
                EXPECT_NEAR(up[2], std::sqrt(1.f - tx * tx - ty * ty), 1e-5f);
            }
    }

    TEST(GroundcoverTilt, itIsARotationLengthsAndAnglesAreKept)
    {
        const Matrix3 m = rotation(-0.35f, 0.45f, 1.9f);
        const auto a = apply(m, 1.f, 0.f, 0.f);
        const auto b = apply(m, 0.f, 1.f, 0.f);
        const auto c = apply(m, 0.f, 0.f, 1.f);
        EXPECT_NEAR(dot(a, a), 1.f, 1e-5f);
        EXPECT_NEAR(dot(b, b), 1.f, 1e-5f);
        EXPECT_NEAR(dot(c, c), 1.f, 1e-5f);
        EXPECT_NEAR(dot(a, b), 0.f, 1e-5f);
        EXPECT_NEAR(dot(a, c), 0.f, 1e-5f);
        EXPECT_NEAR(dot(b, c), 0.f, 1e-5f);
        // Right handed: a x b = c.
        EXPECT_NEAR(a[1] * b[2] - a[2] * b[1], c[0], 1e-5f);
        EXPECT_NEAR(a[2] * b[0] - a[0] * b[2], c[1], 1e-5f);
        EXPECT_NEAR(a[0] * b[1] - a[1] * b[0], c[2], 1e-5f);
    }

    TEST(GroundcoverTilt, theYawIsKeptAboutTheTiltedUp)
    {
        // The tuft's local +X, turned by yaw about up and then tilted, keeps its angle to the tilted-from-+X direction.
        const auto tilted0 = apply(rotation(0.4f, 0.f, 0.f), 1.f, 0.f, 0.f);
        const auto tilted1 = apply(rotation(0.4f, 0.f, 1.2f), 1.f, 0.f, 0.f);
        EXPECT_NEAR(dot(tilted0, tilted1), std::cos(1.2f), 1e-5f);
    }
}
