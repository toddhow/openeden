// Task 0072, review of stage g: test fixtures, not golden data. The GLSL strings the post passes compiled before the
// move into files/shaders/post (655d60a6ff), byte for byte from 655d60a6ff^ (components/render/ambientocclusion.cpp,
// bloom.cpp, tonemap.cpp, velocity.cpp, apps/openmw/mwrender/postprocess.cpp), every R"GLSL(...)GLSL" string under
// the name old_<file>_<variable>. Included by testpostpasses.cpp only; never edited.

const char* const old_ambientocclusion_sVertexShader = R"GLSL(#version 450
layout(location=0) out vec2 texCoord;
out gl_PerVertex { vec4 gl_Position; };
void main()
{
    vec2 position = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    gl_Position = vec4(position, 0.0, 1.0);
    texCoord = position * 0.5 + 0.5;
}
)GLSL";
const char* const old_ambientocclusion_sFragmentBody = R"GLSL(
layout(set=0, binding=0) uniform sampler2D linearDepth;
layout(set=0, binding=1) uniform sampler2D viewNormal;
layout(push_constant) uniform Push
{
    vec4 proj;
} push;
layout(location=0) in vec2 texCoord;
layout(location=0) out vec4 outAo;

const float PI = 3.14159265358979;

float gradientNoise(vec2 p)
{
    // Interleaved gradient noise (Jimenez 2014): a fixed pattern by pixel position, no time.
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

vec3 viewPosAt(vec2 uv, float d)
{
    return vec3((uv * 2.0 - 1.0) * d / push.proj.xy, -d);
}

float horizonCosine(vec3 delta, vec3 toCamera)
{
    float len = length(delta);
    if (!(len > 0.0) || len >= AO_RADIUS)
        return -1.0;
    float weight = clamp(1.0 - len / AO_RADIUS, 0.0, 1.0);
    float cosine = dot(delta, toCamera) / len;
    // The falloff of Render::Ao::horizonCosine at 1.
    return -1.0 + (cosine + 1.0) * weight;
}

float sliceVisibility(float h1, float h2, float n)
{
    h1 = n + max(h1 - n, -0.5 * PI);
    h2 = n + min(h2 - n, 0.5 * PI);
    float sinN = sin(n);
    float cosN = cos(n);
    return 0.25 * (-cos(2.0 * h1 - n) + cosN + 2.0 * h1 * sinN) + 0.25 * (-cos(2.0 * h2 - n) + cosN + 2.0 * h2 * sinN);
}

void main()
{
    outAo = vec4(1.0);
    float d = texture(linearDepth, texCoord).r;
    if (!(d > 0.0))
        return;
    vec3 n = texture(viewNormal, texCoord).xyz;
    if (!(dot(n, n) > 0.0))
        return;
    n = normalize(n);
    vec3 position = viewPosAt(texCoord, d);
    vec3 toCamera = normalize(-position);
    vec2 noise = vec2(gradientNoise(gl_FragCoord.xy), gradientNoise(gl_FragCoord.xy + vec2(5.588238, 5.588238)));
    // The radius on the screen, per axis (a view space circle), kept to a quarter of the screen.
    vec2 uvRadius = min(AO_RADIUS * abs(push.proj.xy) * 0.5 / d, vec2(0.25));
    float visibility = 0.0;
    for (int s = 0; s < AO_SLICES; ++s)
    {
        float phi = (float(s) + noise.x) / float(AO_SLICES) * PI;
        vec2 uvDir = vec2(cos(phi), sin(phi));
        vec3 dirView = normalize(vec3(uvDir * sign(push.proj.xy), 0.0));
        vec3 orthoDir = dirView - dot(dirView, toCamera) * toCamera;
        vec3 axis = normalize(cross(dirView, toCamera));
        vec3 projected = n - axis * dot(n, axis);
        float projectedLength = length(projected);
        if (projectedLength < 1e-4)
        {
            visibility += 1.0;
            continue;
        }
        float cosN = clamp(dot(projected, toCamera) / projectedLength, 0.0, 1.0);
        float nAngle = (dot(orthoDir, projected) >= 0.0 ? 1.0 : -1.0) * acos(cosN);
        float horizon[2] = float[2](-1.0, -1.0);
        for (int side = 0; side < 2; ++side)
        {
            float direction = side == 0 ? 1.0 : -1.0;
            for (int k = 1; k <= AO_STEPS; ++k)
            {
                float t = (float(k) - noise.y) / float(AO_STEPS);
                vec2 sampleUv = texCoord + direction * uvDir * uvRadius * t;
                float sampleDepth = texture(linearDepth, sampleUv).r;
                if (!(sampleDepth > 0.0))
                    continue;
                horizon[side] = max(horizon[side], horizonCosine(viewPosAt(sampleUv, sampleDepth) - position, toCamera));
            }
        }
        float h1 = -acos(clamp(horizon[1], -1.0, 1.0));
        float h2 = acos(clamp(horizon[0], -1.0, 1.0));
        visibility += projectedLength * sliceVisibility(h1, h2, nAngle);
    }
    outAo = vec4(clamp(visibility / float(AO_SLICES), 0.0, 1.0));
}
)GLSL";
const char* const old_bloom_sVertexShader = R"GLSL(#version 450
layout(location=0) out vec2 texCoord;
out gl_PerVertex { vec4 gl_Position; };
void main()
{
    vec2 position = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    gl_Position = vec4(position, 0.0, 1.0);
    texCoord = position * 0.5 + 0.5;
}
)GLSL";
const char* const old_bloom_sDownFragment = R"GLSL(#version 450
layout(set=0, binding=0) uniform sampler2D source;
layout(push_constant) uniform Push { vec4 texel; } push; // xy: the source's texel size
layout(location=0) in vec2 texCoord;
layout(location=0) out vec4 outColor;
const float kMaxInput = 64.0; // about 1 / 1000 of the float target's largest value
vec3 tap(vec2 offset)
{
    vec3 v = texture(source, texCoord + offset * push.texel.xy).rgb;
    if (any(isnan(v)))
        return vec3(0.0);
    return min(v, vec3(kMaxInput));
}
void main()
{
    vec3 a = tap(vec2(-2, 2)), b = tap(vec2(0, 2)), c = tap(vec2(2, 2));
    vec3 d = tap(vec2(-2, 0)), e = tap(vec2(0, 0)), f = tap(vec2(2, 0));
    vec3 g = tap(vec2(-2, -2)), h = tap(vec2(0, -2)), i = tap(vec2(2, -2));
    vec3 j = tap(vec2(-1, 1)), k = tap(vec2(1, 1)), l = tap(vec2(-1, -1)), m = tap(vec2(1, -1));
    vec3 result = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    outColor = vec4(result, 1.0);
}
)GLSL";
const char* const old_bloom_sUpFragment = R"GLSL(#version 450
layout(set=0, binding=0) uniform sampler2D source;
layout(push_constant) uniform Push { vec4 texel; } push; // xy: the source's texel size
layout(location=0) in vec2 texCoord;
layout(location=0) out vec4 outColor;
void main()
{
    vec2 t = push.texel.xy;
    vec3 a = texture(source, texCoord + vec2(-t.x, t.y)).rgb;
    vec3 b = texture(source, texCoord + vec2(0, t.y)).rgb;
    vec3 c = texture(source, texCoord + vec2(t.x, t.y)).rgb;
    vec3 d = texture(source, texCoord + vec2(-t.x, 0)).rgb;
    vec3 e = texture(source, texCoord).rgb;
    vec3 f = texture(source, texCoord + vec2(t.x, 0)).rgb;
    vec3 g = texture(source, texCoord + vec2(-t.x, -t.y)).rgb;
    vec3 h = texture(source, texCoord + vec2(0, -t.y)).rgb;
    vec3 i = texture(source, texCoord + vec2(t.x, -t.y)).rgb;
    vec3 result = (e * 4.0 + (b + d + f + h) * 2.0 + (a + c + g + i)) / 16.0;
    outColor = vec4(result, 1.0);
}
)GLSL";
const char* const old_postprocess_sVertexShader = R"GLSL(#version 450
layout(location=0) out vec2 omw_TexCoord;
out gl_PerVertex { vec4 gl_Position; };
void main()
{
    vec2 position = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    gl_Position = vec4(position, 0.0, 1.0);
    omw_TexCoord = position * 0.5 + 0.5;
}
)GLSL";
const char* const old_postprocess_sPresentVertexShader = R"GLSL(#version 450
layout(location=0) out vec2 texCoord;
out gl_PerVertex { vec4 gl_Position; };
void main()
{
    vec2 position = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    gl_Position = vec4(position, 0.0, 1.0);
    texCoord = vec2(position.x, -position.y) * 0.5 + 0.5;
}
)GLSL";
const char* const old_postprocess_sPresentFragmentShader = R"GLSL(#version 450
layout(set=0, binding=0) uniform sampler2D result;
layout(location=0) in vec2 texCoord;
layout(location=0) out vec4 color;
void main()
{
    color = vec4(texture(result, texCoord).rgb, 1.0);
}
)GLSL";
const char* const old_tonemap_sVertexShader = R"GLSL(#version 450
layout(location=0) out vec2 texCoord;
out gl_PerVertex { vec4 gl_Position; };
void main()
{
    vec2 position = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    gl_Position = vec4(position, 0.0, 1.0);
    texCoord = position * 0.5 + 0.5;
}
)GLSL";
const char* const old_tonemap_sFragmentBody = R"GLSL(
layout(set=0, binding=0) uniform sampler2D sceneColor;
#ifdef BLOOM
layout(set=0, binding=1) uniform sampler2D bloomColor;
#endif
#ifdef AO
layout(set=0, binding=6) uniform sampler2D gAmbient;
layout(set=0, binding=7) uniform sampler2D gAo;       // half resolution, nearest
layout(set=0, binding=8) uniform sampler2D gAoNormal;
layout(set=0, binding=9) uniform sampler2D gAoDepth;
#endif
#ifdef SSS
layout(set=0, binding=10) uniform sampler2D gSssMaterial; // alpha: 1 on skin
layout(set=0, binding=11) uniform sampler2D gSssDepth;
layout(set=0, binding=12) uniform sampler2D gSssNormal;
#endif
#ifdef DEBUG_VIEW
layout(set=0, binding=2) uniform sampler2D gNormal;
layout(set=0, binding=3) uniform sampler2D gLinearDepth;
layout(set=0, binding=4) uniform sampler2D gVelocity;
layout(set=0, binding=5) uniform sampler2D gMaterial;
#endif
layout(push_constant) uniform Push
{
    vec4 params; // x exposure scale, y curve, z encode, w bloom strength
#ifdef AO
    vec4 ao;     // xy the size of one texel of the occlusion image, z strength
#endif
#ifdef SSS
    vec4 sss;    // x strength, y width in game units, z focal length in pixels
#endif
#ifdef DEBUG_VIEW
    vec4 debug;  // x mode (1 normals, 2 depth, 3 velocity, 4 roughness, 5 material, 6 subsurface, 7 ao, 8 ambient, 9 sss), y depth range, z velocity scale
#endif
} push;
layout(location=0) in vec2 texCoord;
layout(location=0) out vec4 outColor;

