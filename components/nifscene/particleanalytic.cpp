#include "particleanalytic.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace NifScene::ParticleAnalytic
{
    namespace
    {
        constexpr float kPi = 3.14159265358979f;

        float lerp(float a, float b, float t)
        {
            return a + (b - a) * t;
        }

        // The size factor at u (0 to 1) from the modifier's own array, as a straight line between its values.
        float curveAt(const std::vector<float>& values, float u)
        {
            if (values.empty())
                return 1.f;
            if (values.size() == 1)
                return values[0];
            const float f = std::clamp(u, 0.f, 1.f) * static_cast<float>(values.size() - 1);
            const std::size_t i = std::min<std::size_t>(static_cast<std::size_t>(f), values.size() - 2);
            return lerp(values[i], values[i + 1], f - static_cast<float>(i));
        }

        struct Grid
        {
            std::uint32_t mColumns = 1, mRows = 1;
            bool mValid = false;
        };

        // A flipbook atlas is a regular grid listed row by row (left, width, top, height); any other list is not cut up.
        Grid gridOf(const std::vector<Vec4>& rects)
        {
            Grid g;
            if (rects.size() < 2 || !(rects[0][1] > 0.f) || !(rects[0][3] > 0.f))
                return g;
            const float columns = std::round(1.f / rects[0][1]), rows = std::round(1.f / rects[0][3]);
            if (columns < 1.f || rows < 1.f || columns * rows != static_cast<float>(rects.size())
                || columns * rows > 4096.f)
                return g;
            const auto c = static_cast<std::uint32_t>(columns), r = static_cast<std::uint32_t>(rows);
            for (std::size_t i = 0; i < rects.size(); ++i)
            {
                const float u0 = static_cast<float>(i % c) / columns, v0 = static_cast<float>(i / c) / rows;
                if (std::fabs(rects[i][0] - u0) > 2e-3f || std::fabs(rects[i][1] - 1.f / columns) > 2e-3f
                    || std::fabs(rects[i][2] - v0) > 2e-3f || std::fabs(rects[i][3] - 1.f / rows) > 2e-3f)
                    return g;
            }
            g.mColumns = c;
            g.mRows = r;
            g.mValid = true;
            return g;
        }

        std::uint32_t slotKey(std::uint32_t seed, std::uint32_t slot, std::uint32_t cycle)
        {
            return hash(seed + slot * 0x9E3779B1u) ^ hash(cycle + 0x7F4A7C15u);
        }

        float draw(std::uint32_t key, std::uint32_t k)
        {
            return unitFloat(hash(key + k * 0x85EBCA6Bu));
        }
    }

    std::uint32_t hash(std::uint32_t x)
    {
        x += (x << 10u);
        x ^= (x >> 6u);
        x += (x << 3u);
        x ^= (x >> 11u);
        x += (x << 15u);
        return x;
    }

    float unitFloat(std::uint32_t bits)
    {
        std::uint32_t m = (bits & 0x007FFFFFu) | 0x3F800000u;
        float f;
        std::memcpy(&f, &m, sizeof(f));
        return f - 1.f;
    }

    Pack pack(const Flat::ParticleSystem& system, std::uint32_t seed, const std::array<float, 4>& tint)
    {
        Pack result;
        const NifScene::ParticleSystem& s = system.mSystem;
        if (!(s.mBirthRate > 0.f) || !(s.mLife > 0.f))
            return result;
        const float lifeMax = s.mLife + s.mLifeVariation;
        const std::uint32_t pool = s.mMaxParticles == 0 ? static_cast<std::uint32_t>(kMaxSlots) : s.mMaxParticles;
        const std::uint32_t wanted = static_cast<std::uint32_t>(std::ceil(std::min(s.mBirthRate * lifeMax, 1.0e6f)));
        const std::uint32_t slots
            = std::clamp<std::uint32_t>(std::min(pool, wanted), 1u, static_cast<std::uint32_t>(kMaxSlots));
        result.mSlots = slots;
        result.mPeriod = std::max(static_cast<float>(slots) / s.mBirthRate, lifeMax);
        result.mCentre = system.mCentre;
        const float sys = system.mSystemScale;
        const float emitterScale = system.mEmitterScale;

        result.mEntries.resize(kHeaderEntries + slots);
        Entry& e0 = result.mEntries[0];
        e0.mPositionSize = { system.mCentre[0], system.mCentre[1], system.mCentre[2],
            static_cast<float>(static_cast<int>(s.mEmitter)) };
        e0.mColor = { s.mEmitterSize[0] * emitterScale, s.mEmitterSize[1] * emitterScale,
            s.mEmitterSize[2] * emitterScale, s.mAspectRatio };
        e0.mVelocityAge = { s.mSpeed * sys, s.mSpeedVariation * sys, s.mDeclination, s.mDeclinationVariation };
        e0.mMaxAge = { s.mPlanarAngle, s.mPlanarAngleVariation, s.mLife, s.mLifeVariation };

        Entry& e1 = result.mEntries[1];
        const auto& r = system.mRotation;
        e1.mPositionSize = { r[0], r[3], r[6], s.mRadius * sys };
        e1.mColor = { r[1], r[4], r[7], s.mRadiusVariation * sys };
        e1.mVelocityAge = { r[2], r[5], r[8], 0.f };
        e1.mMaxAge = { s.mRotationSpeed, s.mRotationSpeedVariation, s.mRotationAngle, s.mRotationAngleVariation };

        const Grid grid = s.mHasSubTexture ? gridOf(s.mSubTextures) : Grid{};
        Entry& e2 = result.mEntries[2];
        e2.mPositionSize = { system.mGravity[0], system.mGravity[1], system.mGravity[2],
            std::clamp(s.mDrag, 0.f, 2.f) * kDragPerSecond };
        e2.mColor = { static_cast<float>(grid.mColumns), static_cast<float>(grid.mRows),
            static_cast<float>(grid.mColumns * grid.mRows), s.mRandomRotationSign ? 1.f : 0.f };
        e2.mVelocityAge = { s.mSubStart, s.mSubStartFudge, s.mSubEnd, s.mSubLoopStart };
        e2.mMaxAge = { s.mSubCount, s.mSubCountFudge, s.mFadeIn, s.mFadeOut };

        Entry& e3 = result.mEntries[3];
        std::array<std::array<float, 4>, 3> colours;
        for (std::size_t i = 0; i < 3; ++i)
        {
            const Vec4& c = s.mHasColor ? s.mColors[i] : s.mInitialColor;
            colours[i] = { c[0] * s.mInitialColor[0] * tint[0], c[1] * s.mInitialColor[1] * tint[1],
                c[2] * s.mInitialColor[2] * tint[2], c[3] * s.mInitialColor[3] * tint[3] };
        }
        std::array<float, 4> stops{ 0.f, 0.5f, 0.5f, 1.f };
        if (s.mHasColor)
        {
            stops = s.mColorStops;
            if (stops[0] == stops[1] && stops[1] == stops[2] && stops[2] == stops[3])
                stops = { 0.f, 0.5f, 0.5f, 1.f }; // no stops given: three colours spread evenly over the life
            for (std::size_t i = 1; i < 4; ++i)
                stops[i] = std::max(stops[i], stops[i - 1]);
        }
        else
            stops = { 0.f, 0.f, 1.f, 1.f };
        e3.mPositionSize = colours[0];
        e3.mColor = colours[1];
        e3.mVelocityAge = colours[2];
        e3.mMaxAge = stops;

        Entry& e4 = result.mEntries[4];
        float maxCurve = 0.f;
        std::array<float, kCurveSamples> samples{};
        for (std::size_t i = 0; i < kCurveSamples; ++i)
        {
            samples[i] = curveAt(s.mScales, static_cast<float>(i) / static_cast<float>(kCurveSamples - 1));
            maxCurve = std::max(maxCurve, samples[i]);
        }
        e4.mPositionSize = { samples[0], samples[1], samples[2], samples[3] };
        e4.mColor = { samples[4], samples[5], samples[6], samples[7] };
        e4.mVelocityAge = { samples[8], samples[9], samples[10], samples[11] };
        e4.mMaxAge = { samples[12], samples[13], samples[14], samples[15] };

        const float seedPhase = result.mPeriod * unitFloat(hash(seed ^ 0xA511E9B3u));
        for (std::uint32_t i = 0; i < slots; ++i)
        {
            Entry& e = result.mEntries[kHeaderEntries + i];
            const float phase = seedPhase + result.mPeriod * static_cast<float>(i) / static_cast<float>(slots);
            e.mPositionSize = { static_cast<float>(i), static_cast<float>(slots), result.mPeriod, phase };
            e.mColor = { static_cast<float>(seed & 0xFFFFFFu), 0.f, 0.f, 0.f };
            e.mVelocityAge = { 0.f, 0.f, 0.f, -1.f };
            e.mMaxAge = { 0.f, 0.f, 0.f, 0.f };
        }

        // the sphere that holds every quad: the emitter volume, the longest flight, and the biggest quad corner
        float extent = 0.f;
        switch (s.mEmitter)
        {
            case ParticleSystem::Emitter::Box:
                extent = 0.5f * emitterScale
                    * std::sqrt(s.mEmitterSize[0] * s.mEmitterSize[0] + s.mEmitterSize[1] * s.mEmitterSize[1]
                        + s.mEmitterSize[2] * s.mEmitterSize[2]);
                break;
            case ParticleSystem::Emitter::Cylinder:
                extent = emitterScale * std::hypot(s.mEmitterSize[0], 0.5f * s.mEmitterSize[1]);
                break;
            case ParticleSystem::Emitter::Sphere:
                extent = emitterScale * s.mEmitterSize[0];
                break;
            case ParticleSystem::Emitter::Point:
                break;
        }
        const float gravity = std::sqrt(system.mGravity[0] * system.mGravity[0] + system.mGravity[1] * system.mGravity[1]
            + system.mGravity[2] * system.mGravity[2]);
        const float travel = (s.mSpeed + s.mSpeedVariation) * sys * lifeMax + 0.5f * gravity * lifeMax * lifeMax;
        const float quad = (s.mRadius + s.mRadiusVariation) * sys * maxCurve * 1.5f * std::max(1.f, s.mAspectRatio);
        result.mReach = extent + travel + quad;
        return result;
    }

    Particle evaluate(const Pack& pack, double time, std::uint32_t slot)
    {
        Particle out;
        if (slot >= pack.mSlots || pack.mEntries.size() < kHeaderEntries + pack.mSlots)
            return out;
        const Entry& e0 = pack.mEntries[0];
        const Entry& e1 = pack.mEntries[1];
        const Entry& e2 = pack.mEntries[2];
        const Entry& e3 = pack.mEntries[3];
        const Entry& e4 = pack.mEntries[4];
        const Entry& slotEntry = pack.mEntries[kHeaderEntries + slot];
        const float period = slotEntry.mPositionSize[2];
        const double x = (time + static_cast<double>(slotEntry.mPositionSize[3])) / static_cast<double>(period);
        const double cycleD = std::floor(x);
        const float age = static_cast<float>((x - cycleD) * static_cast<double>(period));
        const auto cycle = static_cast<std::uint32_t>(static_cast<std::int64_t>(cycleD));
        const std::uint32_t key = slotKey(static_cast<std::uint32_t>(slotEntry.mColor[0]), slot, cycle);
        auto rnd = [&](std::uint32_t k) { return draw(key, k); };
        auto signedRnd = [&](std::uint32_t k) { return 2.f * draw(key, k) - 1.f; };

        const float life = std::max(0.01f, e0.mMaxAge[2] + e0.mMaxAge[3] * signedRnd(0));
        out.mAge = age;
        out.mLife = life;
        out.mAlive = age < life;
        const float u = std::clamp(age / life, 0.f, 1.f);

        // where it starts, in the emitter's frame
        std::array<float, 3> local{ 0.f, 0.f, 0.f };
        const int kind = static_cast<int>(e0.mPositionSize[3]);
        const float a = rnd(5), b = rnd(6), c = rnd(7);
        if (kind == static_cast<int>(ParticleSystem::Emitter::Box))
            local = { (a - 0.5f) * e0.mColor[0], (b - 0.5f) * e0.mColor[1], (c - 0.5f) * e0.mColor[2] };
        else if (kind == static_cast<int>(ParticleSystem::Emitter::Cylinder))
        {
            const float radius = e0.mColor[0] * std::sqrt(a), angle = 2.f * kPi * b;
            local = { radius * std::cos(angle), radius * std::sin(angle), (c - 0.5f) * e0.mColor[1] };
        }
        else if (kind == static_cast<int>(ParticleSystem::Emitter::Sphere))
        {
            const float radius = e0.mColor[0] * std::cbrt(c), z = 2.f * a - 1.f, angle = 2.f * kPi * b;
            const float ring = std::sqrt(std::max(0.f, 1.f - z * z));
            local = { radius * ring * std::cos(angle), radius * ring * std::sin(angle), radius * z };
        }
        // which way it goes
        const float speed = e0.mVelocityAge[0] + e0.mVelocityAge[1] * signedRnd(1);
        const float declination = e0.mVelocityAge[2] + e0.mVelocityAge[3] * signedRnd(2);
        const float planar = e0.mMaxAge[0] + e0.mMaxAge[1] * signedRnd(3);
        const std::array<float, 3> dirLocal{ std::sin(declination) * std::cos(planar),
            std::sin(declination) * std::sin(planar), std::cos(declination) };
        auto toModel = [&](const std::array<float, 3>& v) {
            return std::array<float, 3>{ e1.mPositionSize[0] * v[0] + e1.mColor[0] * v[1] + e1.mVelocityAge[0] * v[2],
                e1.mPositionSize[1] * v[0] + e1.mColor[1] * v[1] + e1.mVelocityAge[1] * v[2],
                e1.mPositionSize[2] * v[0] + e1.mColor[2] * v[1] + e1.mVelocityAge[2] * v[2] };
        };
        const std::array<float, 3> p0 = toModel(local);
        const std::array<float, 3> d = toModel(dirLocal);
        const std::array<float, 3> gravity{ e2.mPositionSize[0], e2.mPositionSize[1], e2.mPositionSize[2] };
        const float drag = e2.mPositionSize[3];
        for (std::size_t i = 0; i < 3; ++i)
        {
            const float v0 = d[i] * speed;
            float travelled;
            if (drag > 1e-4f)
            {
                const float terminal = gravity[i] / drag;
                travelled = terminal * age + (v0 - terminal) * (1.f - std::exp(-drag * age)) / drag;
            }
            else
                travelled = v0 * age + 0.5f * gravity[i] * age * age;
            out.mPosition[i] = e0.mPositionSize[i] + p0[i] + travelled;
        }

        out.mSize = (e1.mPositionSize[3] + e1.mColor[3] * signedRnd(4)) * [&] {
            const float t = u * static_cast<float>(kCurveSamples - 1);
            const std::size_t i = std::min<std::size_t>(static_cast<std::size_t>(t), kCurveSamples - 2);
            auto sample = [&](std::size_t k) {
                const Entry& q = e4;
                const std::array<const std::array<float, 4>*, 4> rows{ &q.mPositionSize, &q.mColor, &q.mVelocityAge,
                    &q.mMaxAge };
                return (*rows[k / 4])[k % 4];
            };
            return lerp(sample(i), sample(i + 1), t - static_cast<float>(i));
        }();
        out.mSize = std::max(out.mSize, 0.f);
        out.mAspect = e0.mColor[3];

        const float sign = e2.mColor[3] > 0.5f ? (rnd(10) < 0.5f ? -1.f : 1.f) : 1.f;
        out.mAngle = e1.mMaxAge[2] + e1.mMaxAge[3] * signedRnd(8)
            + (e1.mMaxAge[0] + e1.mMaxAge[1] * signedRnd(9)) * sign * age;

        // colour: three colours with the stops between them, then the alpha fades at the ends of the life
        const std::array<float, 4>& stops = e3.mMaxAge;
        auto mix = [&](const std::array<float, 4>& from, const std::array<float, 4>& to, float lo, float hi) {
            const float t = std::clamp((u - lo) / std::max(hi - lo, 1e-5f), 0.f, 1.f);
            return std::array<float, 4>{ lerp(from[0], to[0], t), lerp(from[1], to[1], t), lerp(from[2], to[2], t),
                lerp(from[3], to[3], t) };
        };
        if (u < stops[0])
            out.mColor = e3.mPositionSize;
        else if (u < stops[1])
            out.mColor = mix(e3.mPositionSize, e3.mColor, stops[0], stops[1]);
        else if (u < stops[2])
            out.mColor = e3.mColor;
        else if (u < stops[3])
            out.mColor = mix(e3.mColor, e3.mVelocityAge, stops[2], stops[3]);
        else
            out.mColor = e3.mVelocityAge;
        const float fadeIn = e2.mMaxAge[2], fadeOut = e2.mMaxAge[3];
        float fade = 1.f;
        if (fadeIn > 0.f)
            fade *= std::min(u / fadeIn, 1.f);
        if (fadeOut > 0.f)
            fade *= std::min((1.f - u) / fadeOut, 1.f);
        out.mColor[3] *= fade;

        // the flipbook frame
        const float columns = e2.mColor[0], rows = e2.mColor[1], frames = e2.mColor[2];
        if (frames > 1.f)
        {
            const float start = std::max(0.f, e2.mVelocityAge[0] + e2.mVelocityAge[1] * signedRnd(11));
            const float span = std::max(0.f, e2.mMaxAge[0] + e2.mMaxAge[1] * signedRnd(12));
            float f = start + u * span;
            const float last = std::min(e2.mVelocityAge[2], frames - 1.f);
            const float loopStart = std::min(e2.mVelocityAge[3], last);
            if (f > last + 1.f - 1e-4f && last + 1.f - loopStart > 0.f)
                f = loopStart + std::fmod(f - loopStart, last + 1.f - loopStart);
            const float frame = std::clamp(std::floor(f), 0.f, frames - 1.f);
            out.mFrame = static_cast<std::uint32_t>(frame);
            const float col = std::fmod(frame, columns), row = std::floor(frame / columns);
            out.mRect = { col / columns, 1.f / columns, row / rows, 1.f / rows };
        }
        return out;
    }
}
