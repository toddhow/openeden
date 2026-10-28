#pragma once

// Context-Driven Style Controller (milestone C5, task 0023).
// Maps game context (health, encumbrance, alertness, cold) to dynamic style parameters.
// Idempotent, reversible, zero heap allocations on hot path.

#include "opas/style/StyleProfile.hpp"

namespace opas::style {

struct GameContext {
    double health = 1.0;       // [0.0, 1.0]: 1.0 = healthy, 0.0 = incapacitated
    double encumbrance = 0.0;  // [0.0, 1.0]: 0.0 = unburdened, 1.0 = max load
    double alertness = 0.0;    // [0.0, 1.0]: 0.0 = relaxed, 1.0 = high tension
    double cold = 0.0;         // [0.0, 1.0]: 0.0 = comfortable, 1.0 = freezing

    bool operator==(const GameContext&) const = default;
};

class ContextController {
public:
    explicit ContextController(double smoothingRateHz = 5.0);

    // Updates filtered context towards targetContext over dt seconds.
    void update(const GameContext& targetContext, double dt);

    // Direct setter without smoothing (e.g. initialization or teleport).
    void setContextImmediate(const GameContext& context);

    [[nodiscard]] const GameContext& currentContext() const { return current_; }
    [[nodiscard]] const GameContext& targetContext() const { return target_; }

    // Pre-allocates / populates slots in outEffective so that applyContext makes zero heap allocations.
    static void prepareEffectiveProfile(const StyleProfile& baseProfile, StyleProfile& outEffective);

    // Evaluates context mappings from baseProfile into outEffective.
    // Idempotent and reversible. Zero heap allocations if outEffective was prepared with prepareEffectiveProfile.
    void applyContext(const StyleProfile& baseProfile, StyleProfile& outEffective) const;

    // Evaluates context mappings and returns effective style profile.
    [[nodiscard]] StyleProfile computeEffectiveProfile(const StyleProfile& baseProfile) const;

private:
    GameContext target_{};
    GameContext current_{};
    double smoothingRateHz_ = 5.0;
};

} // namespace opas::style
