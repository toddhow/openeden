#ifndef OPENEDEN_COMPONENTS_NIFSCENE_PARTICLEANALYTIC_HPP
#define OPENEDEN_COMPONENTS_NIFSCENE_PARTICLEANALYTIC_HPP

// Task 0359 stage b: analytic (stateless) particles for the particle systems of Skyrim SE static models.
//
// Why not the compute simulation Morrowind's particles use: the Skyrim scene reader returns ONE node that VSG shares
// between every placement of a model. A compute dispatch inside it would run once for each placement in view (the fire
// would simulate that many times too fast) and its particle buffer would be shared. So a particle here is a pure
// function of (scene time, slot): slot i of a system of M slots restarts every `period` seconds, staggered over the
// period, and everything about one life (where it starts, its velocity, its size, spin, flipbook frame) is drawn from
// a hash of (system seed, slot, cycle number), so no state is kept anywhere. The vertex shader
// (files/shaders/default/main.vert, `analyticParticle`, and its Slang twin) does exactly what `evaluate` does; keep the
// two in step. Every placement of a model shows the same flame at the same moment.
//
// The buffer the shader reads is the PARTICLE storage buffer (Pipeline::Data::Particle, four vec4 an entry): kHeader
// header entries that describe the system, then one entry a slot. A slot entry has velocityAge.w == -1, which is how
// the shader tells it from a Morrowind particle (their age is never negative).

#include <array>
#include <cstdint>
#include <vector>

#include "flatten.hpp"

namespace NifScene::ParticleAnalytic
{
    constexpr std::size_t kHeaderEntries = 5;
    constexpr std::size_t kMaxSlots = 128;
    constexpr std::size_t kCurveSamples = 16;
    /// NiPSysDragModifier's percentage is taken as a rate a second of percentage times this (ours: the data does not
    /// say; chosen so that the 0.075 of the giant campfire's smoke limits its rise to a few hundred units).
    constexpr float kDragPerSecond = 30.f;

    struct Entry
    {
        std::array<float, 4> mPositionSize{};
        std::array<float, 4> mColor{};
        std::array<float, 4> mVelocityAge{};
        std::array<float, 4> mMaxAge{};
    };

    /// Header layout (fields of an Entry as named there):
    ///  [0] positionSize = emitter centre xyz, emitter kind (0 point, 1 box, 2 cylinder, 3 sphere)
    ///      color = emitter size xyz (scaled by the emitter scale), aspect ratio
    ///      velocityAge = speed, speed variation, declination, declination variation (speeds already scaled)
    ///      maxAge = planar angle, planar variation, life, life variation
    ///  [1] positionSize.xyz / color.xyz / velocityAge.xyz = the emitter rotation's three columns (model frame);
    ///      positionSize.w = particle radius, color.w = radius variation (system scale applied),
    ///      velocityAge.w = unused, maxAge = rotation speed, speed variation, angle, angle variation
    ///  [2] positionSize = gravity xyz, drag rate; color = flipbook columns, rows, frame count, random rotation sign
    ///  (0/1);
    ///      velocityAge = start frame, start fudge, end frame, loop start; maxAge = frame count, count fudge, fade in,
    ///      fade out
    ///  [3] colours: positionSize = colour 1, color = colour 2, velocityAge = colour 3, maxAge = the four stops
    ///  [4] the size factor over a life, 16 samples: positionSize, color, velocityAge, maxAge in order
    /// Slot entry: positionSize = slot, slot count, period, phase; color.x = system seed; velocityAge.w = -1.
    struct Pack
    {
        std::vector<Entry> mEntries; // empty when the system draws nothing
        std::uint32_t mSlots = 0;
        float mPeriod = 1.f;
        std::array<float, 3> mCentre{ 0.f, 0.f, 0.f };
        float mReach = 0.f; // a sphere about mCentre that holds every particle, quad corners included
    };

    /// One particle at one moment, the way the shader computes it.
    struct Particle
    {
        bool mAlive = false;
        float mAge = 0.f;
        float mLife = 1.f;
        std::array<float, 3> mPosition{ 0.f, 0.f, 0.f };
        float mSize = 0.f; // half the quad side, before the aspect ratio
        float mAspect = 1.f;
        float mAngle = 0.f;
        std::array<float, 4> mColor{ 1.f, 1.f, 1.f, 1.f };
        std::array<float, 4> mRect{ 0.f, 1.f, 0.f, 1.f }; // u0, du, v0, dv of the flipbook frame
        std::uint32_t mFrame = 0;
    };

    /// `tint` multiplies the system's colours (the palette hue times the effect shader's base colour, and its alpha).
    /// The result depends on nothing but the arguments.
    Pack pack(const Flat::ParticleSystem& system, std::uint32_t seed, const std::array<float, 4>& tint);

    /// The particle in `slot` at `time` seconds. A slot at or beyond mSlots is dead.
    Particle evaluate(const Pack& pack, double time, std::uint32_t slot);

    /// The shader's integer hash and its [0, 1) float, exposed for the tests (Bob Jenkins' one-at-a-time, as
    /// lib/math/random.glsl).
    std::uint32_t hash(std::uint32_t x);
    float unitFloat(std::uint32_t bits);
}

#endif
