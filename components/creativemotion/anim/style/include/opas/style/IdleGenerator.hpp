#pragma once

// Procedural Idle Life generator (milestone C5, task 0023).
// Evaluates continuous breathing, pelvic sway, and micro-fidgets.
// Multi-rate invariant, bit-for-bit deterministic, zero heap allocations on hot path.

#include "opas/math/Quat.hpp"
#include "opas/math/Vec3.hpp"
#include "opas/style/StyleProfile.hpp"

namespace opas::style {

struct IdlePoseOffset {
    // Breathing offsets
    math::Vec3 chestTranslationM{0.0, 0.0, 0.0};
    math::Quat spinePitch{}; // identity: {x=0, y=0, z=0, w=1}

    // Pelvic sway offsets
    math::Vec3 pelvisTranslationM{0.0, 0.0, 0.0};
    math::Quat pelvisTilt{};

    // Micro-fidget offsets
    math::Quat headGlanceOffset{};
    math::Vec3 shoulderShiftM{0.0, 0.0, 0.0};

    bool operator==(const IdlePoseOffset&) const = default;
};

class IdleGenerator {
public:
    explicit IdleGenerator(const IdleParams& params = IdleParams{});

    void setParams(const IdleParams& params) { params_ = params; }
    [[nodiscard]] const IdleParams& params() const { return params_; }

    // Evaluates idle offsets at continuous time timeS (in seconds).
    // Pure function of (params, timeS) with 0 heap allocations.
    [[nodiscard]] IdlePoseOffset evaluate(double timeS) const;

private:
    IdleParams params_;
};

} // namespace opas::style
