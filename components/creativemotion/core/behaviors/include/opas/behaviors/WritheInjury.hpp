#pragma once

#include "opas/arbiter/Arbiter.hpp"
#include "opas/math/Quat.hpp"
#include "opas/math/Vec3.hpp"

#include <array>
#include <cstdint>

namespace opas::behaviors {

/// Configuration tuning parameters for organic writhe motion and injured limb tracking.
struct WritheTuning {
    double breathFrequencyHz = 0.25;        ///< Slow breathing cycle frequency (~4s period)
    double breathAmplitudeRad = 0.05;       ///< Subtle chest/spine breathing expansion amplitude (rad)
    double shiftFrequencyHz = 0.10;         ///< Posture shift frequency (~10s period)
    double shiftAmplitudeRad = 0.08;        ///< Posture shift rotation amplitude (rad)
    double injuredStrictness = 0.30;        ///< Clamped strictness for damaged/injured limbs
    double defaultFallenStrictness = 0.00;  ///< Nominal strictness for healthy limbs while fallen
};

/// Writhe and damaged limb compliance controller (Milestone C2 Stage C2d).
/// Generates deterministic low-stiffness organic breathing and shifting motion while fallen,
/// and maintains persistent compliant asymmetry on injured body parts across states.
class WritheInjuryController {
public:
    WritheInjuryController() = default;
    explicit WritheInjuryController(WritheTuning tuning);

    [[nodiscard]] const WritheTuning& tuning() const { return tuning_; }

    void setInjured(arbiter::BodyPart part, bool injured = true);
    [[nodiscard]] bool isInjured(arbiter::BodyPart part) const;
    void clearInjuries();

    [[nodiscard]] double partStrictness(arbiter::BodyPart part, arbiter::ArbiterState state) const;

    [[nodiscard]] math::Quat computeSpineWritheOffset(double timeSeconds,
                                                      std::uint32_t seed = 42) const;

    [[nodiscard]] math::Quat computeLimbWritheOffset(arbiter::BodyPart part, double timeSeconds,
                                                     std::uint32_t seed = 42) const;

    void update(double dtSeconds, arbiter::ArbiterState state);
    void reset();

private:
    WritheTuning tuning_{};
    std::array<bool, 6> injured_{}; // per BodyPart index
    double elapsedS_ = 0.0;
};

} // namespace opas::behaviors
