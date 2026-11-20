#include <components/havokpack/animation.hpp>
#include <components/havokpack/idle.hpp>
#include <components/havokpack/idlefidget.hpp>
#include <components/havokpack/packfile.hpp>
#include <components/havokpack/skeleton.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

// Task 0361, opt-in: OPENEDEN_HAVOK_FIDGET_DIR is a folder holding skeleton.hkx and the male mt_idle.hkx and every
// variant of Fidget::kVariants as <name>.hkx (bsatool extract). Measures how far each variant moves any bone from the
// standing idle (the largest rotation difference, in degrees, over the clip) and how far the idle itself moves: the
// numbers that say whether a variant can be seen at third person distance.
namespace
{
    using namespace HavokPack;

    std::vector<std::uint8_t> readFile(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        EXPECT_TRUE(in.good()) << path;
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    float angleDegrees(const Transform& a, const Transform& b)
    {
        float dot = 0.f;
        for (int i = 0; i < 4; ++i)
            dot += a.mRotation[i] * b.mRotation[i];
        dot = std::min(1.f, std::abs(dot));
        return 2.f * std::acos(dot) * 57.29578f;
    }

    TEST(HavokIdleFidgetRealData, how_far_each_variant_moves_the_body_from_the_standing_idle)
    {
        const char* dir = std::getenv("OPENEDEN_HAVOK_FIDGET_DIR");
        if (dir == nullptr)
            GTEST_SKIP() << "OPENEDEN_HAVOK_FIDGET_DIR is not set";
        const std::string root = dir;
        const auto skeletons = readSkeletons(Packfile::parse(readFile(root + "/skeleton.hkx")));
        ASSERT_FALSE(skeletons.empty());
        const auto bind = [&](const std::string& name) {
            auto clips = readClips(Packfile::parse(readFile(root + "/" + name + ".hkx")));
            EXPECT_FALSE(clips.empty()) << name;
            return bindClip(skeletons[0], std::move(clips[0]));
        };
        const BoundClip idle = bind("mt_idle");
        std::vector<Transform> a(idle.mClip.mNumTransformTracks), b(a.size());
        float idleRange = 0.f;
        samplePose(idle, 0.f, a);
        for (int i = 1; i <= 100; ++i)
        {
            samplePose(idle, idle.mClip.mDuration * static_cast<float>(i) / 100.f, b);
            for (std::size_t t = 0; t < a.size(); ++t)
                idleRange = std::max(idleRange, angleDegrees(a[t], b[t]));
        }
        std::cout << "FIDGET mt_idle duration " << idle.mClip.mDuration << " s, largest bone move over its loop "
                  << idleRange << " degrees" << std::endl;
        for (const std::string_view variant : Fidget::kVariants)
        {
            const BoundClip clip = bind(std::string(variant));
            std::vector<Transform> v(clip.mClip.mNumTransformTracks);
            if (v.size() != a.size())
            {
                std::cout << "FIDGET " << variant << " has another track count" << std::endl;
                continue;
            }
            float most = 0.f;
            double sum = 0.0;
            int n = 0;
            for (int i = 0; i <= 100; ++i)
            {
                const float t = clip.mClip.mDuration * static_cast<float>(i) / 100.f;
                samplePose(clip, t, v);
                samplePose(idle, t, a);
                float frame = 0.f;
                for (std::size_t k = 0; k < v.size(); ++k)
                    frame = std::max(frame, angleDegrees(v[k], a[k]));
                most = std::max(most, frame);
                sum += frame;
                ++n;
            }
            std::cout << "FIDGET " << variant << " duration " << clip.mClip.mDuration << " s, largest bone difference from the idle "
                      << most << " degrees, mean of the frame maxima " << sum / n << std::endl;
            EXPECT_GT(clip.mClip.mDuration, 0.5f);
        }
    }
}
