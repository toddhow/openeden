#include "opas/arbiter/Arbiter.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <utility>

namespace opas::arbiter {

const char* stateName(ArbiterState state)
{
    switch (state) {
    case ArbiterState::Animated:
        return "Animated";
    case ArbiterState::Standing:
        return "Standing";
    case ArbiterState::Leaning:
        return "Leaning";
    case ArbiterState::Stepping:
        return "Stepping";
    case ArbiterState::Disturbed:
        return "Disturbed";
    case ArbiterState::Stumbling:
        return "Stumbling";
    case ArbiterState::Falling:
        return "Falling";
    case ArbiterState::Fallen:
        return "Fallen";
    case ArbiterState::Down:
        return "Down";
    case ArbiterState::GettingUp:
        return "GettingUp";
    }
    return "";
}

double defaultStrictness(ArbiterState state)
{
    switch (state) {
    case ArbiterState::Animated:
    case ArbiterState::Standing:
    case ArbiterState::GettingUp:
        return 1.0;
    case ArbiterState::Leaning:
        return 0.95;
    case ArbiterState::Stepping:
        return 0.90;
    case ArbiterState::Disturbed:
        return 0.20;
    case ArbiterState::Stumbling:
        return 0.50;
    case ArbiterState::Falling:
    case ArbiterState::Fallen:
    case ArbiterState::Down:
        return 0.00;
    }
    return 1.0;
}

const char* bodyPartName(BodyPart part)
{
    switch (part) {
    case BodyPart::Head:
        return "Head";
    case BodyPart::Torso:
        return "Torso";
    case BodyPart::LeftArm:
        return "LeftArm";
    case BodyPart::RightArm:
        return "RightArm";
    case BodyPart::LeftLeg:
        return "LeftLeg";
    case BodyPart::RightLeg:
        return "RightLeg";
    case BodyPart::Count:
        break;
    }
    return "";
}

std::optional<BodyPart> parseBodyPart(std::string_view name)
{
    if (name == "Head") {
        return BodyPart::Head;
    }
    if (name == "Torso") {
        return BodyPart::Torso;
    }
    if (name == "LeftArm") {
        return BodyPart::LeftArm;
    }
    if (name == "RightArm") {
        return BodyPart::RightArm;
    }
    if (name == "LeftLeg") {
        return BodyPart::LeftLeg;
    }
    if (name == "RightLeg") {
        return BodyPart::RightLeg;
    }
    return std::nullopt;
}

std::optional<BodyPart> classifyBone(std::string_view boneName)
{
    if (boneName.empty()) {
        return std::nullopt;
    }

    std::string lower;
    lower.reserve(boneName.size());
    for (char c : boneName) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    // Filter out common non-body props / root / camera bones
    if (lower == "root" || lower.find("root") != std::string::npos ||
        lower.find("weapon") != std::string::npos ||
        lower.find("camera") != std::string::npos ||
        lower.find("prop") != std::string::npos ||
        lower.find("shield") != std::string::npos ||
        lower.find("unknown") != std::string::npos) {
        return std::nullopt;
    }

    const auto isLeft = [](std::string_view s) {
        if (s.rfind("left", 0) == 0) return true;
        if (s.find("left") != std::string_view::npos) return true;
        if (s.ends_with("_l") || s.ends_with(".l")) return true;
        if (s.rfind("l_", 0) == 0 || s.rfind("l.", 0) == 0) return true;
        if (s.find("_l_") != std::string_view::npos || s.find(".l.") != std::string_view::npos) return true;
        return false;
    };

    const auto isRight = [](std::string_view s) {
        if (s.rfind("right", 0) == 0) return true;
        if (s.find("right") != std::string_view::npos) return true;
        if (s.ends_with("_r") || s.ends_with(".r")) return true;
        if (s.rfind("r_", 0) == 0 || s.rfind("r.", 0) == 0) return true;
        if (s.find("_r_") != std::string_view::npos || s.find(".r.") != std::string_view::npos) return true;
        return false;
    };

    const bool hasLeft = isLeft(lower);
    const bool hasRight = isRight(lower);

    if (hasLeft && !hasRight) {
        if (lower.find("arm") != std::string::npos ||
            lower.find("shoulder") != std::string::npos ||
            lower.find("hand") != std::string::npos ||
            lower.find("forearm") != std::string::npos ||
            lower.find("wrist") != std::string::npos ||
            lower.find("finger") != std::string::npos ||
            lower.find("elbow") != std::string::npos) {
            return BodyPart::LeftArm;
        }
        if (lower.find("leg") != std::string::npos ||
            lower.find("thigh") != std::string::npos ||
            lower.find("calf") != std::string::npos ||
            lower.find("shin") != std::string::npos ||
            lower.find("foot") != std::string::npos ||
            lower.find("toe") != std::string::npos ||
            lower.find("ankle") != std::string::npos ||
            lower.find("knee") != std::string::npos ||
            lower.find("upleg") != std::string::npos) {
            return BodyPart::LeftLeg;
        }
        return std::nullopt;
    }

    if (hasRight && !hasLeft) {
        if (lower.find("arm") != std::string::npos ||
            lower.find("shoulder") != std::string::npos ||
            lower.find("hand") != std::string::npos ||
            lower.find("forearm") != std::string::npos ||
            lower.find("wrist") != std::string::npos ||
            lower.find("finger") != std::string::npos ||
            lower.find("elbow") != std::string::npos) {
            return BodyPart::RightArm;
        }
        if (lower.find("leg") != std::string::npos ||
            lower.find("thigh") != std::string::npos ||
            lower.find("calf") != std::string::npos ||
            lower.find("shin") != std::string::npos ||
            lower.find("foot") != std::string::npos ||
            lower.find("toe") != std::string::npos ||
            lower.find("ankle") != std::string::npos ||
            lower.find("knee") != std::string::npos ||
            lower.find("upleg") != std::string::npos) {
            return BodyPart::RightLeg;
        }
        return std::nullopt;
    }

    // Midline structures (Head & Neck)
    if (lower == "head" || lower.find("head") != std::string::npos ||
        lower == "neck" || lower.find("neck") != std::string::npos ||
        lower == "jaw" || lower.find("jaw") != std::string::npos ||
        lower == "skull" || lower.find("eye") != std::string::npos) {
        return BodyPart::Head;
    }

    // Midline structures (Torso: Pelvis, Hips, Spine, Chest, Abdomen)
    if (lower == "hips" || lower.find("hips") != std::string::npos ||
        lower == "pelvis" || lower.find("pelvis") != std::string::npos ||
        lower == "spine" || lower.find("spine") != std::string::npos ||
        lower == "chest" || lower.find("chest") != std::string::npos ||
        lower == "torso" || lower.find("torso") != std::string::npos ||
        lower == "abdomen") {
        return BodyPart::Torso;
    }

    return std::nullopt;
}

Arbiter::Arbiter()
{
    partStates_.fill(ArbiterState::Standing);
    partBlendFractions_.fill(1.0);
    disturbRemainingS_.fill(0.0);
    partStrictness_.fill(1.0);
    strictnessOverrides_.fill(std::nullopt);
}

std::optional<Arbiter> Arbiter::create(const rig::Profile& profile, std::string* error)
{
    if (!profile.arbiter) {
        if (error != nullptr) {
            *error = "the profile has no arbiter tuning (profile version 2, \"arbiter\")";
        }
        return std::nullopt;
    }
    Arbiter arbiter;
    arbiter.tuning_ = *profile.arbiter;
    return arbiter;
}

ArbiterState Arbiter::partState(BodyPart part) const
{
    const auto idx = static_cast<std::size_t>(part);
    if (idx < partStates_.size()) {
        return partStates_[idx];
    }
    return overallState_;
}

double Arbiter::partBlendFraction(BodyPart part) const
{
    const auto idx = static_cast<std::size_t>(part);
    if (idx < partBlendFractions_.size()) {
        return partBlendFractions_[idx];
    }
    return overallBlendFraction_;
}

double Arbiter::partStrictness(BodyPart part) const
{
    const auto idx = static_cast<std::size_t>(part);
    if (idx < partStrictness_.size()) {
        return partStrictness_[idx];
    }
    return 1.0;
}

void Arbiter::setStrictnessOverride(BodyPart part, std::optional<double> strictness)
{
    const auto idx = static_cast<std::size_t>(part);
    if (idx >= strictnessOverrides_.size()) {
        return;
    }
    if (!strictness.has_value()) {
        strictnessOverrides_[idx] = std::nullopt;
        return;
    }
    double val = *strictness;
    if (std::isnan(val)) {
        return;
    }
    if (std::isinf(val)) {
        val = val > 0.0 ? 1.0 : 0.0;
    } else {
        val = std::clamp(val, 0.0, 1.0);
    }
    strictnessOverrides_[idx] = val;
}

void Arbiter::setPartStrictness(BodyPart part, double strictness)
{
    const auto idx = static_cast<std::size_t>(part);
    if (idx >= partStrictness_.size()) {
        return;
    }
    double val = strictness;
    if (std::isnan(val)) {
        return;
    }
    if (std::isinf(val)) {
        val = val > 0.0 ? 1.0 : 0.0;
    } else {
        val = std::clamp(val, 0.0, 1.0);
    }
    partStrictness_[idx] = val;
    strictnessOverrides_[idx] = val;
}

std::optional<double> Arbiter::strictnessOverride(BodyPart part) const
{
    const auto idx = static_cast<std::size_t>(part);
    if (idx < strictnessOverrides_.size()) {
        return strictnessOverrides_[idx];
    }
    return std::nullopt;
}

void Arbiter::disturbPart(BodyPart part, double durationS)
{
    const auto idx = static_cast<std::size_t>(part);
    if (idx < partStates_.size()) {
        partStates_[idx] = ArbiterState::Disturbed;
        partBlendFractions_[idx] = 0.0;
        disturbRemainingS_[idx] =
            (durationS > 0.0) ? durationS : (tuning_.blendTimeS > 0.0 ? tuning_.blendTimeS : 0.25);
    }
}

ArbiterState Arbiter::update(const ArbiterInputs& in)
{
    // The standing conditions, timed on every update (any update where they
    // do not hold resets the count).
    const bool slow = in.balancing && in.supported && in.comSpeedMps <= tuning_.standingSpeedMps;
    slowS_ = slow ? slowS_ + in.dtS : 0.0;

    ArbiterState baseState = overallState_;
    if (overallState_ == ArbiterState::Fallen || overallState_ == ArbiterState::Down) {
        baseState = overallState_;
    } else if (overallState_ == ArbiterState::Falling) {
        // Resting: every body slower than restSpeedMps, for restTimeS in a
        // row of updates made while Falling.
        restingS_ = in.maxBodySpeedMps < tuning_.restSpeedMps ? restingS_ + in.dtS : 0.0;
        if (restingS_ >= tuning_.restTimeS) {
            baseState = ArbiterState::Fallen;
        } else {
            baseState = ArbiterState::Falling;
        }
    } else if (in.gaveUp || in.pelvisTiltRad > tuning_.fallTiltRad) {
        baseState = ArbiterState::Falling;
        restingS_ = 0.0;
    } else if (in.stumbling) {
        baseState = ArbiterState::Stumbling;
    } else if (in.stepping) {
        baseState = ArbiterState::Stepping;
    } else {
        // Standing needs the slow conditions to have held for standingTimeS
        // (a hold, so a sway through the speed threshold does not flicker);
        // Standing drops back to Leaning at once when they fail.
        const bool hold =
            overallState_ == ArbiterState::Standing || slowS_ >= tuning_.standingTimeS;
        baseState = (slow && hold) ? ArbiterState::Standing : ArbiterState::Leaning;
    }

    if (baseState == ArbiterState::Falling || baseState == ArbiterState::Fallen) {
        // Falling and Fallen are whole-body cascading states overriding all parts
        for (std::size_t i = 0; i < partStates_.size(); ++i) {
            if (partStates_[i] != baseState) {
                partStates_[i] = baseState;
                partBlendFractions_[i] = 0.0;
            } else {
                partBlendFractions_[i] =
                    std::min(1.0, partBlendFractions_[i] + in.dtS / tuning_.blendTimeS);
            }
            disturbRemainingS_[i] = 0.0;
        }
    } else {
        // Update per-body-part states and disturbance timers
        for (std::size_t i = 0; i < partStates_.size(); ++i) {
            if (partStates_[i] == ArbiterState::Disturbed) {
                disturbRemainingS_[i] -= in.dtS;
                if (disturbRemainingS_[i] <= 0.0) {
                    // Recover back to base state
                    partStates_[i] = baseState;
                    partBlendFractions_[i] = 0.0;
                    disturbRemainingS_[i] = 0.0;
                } else {
                    partBlendFractions_[i] =
                        std::min(1.0, partBlendFractions_[i] + in.dtS / tuning_.blendTimeS);
                }
            } else {
                if (partStates_[i] != baseState) {
                    partStates_[i] = baseState;
                    partBlendFractions_[i] = 0.0;
                } else {
                    partBlendFractions_[i] =
                        std::min(1.0, partBlendFractions_[i] + in.dtS / tuning_.blendTimeS);
                }
            }
        }
    }

    if (baseState != overallState_) {
        overallState_ = baseState;
        overallBlendFraction_ = 0.0;
    } else {
        overallBlendFraction_ =
            std::min(1.0, overallBlendFraction_ + in.dtS / tuning_.blendTimeS);
    }

    // Update tracking strictness for each body part smoothly towards target (Task 0024, C1b)
    if (in.dtS > 0.0) {
        constexpr double kStrictnessRate = 15.0;
        const double alpha = std::exp(-kStrictnessRate * in.dtS);
        for (std::size_t i = 0; i < partStrictness_.size(); ++i) {
            const double target = strictnessOverrides_[i].has_value()
                                      ? *strictnessOverrides_[i]
                                      : defaultStrictness(partStates_[i]);
            partStrictness_[i] = target + (partStrictness_[i] - target) * alpha;
            partStrictness_[i] = std::clamp(partStrictness_[i], 0.0, 1.0);
        }
    }

    return overallState_;
}

} // namespace opas::arbiter
