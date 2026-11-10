#ifndef OPENEDEN_RENDER_PUDDLELEVEL_H
#define OPENEDEN_RENDER_PUDDLELEVEL_H

// OpenEden (task 0095, stage W-a): the run-time side of the puddle map, as pure functions. `puddlefill.hpp` makes the
// map (per texel: capacity C, the highest capacity of its pit Cmax, the pit's catchment ratio kappa); these turn one
// number, the free water on the ground, into the water in a pit. Design: docs/research/0049-puddles-and-wetness.md
// section 5.2. The shader twin (stage W-d) is files/shaders/lib/material/wetness.glsl; change both together. Units
// are game units (Morrowind's: one unit is about 1.43 cm).
//
// Free water Wf = max(W - P, 0), where W is the water the rain has supplied and not yet lost and P what the ground
// soaks up first. A pit with catchment ratio kappa then holds the level L = min(Cmax, Wf * kappa) above its floor. A
// texel whose ground is `Cmax - C` above the pit's floor is under water by d = clamp(L - (Cmax - C), 0, C). So the
// deepest point wets first, the contour follows the ground, d never exceeds the baked capacity, and the picture is a
// function of W alone: drying retraces growing exactly, no state is kept.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Render::Puddle
{
    // Free water: the supply above what the ground soaks up. Not finite or negative inputs count as none.
    inline float freeWater(float supplied, float absorbed)
    {
        if (!(supplied > 0.f))
            return 0.f;
        if (!(absorbed > 0.f))
            absorbed = 0.f;
        return std::max(supplied - absorbed, 0.f);
    }

    // The level of the water in a pit above its floor. A catchment ratio that is not a positive number counts as 0
    // (nothing drains in), an infinite product as full.
    //
    // This is a flat-tank model: the volume is spread over the pit's whole area at its spill height, L = Wf * kappa.
    // For a bowl whose area grows with height the true level for the same volume is higher at first: a paraboloid of
    // depth Cmax holds V = A L^2 / (2 Cmax) up to level L (A is its area at the spill height), so the water of
    // Wf * kappa * A reaches L = sqrt(2 Cmax Wf kappa), not Wf * kappa (with Wf = 1, Cmax = 12 and kappa = 1: 4.9
    // against 1.0; the review quoted 3.5, from a slightly different bowl). Early filling of a bowl is therefore
    // under-predicted and its contour starts too small. The formula is the spec's and is not changed here; judge it
    // in the stage W-d frames and consider a hypsometric curve then.
    inline float levelOf(float freeWaterDepth, float kappa, float basinMax)
    {
        if (!(basinMax > 0.f) || !std::isfinite(basinMax) || !(freeWaterDepth > 0.f) || !(kappa > 0.f))
            return 0.f;
        // Compared as a division so that a huge supply times a huge ratio cannot overflow.
        if (freeWaterDepth >= basinMax / kappa)
            return basinMax;
        return freeWaterDepth * kappa;
    }

    // The depth of the water over a texel with capacity `capacity` in a pit of capacity `basinMax` holding `level`.
    inline float depthAt(float level, float basinMax, float capacity)
    {
        if (!(capacity > 0.f) || !(level > 0.f) || !std::isfinite(capacity) || !std::isfinite(basinMax))
            return 0.f;
        const float floorAbove = basinMax - capacity;
        return std::clamp(level - floorAbove, 0.f, capacity);
    }

    // How much of a texel the water covers: 0 dry, 1 under water, a smooth ramp over `edge` units of depth between
    // (a thin film at the shore). An edge of zero or less is a hard step.
    inline float coverage(float depth, float edge)
    {
        if (!(depth > 0.f))
            return 0.f;
        if (!(edge > 0.f) || !std::isfinite(edge) || !std::isfinite(depth))
            return 1.f;
        const float t = std::min(depth / edge, 1.f);
        return t * t * (3.f - 2.f * t);
    }

    // The wet ground just outside a puddle: 1 where the water is at the texel's floor, falling to 0 `range` units of
    // level below it. A texel under water is 1; a texel with no capacity is 0 (it is not part of a pit).
    inline float damp(float level, float basinMax, float capacity, float range)
    {
        if (!(capacity > 0.f) || !std::isfinite(capacity) || !std::isfinite(basinMax) || std::isnan(level))
            return 0.f;
        const float below = (basinMax - capacity) - level;
        if (!(below > 0.f))
            return 1.f;
        if (!(range > 0.f) || !std::isfinite(range))
            return 0.f;
        return std::max(1.f - below / range, 0.f);
    }

    // Quantisation of the stored channels (research 0049 section 4.4). Each pair round-trips within the stated bound
    // for values inside the range, and clamps (never wraps) outside it; a value that is not a number counts as zero
    // (capacity), as a neutral catchment of 1 (kappa) or as the base (height).

    // Capacity: 8 bits at 1/16 unit, 0 to 15.9375. Error at most 1/32 unit.
    constexpr float capacityStepsPerUnit = 16.f;
    constexpr float maxQuantisedCapacity = 255.f / capacityStepsPerUnit;

    inline std::uint8_t quantiseCapacity(float capacity)
    {
        if (!(capacity > 0.f))
            return 0;
        const float steps = std::floor(capacity * capacityStepsPerUnit + 0.5f);
        return static_cast<std::uint8_t>(std::min(steps, 255.f));
    }

    inline float dequantiseCapacity(std::uint8_t value)
    {
        return static_cast<float>(value) / capacityStepsPerUnit;
    }

    // Catchment ratio: log2(kappa) clamped to [0, 17], 8 bits (15 steps per power of two). A ratio is at least 1 (a pit
    // takes at least its own rain), and real Morrowind land has pits near 99,000 (review F2, the stage W-a log). Error
    // at most 1/30 in log2, about 2.3 per cent of kappa.
    constexpr float minLogKappa = 0.f;
    constexpr float maxLogKappa = 17.f;
    constexpr float logKappaStepsPerOctave = 15.f;

    inline std::uint8_t quantiseKappa(float kappa)
    {
        if (!(kappa > 0.f))
            kappa = 1.f;
        const float logKappa = std::clamp(std::log2(kappa), minLogKappa, maxLogKappa);
        return static_cast<std::uint8_t>(std::floor((logKappa - minLogKappa) * logKappaStepsPerOctave + 0.5f));
    }

    inline float dequantiseKappa(std::uint8_t value)
    {
        return std::exp2(static_cast<float>(value) / logKappaStepsPerOctave + minLogKappa);
    }

    // Height: 16 bits at 1/8 unit above a base the pack stores, a range of 8191.875 units. Error at most 1/16 unit.
    // Whether Morrowind's cells fit in that range is for stage W-b to check (the stage log of the spec).
    constexpr float heightStepsPerUnit = 8.f;
    constexpr float heightRange = 65535.f / heightStepsPerUnit;

    inline bool heightRepresentable(float height, float base)
    {
        return std::isfinite(height) && std::isfinite(base) && height >= base && height - base <= heightRange;
    }

    inline std::uint16_t quantiseHeight(float height, float base)
    {
        if (!std::isfinite(height) || !std::isfinite(base))
            return 0;
        const float steps = std::floor((height - base) * heightStepsPerUnit + 0.5f);
        return static_cast<std::uint16_t>(std::clamp(steps, 0.f, 65535.f));
    }

    inline float dequantiseHeight(std::uint16_t value, float base)
    {
        return base + static_cast<float>(value) / heightStepsPerUnit;
    }
}

#endif
