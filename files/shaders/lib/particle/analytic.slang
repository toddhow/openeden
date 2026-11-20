// OpenEden (task 0359 stage b): analytic particles for Skyrim SE's static models. A particle is a pure function of the
// scene time and its slot, so a model shared by many placements needs no per-placement state and no compute pass. The
// buffer layout and the maths are described in components/nifscene/particleanalytic.hpp and done by
// NifScene::ParticleAnalytic::evaluate: change both together. This is the Slang twin of lib/particle/analytic.glsl.
// Needs lib.particle.data (Particle).

uint paHash(uint x)
{
    x += (x << 10u);
    x ^= (x >> 6u);
    x += (x << 3u);
    x ^= (x >> 11u);
    x += (x << 15u);
    return x;
}

float paUnit(uint bits)
{
    return asfloat((bits & 0x007FFFFFu) | 0x3F800000u) - 1.0;
}

float paDraw(uint key, uint k)
{
    return paUnit(paHash(key + k * 0x85EBCA6Bu));
}

struct AnalyticParticle
{
    float3 position;
    float size;
    float aspect;
    float angle;
    float4 color;
    float4 rect; // u0, du, v0, dv
    float age;
    float life;
};

AnalyticParticle analyticParticle(Particle slotEntry, Particle e0, Particle e1, Particle e2, Particle e3, Particle e4,
    float time)
{
    AnalyticParticle o;
    uint slot = uint(slotEntry.positionSize.x);
    float period = slotEntry.positionSize.z;
    float x = (time + slotEntry.positionSize.w) / period;
    float cycleF = floor(x);
    float age = (x - cycleF) * period;
    uint cycle = uint(max(cycleF, 0.0));
    uint key = paHash(uint(slotEntry.color.x) + slot * 0x9E3779B1u) ^ paHash(cycle + 0x7F4A7C15u);

    float life = max(0.01, e0.maxAge.z + e0.maxAge.w * (2.0 * paDraw(key, 0u) - 1.0));
    o.age = age;
    o.life = life;
    float u = clamp(age / life, 0.0, 1.0);

    // where it starts, in the emitter's frame
    float3 local = float3(0.0, 0.0, 0.0);
    int kind = int(e0.positionSize.w + 0.5);
    float a = paDraw(key, 5u), b = paDraw(key, 6u), c = paDraw(key, 7u);
    if (kind == 1)
        local = float3((a - 0.5) * e0.color.x, (b - 0.5) * e0.color.y, (c - 0.5) * e0.color.z);
    else if (kind == 2)
    {
        float radius = e0.color.x * sqrt(a);
        float angle = 6.28318530718 * b;
        local = float3(radius * cos(angle), radius * sin(angle), (c - 0.5) * e0.color.y);
    }
    else if (kind == 3)
    {
        float radius = e0.color.x * pow(c, 1.0 / 3.0);
        float z = 2.0 * a - 1.0;
        float angle = 6.28318530718 * b;
        float ring = sqrt(max(0.0, 1.0 - z * z));
        local = float3(radius * ring * cos(angle), radius * ring * sin(angle), radius * z);
    }
    // which way it goes
    float speed = e0.velocityAge.x + e0.velocityAge.y * (2.0 * paDraw(key, 1u) - 1.0);
    float declination = e0.velocityAge.z + e0.velocityAge.w * (2.0 * paDraw(key, 2u) - 1.0);
    float planar = e0.maxAge.x + e0.maxAge.y * (2.0 * paDraw(key, 3u) - 1.0);
    float3 dirLocal = float3(sin(declination) * cos(planar), sin(declination) * sin(planar), cos(declination));
    float3 axisX = e1.positionSize.xyz, axisY = e1.color.xyz, axisZ = e1.velocityAge.xyz;
    float3 p0 = axisX * local.x + axisY * local.y + axisZ * local.z;
    float3 d = axisX * dirLocal.x + axisY * dirLocal.y + axisZ * dirLocal.z;
    float3 gravity = e2.positionSize.xyz;
    float drag = e2.positionSize.w;
    float3 v0 = d * speed;
    float3 travelled;
    if (drag > 1e-4)
    {
        float3 terminal = gravity / drag;
        travelled = terminal * age + (v0 - terminal) * ((1.0 - exp(-drag * age)) / drag);
    }
    else
        travelled = v0 * age + 0.5 * gravity * age * age;
    o.position = e0.positionSize.xyz + p0 + travelled;

    // size over the life: the 16 samples of e4, a straight line between them
    float samples[16] = { e4.positionSize.x, e4.positionSize.y, e4.positionSize.z, e4.positionSize.w,
        e4.color.x, e4.color.y, e4.color.z, e4.color.w, e4.velocityAge.x, e4.velocityAge.y, e4.velocityAge.z,
        e4.velocityAge.w, e4.maxAge.x, e4.maxAge.y, e4.maxAge.z, e4.maxAge.w };
    float curveT = u * 15.0;
    int curveI = min(int(curveT), 14);
    float curve = lerp(samples[curveI], samples[curveI + 1], curveT - float(curveI));
    o.size = max((e1.positionSize.w + e1.color.w * (2.0 * paDraw(key, 4u) - 1.0)) * curve, 0.0);
    o.aspect = e0.color.w;

    float spinSign = e2.color.w > 0.5 ? (paDraw(key, 10u) < 0.5 ? -1.0 : 1.0) : 1.0;
    o.angle = e1.maxAge.z + e1.maxAge.w * (2.0 * paDraw(key, 8u) - 1.0)
        + (e1.maxAge.x + e1.maxAge.y * (2.0 * paDraw(key, 9u) - 1.0)) * spinSign * age;

    // colour: three colours with the stops between them, then the alpha fades at the ends of the life
    float4 stops = e3.maxAge;
    if (u < stops.x)
        o.color = e3.positionSize;
    else if (u < stops.y)
        o.color = lerp(e3.positionSize, e3.color, clamp((u - stops.x) / max(stops.y - stops.x, 1e-5), 0.0, 1.0));
    else if (u < stops.z)
        o.color = e3.color;
    else if (u < stops.w)
        o.color = lerp(e3.color, e3.velocityAge, clamp((u - stops.z) / max(stops.w - stops.z, 1e-5), 0.0, 1.0));
    else
        o.color = e3.velocityAge;
    float fadeIn = e2.maxAge.z, fadeOut = e2.maxAge.w;
    float fade = 1.0;
    if (fadeIn > 0.0)
        fade *= min(u / fadeIn, 1.0);
    if (fadeOut > 0.0)
        fade *= min((1.0 - u) / fadeOut, 1.0);
    o.color.a *= fade;

    // the flipbook frame
    o.rect = float4(0.0, 1.0, 0.0, 1.0);
    float columns = e2.color.x, rows = e2.color.y, frames = e2.color.z;
    if (frames > 1.0)
    {
        float start = max(0.0, e2.velocityAge.x + e2.velocityAge.y * (2.0 * paDraw(key, 11u) - 1.0));
        float span = max(0.0, e2.maxAge.x + e2.maxAge.y * (2.0 * paDraw(key, 12u) - 1.0));
        float f = start + u * span;
        float last = min(e2.velocityAge.z, frames - 1.0);
        float loopStart = min(e2.velocityAge.w, last);
        if (f > last + 1.0 - 1e-4 && last + 1.0 - loopStart > 0.0)
            f = loopStart + fmod(f - loopStart, last + 1.0 - loopStart);
        float frame = clamp(floor(f), 0.0, frames - 1.0);
        float col = fmod(frame, columns);
        float row = floor(frame / columns);
        o.rect = float4(col / columns, 1.0 / columns, row / rows, 1.0 / rows);
    }
    return o;
}
