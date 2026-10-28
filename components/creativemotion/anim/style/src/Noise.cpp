#include "opas/style/Noise.hpp"

#include <cmath>

namespace opas::style {

namespace {

constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kHarmonics[4] = {1.0, 1.618033988749895, 2.718281828459045, 4.236067977499790};
constexpr double kWeights[4] = {0.50, 0.25, 0.15, 0.10};

} // namespace

double noise1D(double timeS, double freqHz, std::uint64_t seed, std::uint32_t channel)
{
    double val = 0.0;
    for (std::size_t k = 0; k < 4; ++k) {
        const std::uint64_t h = hash64(seed ^ (static_cast<std::uint64_t>(channel) << 16U) ^
                                       (static_cast<std::uint64_t>(k) * 0x9e3779b97f4a7c15ULL));
        const double phase = toUnitDouble(h) * kTwoPi;
        val += kWeights[k] * std::sin(kTwoPi * freqHz * kHarmonics[k] * timeS + phase);
    }
    return val;
}

math::Vec3 noise2D(double timeS, double freqHz, std::uint64_t seed, std::uint32_t channel)
{
    const double nx = noise1D(timeS, freqHz, seed, channel);
    const double ny = noise1D(timeS, freqHz, seed, channel + 1000U);
    return math::Vec3{nx, ny, 0.0};
}

} // namespace opas::style
