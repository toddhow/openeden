#pragma once

// The arbiter (M5d, C1, docs/tasks/0024): a state machine with weighted
// per-body-part blending (Animated, Standing, Leaning, Stepping, Disturbed,
// Stumbling, Falling, Fallen/Down, GettingUp) and dynamic strictness. It runs
// no physics and knows no behaviors; BalanceDriver feeds it and acts on its state.
//
// Ownership: a plain value, owned by BalanceDriver.

#include "opas/rig/Profile.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace opas::arbiter
{

    // Unified character and body-part state vocabulary (Task 0024, C1a).
    enum class ArbiterState : std::uint8_t
    {
        Animated,
        Standing,
        Leaning,
        Stepping,
        Disturbed,
        Stumbling,
        Falling,
        Fallen,
        Down,
        GettingUp
    };

    // Backward-compatible alias for existing M5d code.
    using CharacterState = ArbiterState;

    // Canonical string name for a state (e.g. "Standing", "Leaning", "Disturbed", etc.).
    const char* stateName(ArbiterState state);

    // Default nominal tracking strictness in [0.0, 1.0] for a state (Task 0024, C1b).
    double defaultStrictness(ArbiterState state);

    // Humanoid anatomical body part partition (Task 0024, C1a).
    enum class BodyPart : std::uint8_t
    {
        Head = 0,
        Torso = 1,
        LeftArm = 2,
        RightArm = 3,
        LeftLeg = 4,
        RightLeg = 5,
        Count = 6
    };

    // "Head", "Torso", "LeftArm", "RightArm", "LeftLeg", or "RightLeg".
    const char* bodyPartName(BodyPart part);

    // Parses exact canonical body part name; nullopt for unknown or invalid.
    std::optional<BodyPart> parseBodyPart(std::string_view name);

    // Classifies a bone into a BodyPart based on standard humanoid naming conventions.
    std::optional<BodyPart> classifyBone(std::string_view boneName);

    struct ArbiterInputs
    {
        bool balancing = false; // the driver's phase is Leaning or Settling
        bool stepping = false; // its phase is Stepping
        bool stumbling = false; // active stumble recovery stepping
        bool gaveUp = false; // its phase is GaveUp
        bool supported = false; // the reading is valid and has support
        double comSpeedMps = 0.0; // horizontal centre-of-mass speed
        double pelvisTiltRad = 0.0; // against the pelvis body's rest rotation
        double maxBodySpeedMps = 0.0; // the fastest body's linear speed
        double dtS = 0.0;
    };

    class Arbiter
    {
    public:
        // Fails, with a message, without the profile's `arbiter` object.
        static std::optional<Arbiter> create(const rig::Profile& profile, std::string* error = nullptr);

        // One step of the rules, in order (docs/tasks/0017, docs/tasks/0024):
        // Fallen stays; Falling becomes Fallen after resting; Falling on giving up or tilt;
        // Stepping; Standing when balancing, supported and slow; else Leaning.
        // Also updates localized body part disturbances and recoveries.
        ArbiterState update(const ArbiterInputs& inputs);

        [[nodiscard]] ArbiterState overallState() const { return overallState_; }
        [[nodiscard]] CharacterState state() const { return overallState_; }
        // Time since the last state change over blendTimeS, capped at 1.
        [[nodiscard]] double blendFraction() const { return overallBlendFraction_; }
        [[nodiscard]] double blendTimeS() const { return tuning_.blendTimeS; }

        // Per-body-part inspection
        [[nodiscard]] ArbiterState partState(BodyPart part) const;
        [[nodiscard]] double partBlendFraction(BodyPart part) const;
        [[nodiscard]] double partStrictness(BodyPart part) const;

        // Sets targeted body part to Disturbed. durationS <= 0 uses default recovery time.
        void disturbPart(BodyPart part, double durationS = 0.0);

        // Sets tracking strictness immediately and updates override target.
        void setPartStrictness(BodyPart part, double strictness);

        // Overrides tracking strictness for a specific body part; nullopt clears override.
        void setStrictnessOverride(BodyPart part, std::optional<double> strictness);
        [[nodiscard]] std::optional<double> strictnessOverride(BodyPart part) const;

    private:
        Arbiter();

        rig::ArbiterTuning tuning_;
        ArbiterState overallState_ = ArbiterState::Standing;
        double overallBlendFraction_ = 1.0;

        std::array<ArbiterState, static_cast<std::size_t>(BodyPart::Count)> partStates_;
        std::array<double, static_cast<std::size_t>(BodyPart::Count)> partBlendFractions_;
        std::array<double, static_cast<std::size_t>(BodyPart::Count)> disturbRemainingS_{};
        std::array<double, static_cast<std::size_t>(BodyPart::Count)> partStrictness_;
        std::array<std::optional<double>, static_cast<std::size_t>(BodyPart::Count)> strictnessOverrides_;

        double restingS_ = 0.0; // time resting while Falling
        double slowS_ = 0.0; // time the Standing conditions have held
    };

} // namespace opas::arbiter
