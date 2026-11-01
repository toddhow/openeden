#include "opas/style/ContextController.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace opas::style
{

    ContextController::ContextController(double smoothingRateHz)
        : smoothingRateHz_(std::max(0.1, smoothingRateHz))
    {
    }

    void ContextController::setContextImmediate(const GameContext& context)
    {
        target_ = context;
        current_ = context;
    }

    void ContextController::update(const GameContext& targetContext, double dt)
    {
        target_ = targetContext;
        if (dt <= 0.0)
        {
            return;
        }
        const double alpha = 1.0 - std::exp(-smoothingRateHz_ * dt);
        current_.health += alpha * (target_.health - current_.health);
        current_.encumbrance += alpha * (target_.encumbrance - current_.encumbrance);
        current_.alertness += alpha * (target_.alertness - current_.alertness);
        current_.cold += alpha * (target_.cold - current_.cold);
    }

    void ContextController::prepareEffectiveProfile(const StyleProfile& baseProfile, StyleProfile& outEffective)
    {
        outEffective = baseProfile;
        auto ensureLayer = [&](std::string_view name) {
            for (const auto& l : outEffective.layers)
            {
                if (l.name == name)
                {
                    return;
                }
            }
            outEffective.layers.push_back(LayerConfig{ std::string(name), 0.0, "" });
        };
        ensureLayer("limp");
        ensureLayer("hunch");
        ensureLayer("shiver");
    }

    void ContextController::applyContext(const StyleProfile& base, StyleProfile& out) const
    {
        const double h = std::clamp(current_.health, 0.0, 1.0);
        const double e = std::clamp(current_.encumbrance, 0.0, 1.0);
        const double a = std::clamp(current_.alertness, 0.0, 1.0);
        const double c = std::clamp(current_.cold, 0.0, 1.0);

        out.name = base.name;
        out.idle = base.idle;

        // 1. Posture mappings
        const double slouchDelta = (1.0 - h) * 0.25 + e * 0.15 - a * 0.10;
        out.posture.slouchRad = std::clamp(base.posture.slouchRad + slouchDelta, -0.6, 0.6);

        const double headPitchDelta = (1.0 - h) * 0.10 - a * 0.08;
        out.posture.headPitchRad = std::clamp(base.posture.headPitchRad + headPitchDelta, -0.6, 0.6);

        out.posture.headYawRad = base.posture.headYawRad;

        const double elevDelta = c * 0.25;
        out.posture.shoulderElevationRad = std::clamp(base.posture.shoulderElevationRad + elevDelta, -0.3, 0.6);

        out.posture.spineLeanRad = base.posture.spineLeanRad;

        // 2. Gait mappings
        const double cadenceFactor = (1.0 - 0.35 * (1.0 - h)) * (1.0 - 0.25 * e);
        out.gait.cadenceScale = std::clamp(base.gait.cadenceScale * cadenceFactor, 0.1001, 3.0);

        const double stepWidthDelta = e * 0.12;
        out.gait.stepWidthOffsetM = std::clamp(base.gait.stepWidthOffsetM + stepWidthDelta, -0.2, 0.5);

        out.gait.strideScale = base.gait.strideScale;
        out.gait.armSwingScale = base.gait.armSwingScale;
        out.gait.hipSwayScale = base.gait.hipSwayScale;

        // 3. Timing mappings
        const double stiffnessFactor = (1.0 - 0.40 * (1.0 - h)) * (1.0 + 0.35 * a);
        out.timing.stiffnessMultiplier = std::clamp(base.timing.stiffnessMultiplier * stiffnessFactor, 0.0501, 5.0);

        const double lagDelta = e * 0.15;
        out.timing.followThroughLagS = std::clamp(base.timing.followThroughLagS + lagDelta, 0.0, 0.5);

        out.timing.dampingMultiplier = base.timing.dampingMultiplier;

        // 4. Gaze mappings
        out.gaze.glanceFrequencyHz = std::clamp(base.gaze.glanceFrequencyHz + a * 1.2, 0.0, 3.0);
        out.gaze.glanceSpeedScale = std::clamp(base.gaze.glanceSpeedScale + a * 0.5, 0.1001, 3.0);
        out.gaze.headGazeFraction = base.gaze.headGazeFraction;

        // 5. Additive layer mappings
        auto baseLayerWeight = [&](std::string_view name) -> double {
            for (const auto& l : base.layers)
            {
                if (l.name == name)
                {
                    return l.weight;
                }
            }
            return 0.0;
        };

        auto setLayerWeight = [&](std::string_view name, double targetWeight) {
            for (auto& l : out.layers)
            {
                if (l.name == name)
                {
                    l.weight = std::clamp(targetWeight, 0.0, 1.0);
                    return;
                }
            }
            if (targetWeight > 0.0)
            {
                out.layers.push_back(LayerConfig{ std::string(name), std::clamp(targetWeight, 0.0, 1.0), "" });
            }
        };

        const double targetLimp = std::max(baseLayerWeight("limp"), (1.0 - h) * 0.8);
        const double targetHunch = std::max(baseLayerWeight("hunch"), c * 0.5);
        const double targetShiver = std::max(baseLayerWeight("shiver"), c * 0.75);

        setLayerWeight("limp", targetLimp);
        setLayerWeight("hunch", targetHunch);
        setLayerWeight("shiver", targetShiver);

        for (const auto& l : base.layers)
        {
            if (l.name != "limp" && l.name != "hunch" && l.name != "shiver")
            {
                setLayerWeight(l.name, l.weight);
            }
        }
    }

    StyleProfile ContextController::computeEffectiveProfile(const StyleProfile& baseProfile) const
    {
        StyleProfile effective;
        prepareEffectiveProfile(baseProfile, effective);
        applyContext(baseProfile, effective);
        return effective;
    }

} // namespace opas::style
