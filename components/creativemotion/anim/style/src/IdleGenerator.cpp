#include "opas/style/IdleGenerator.hpp"

#include "opas/style/Noise.hpp"

#include <algorithm>
#include <cmath>

namespace opas::style
{

    namespace
    {

        constexpr double kPi = 3.14159265358979323846;
        constexpr double kTwoPi = 6.28318530717958647692;

    } // namespace

    IdleGenerator::IdleGenerator(const IdleParams& params)
        : params_(params)
    {
    }

    IdlePoseOffset IdleGenerator::evaluate(double timeS) const
    {
        IdlePoseOffset out;

        // 1. Respiration (Breathing)
        if (params_.breathingAmplitudeM > 0.0 && params_.breathingRateHz > 0.0)
        {
            const double s = std::sin(kTwoPi * params_.breathingRateHz * timeS);
            // Sagittal chest expansion and subtle thoracic rise
            out.chestTranslationM
                = math::Vec3{ 0.0, params_.breathingAmplitudeM * s, 0.3 * params_.breathingAmplitudeM * s };
            const double pitchAngle = -0.015 * s * (params_.breathingAmplitudeM / 0.015);
            out.spinePitch = math::fromAxisAngle(math::Vec3{ 1.0, 0.0, 0.0 }, pitchAngle);
        }
        else
        {
            out.chestTranslationM = math::Vec3{ 0.0, 0.0, 0.0 };
            out.spinePitch = math::Quat{};
        }

        // 2. Pelvic Sway (Weight Shifts)
        if (params_.swayAmplitudeM > 0.0 && params_.swayRateHz > 0.0)
        {
            const double sx = params_.swayAmplitudeM * noise1D(timeS, params_.swayRateHz, params_.seed, 1U);
            const double sy = 0.5 * params_.swayAmplitudeM * noise1D(timeS, params_.swayRateHz * 0.8, params_.seed, 2U);
            const double sz = -0.1 * (sx * sx) / params_.swayAmplitudeM;
            out.pelvisTranslationM = math::Vec3{ sx, sy, sz };

            // Pelvic roll and pitch accompanying weight shifts
            const double roll = -(sx / params_.swayAmplitudeM) * 0.02;
            const double pitch = (sy / params_.swayAmplitudeM) * 0.015;
            const math::Quat qRoll = math::fromAxisAngle(math::Vec3{ 0.0, 1.0, 0.0 }, roll);
            const math::Quat qPitch = math::fromAxisAngle(math::Vec3{ 1.0, 0.0, 0.0 }, pitch);
            out.pelvisTilt = math::normalized(qRoll * qPitch);
        }
        else
        {
            out.pelvisTranslationM = math::Vec3{ 0.0, 0.0, 0.0 };
            out.pelvisTilt = math::Quat{};
        }

        // 3. Micro-Fidgets
        if (params_.fidgetIntervalS > 0.0)
        {
            const double interval = params_.fidgetIntervalS;
            const double pulseWidth = std::min(2.0, 0.4 * interval);
            const auto k = static_cast<std::int64_t>(std::floor(timeS / interval));
            const std::uint64_t hk = hash64(params_.seed ^ (static_cast<std::uint64_t>(k) * 0x517cc1b727220a95ULL));

            const double uStart = toUnitDouble(hk) * (interval - pulseWidth);
            const double tk = static_cast<double>(k) * interval + uStart;

            if (timeS >= tk && timeS < tk + pulseWidth)
            {
                const double tau = (timeS - tk) / pulseWidth;
                const double s = std::sin(kPi * tau);
                const double b = s * s; // C^1 smooth bell: b(0)=0, b'(0)=0, b(1)=0, b'(1)=0

                const double psi = toSignedDouble(hash64(hk ^ 0x12345ULL)) * 0.15 * b; // Glance yaw
                const double theta = toSignedDouble(hash64(hk ^ 0x6789aULL)) * 0.08 * b; // Glance pitch

                const math::Quat qYaw = math::fromAxisAngle(math::Vec3{ 0.0, 0.0, 1.0 }, psi);
                const math::Quat qPitch = math::fromAxisAngle(math::Vec3{ 1.0, 0.0, 0.0 }, theta);
                out.headGlanceOffset = math::normalized(qYaw * qPitch);

                const double shoulderZ = toSignedDouble(hash64(hk ^ 0xbcdeULL)) * 0.008 * b;
                out.shoulderShiftM = math::Vec3{ 0.0, 0.0, shoulderZ };
            }
            else
            {
                out.headGlanceOffset = math::Quat{};
                out.shoulderShiftM = math::Vec3{ 0.0, 0.0, 0.0 };
            }
        }
        else
        {
            out.headGlanceOffset = math::Quat{};
            out.shoulderShiftM = math::Vec3{ 0.0, 0.0, 0.0 };
        }

        return out;
    }

} // namespace opas::style