vec3 reinhard(vec3 x)
{
    return x * (vec3(1.0) + x / (REINHARD_WHITE * REINHARD_WHITE)) / (vec3(1.0) + x);
}
vec3 aces(vec3 x)
{
    return (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
}

#ifdef AO
// Task 0029: the CPU twins are Render::Ao::bilateralWeight and aoCurve (aomath.hpp). The half resolution occlusion is
// filtered over 5 by 5 of its texels, each weighted by whether its surface (the stored depth and normal at that
// texel centre) is this pixel's.
float aoSmooth(float low, float high, float x)
{
    float t = clamp((x - low) / (high - low), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
float aoBilateral(float depthDifference, float depth, float normalDot)
{
    float relative = depth > 0.0 ? abs(depthDifference) / depth : 1.0;
    return (1.0 - aoSmooth(0.02, 0.05, relative)) * aoSmooth(0.707, 0.9, normalDot);
}
// pow(0, 0) is undefined in GLSL, so strength 0 (no occlusion) is handled first.
float aoApply(float ao, float strength)
{
    return strength > 0.0 ? pow(max(ao, 0.0), strength) : 1.0;
}
float aoFiltered(vec2 uv)
{
    float d0 = texture(gAoDepth, uv).r;
    if (!(d0 > 0.0))
        return 1.0;
    vec3 n0 = texture(gAoNormal, uv).xyz;
    n0 = dot(n0, n0) > 0.0 ? normalize(n0) : vec3(0.0, 0.0, 1.0);
    float sum = 0.0;
    float total = 0.0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x)
        {
            vec2 tap = uv + vec2(float(x), float(y)) * push.ao.xy;
            vec2 centre = (floor(tap / push.ao.xy) + 0.5) * push.ao.xy;
            float d = texture(gAoDepth, centre).r;
            if (!(d > 0.0))
                continue;
            vec3 n = texture(gAoNormal, centre).xyz;
            n = dot(n, n) > 0.0 ? normalize(n) : n0;
            float w = aoBilateral(d - d0, d0, dot(n, n0)) * (1.0 - 0.1 * float(x * x + y * y));
            sum += texture(gAo, centre).r * w;
            total += w;
        }
    // No neighbour on this surface: no occlusion, rather than another surface's.
    return total > 1e-4 ? sum / total : 1.0;
}
#endif

#ifdef SSS
// Task 0033: the CPU twins are Render::Sss::gaussian, tapOffset, surfaceWeight and scatter (sssmath.hpp). For a pixel on
// skin, each colour channel becomes: itself times a gain, the gain being a wide Gaussian average of the neighbouring
// skin on the same surface over a very narrow one, blended in by the strength. The constants repeat sssmath.hpp (the
// numbers are written in).
float sssSmooth(float low, float high, float x)
{
    float t = clamp((x - low) / (high - low), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
float sssGaussian(float distance, float sigma)
{
    if (!(sigma > 0.0))
        return distance == 0.0 ? 1.0 : 0.0;
    return exp(-distance * distance / (2.0 * sigma * sigma));
}
float sssSurface(float depthDelta, float width, float normalDot)
{
    float limit = max(width, 1e-4);
    return (1.0 - sssSmooth(limit, 2.0 * limit, abs(depthDelta))) * sssSmooth(0.2, 0.6, normalDot);
}
vec3 sssApply(vec2 uv, vec3 centre)
{
    // Skin is marked by the material output's alpha; multisampling averages it, so an edge pixel counts when half is skin.
    if (!(push.sss.x > 0.0) || texture(gSssMaterial, uv).a < 0.5)
        return centre;
    float depth = texture(gSssDepth, uv).r;
    if (!(depth > 0.0))
        return centre;
    float width = push.sss.y;
    float pixelsPerUnit = push.sss.z / depth;
    vec3 n0 = texture(gSssNormal, uv).xyz;
    n0 = dot(n0, n0) > 0.0 ? normalize(n0) : vec3(0.0, 0.0, 1.0);
    vec2 texel = 1.0 / vec2(textureSize(sceneColor, 0));
    // The spiral is not turned from pixel to pixel: with 16 taps a per-pixel turn made the wide and the narrow averages
    // differ by noise, and the face came out dotted (found by the frame check).
    float rotation = 0.0;
    vec3 sigma = SSS_CHANNEL_SIGMA * width;
    float narrow = SSS_NARROW_SIGMA * width;
    float reach = SSS_REACH * width;
    vec3 wideSum = centre;
    vec3 wideWeight = vec3(1.0);
    vec3 narrowSum = centre;
    float narrowWeight = 1.0;
    for (int i = 0; i < SSS_TAPS; ++i)
    {
        float r = reach * sqrt((float(i) + 0.5) / float(SSS_TAPS));
        float a = float(i) * 2.39996323 + rotation;
        vec2 tap = uv + vec2(cos(a), sin(a)) * (r * pixelsPerUnit) * texel;
        if (any(lessThan(tap, vec2(0.0))) || any(greaterThan(tap, vec2(1.0))))
            continue;
        if (texture(gSssMaterial, tap).a < 0.5)
            continue;
        float d = texture(gSssDepth, tap).r;
        if (!(d > 0.0))
            continue;
        vec3 n = texture(gSssNormal, tap).xyz;
        n = dot(n, n) > 0.0 ? normalize(n) : n0;
        float v = sssSurface(d - depth, width, dot(n, n0));
        if (v <= 0.0)
            continue;
        vec3 c = texture(sceneColor, tap).rgb;
        vec3 w1 = vec3(sssGaussian(r, sigma.x), sssGaussian(r, sigma.y), sssGaussian(r, sigma.z)) * v;
        float w2 = sssGaussian(r, narrow) * v;
        wideSum += c * w1;
        wideWeight += w1;
        narrowSum += c * w2;
        narrowWeight += w2;
    }
    vec3 gain = clamp((wideSum / wideWeight + SSS_EPSILON) / (narrowSum / narrowWeight + SSS_EPSILON),
        vec3(SSS_GAIN_MIN), vec3(SSS_GAIN_MAX));
    return max(centre * (vec3(1.0) + push.sss.x * (gain - vec3(1.0))), vec3(0.0));
}
#endif

void main()
{
#ifdef DEBUG_VIEW
    // Task 0026: a stored image in place of the scene (the CPU twins are Render::GBuffer::debug*).
    {
        int mode = int(push.debug.x + 0.5);
        vec3 shown = vec3(0.0);
        if (mode == 1)
        {
            vec3 n = texture(gNormal, texCoord).xyz;
            shown = dot(n, n) > 0.0 ? normalize(n) * 0.5 + 0.5 : vec3(0.0);
        }
        else if (mode == 2)
        {
            float d = texture(gLinearDepth, texCoord).r;
            shown = vec3((d > 0.0 && push.debug.y > 0.0) ? 1.0 - exp(-d / push.debug.y) : 0.0);
        }
        else if (mode == 3)
        {
            vec2 v = texture(gVelocity, texCoord).rg;
            shown = vec3(clamp(0.5 + v * push.debug.z, 0.0, 1.0), 0.5);
        }
        else if (mode == 4)
        {
            // Task 0027: roughness is the normal image's w; black where there is no surface.
            vec4 n = texture(gNormal, texCoord);
            shown = vec3(dot(n.xyz, n.xyz) > 0.0 ? n.w : 0.0);
        }
        else if (mode == 5)
            shown = texture(gMaterial, texCoord).rgb; // metalness, reflectance, occlusion
        else if (mode == 6)
            shown = vec3(texture(gMaterial, texCoord).a);
#ifdef AO
        else if (mode == 7)
            shown = vec3(aoApply(aoFiltered(texCoord), push.ao.z));
        else if (mode == 8)
            shown = texture(gAmbient, texCoord).rgb;
#endif
#ifdef SSS
        else if (mode == 9)
        {
            vec3 base = texture(sceneColor, texCoord).rgb;
            shown = clamp(abs(sssApply(texCoord, base) - base) * 8.0, vec3(0.0), vec3(1.0));
        }
#endif
        if (mode != 0)
        {
            outColor = vec4(shown, 1.0);
            return;
        }
    }
#endif
    vec3 color = texture(sceneColor, texCoord).rgb;
#ifdef SSS
    color = sssApply(texCoord, color);
#endif
#ifdef AO
    // Only the ambient part of the colour is darkened (Render::Ao::removeOcclusion).
    color = max(color - texture(gAmbient, texCoord).rgb * (1.0 - aoApply(aoFiltered(texCoord), push.ao.z)), vec3(0.0));
#endif
#ifdef BLOOM
    color += texture(bloomColor, texCoord).rgb * push.params.w;
#endif
    // A NaN would spread through the 8-bit chain after this; draw black instead.
    if (any(isnan(color)))
        color = vec3(0.0);
    color = clamp(color * push.params.x, vec3(0.0), vec3(MAX_TARGET_VALUE));
    if (push.params.y > 1.5)
        color = aces(color);
    else if (push.params.y > 0.5)
        color = reinhard(color);
    color = clamp(color, 0.0, 1.0);
    if (push.params.z > 0.5)
        color = pow(color, vec3(1.0 / DISPLAY_GAMMA));
    outColor = vec4(color, 1.0);
}
)GLSL";
const char* const old_velocity_sVertexShader = R"GLSL(#version 450
layout(location=0) out vec2 texCoord;
out gl_PerVertex { vec4 gl_Position; };
void main()
{
    vec2 position = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
    gl_Position = vec4(position, 0.0, 1.0);
    texCoord = position * 0.5 + 0.5;
}
)GLSL";
const char* const old_velocity_sFragmentShader = R"GLSL(#version 450
layout(set=0, binding=0) uniform sampler2D linearDepth;
layout(set=0, binding=1) uniform sampler2D objectMotion; // skinned actors' own motion, from the scene pass
layout(push_constant) uniform Push
{
    mat4 toPrevClip;
    vec4 projScale; // xy: the projection's [0][0] and [1][1]
} push;
layout(location=0) in vec2 texCoord;
layout(location=0) out vec4 outVelocity;
void main()
{
    float d = texture(linearDepth, texCoord).r;
    outVelocity = vec4(0.0);
    if (!(d > 0.0))
        return;
    vec2 velocity = texture(objectMotion, texCoord).rg;
    vec3 viewPos = vec3((texCoord * 2.0 - 1.0) * d / push.projScale.xy, -d);
    vec4 prevClip = push.toPrevClip * vec4(viewPos, 1.0);
    if (prevClip.w > 0.0)
        velocity += texCoord - (prevClip.xy / prevClip.w * 0.5 + 0.5);
    outVelocity = vec4(velocity, 0.0, 0.0);
}
)GLSL";
