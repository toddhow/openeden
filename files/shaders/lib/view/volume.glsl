// OpenEden (task 0041): the reader of the volumetric fog. Include after lib/view/scene.glsl and lib/view/linear.glsl.
//
// The fog is a 3D image (binding 14) that files/shaders/comp/volume/volume.slang fills each frame: x and y follow the
// screen, z the distance along the view axis in slices that are thin near the camera (components/render/volumemath.hpp,
// sliceDepth and sliceCoordinate: change both together). Each texel holds the light the fog scatters towards the eye
// from the camera up to the far side of its slice (rgb) and the share of the picture behind it that still gets
// through (a). Only the main view with the linear HDR path has a real one: every other view binds a stand-in whose
// params say "off" (grid.w = 0), and cVolumetrics (fog.settings) is false unless [Rendering] volumetrics is on.
layout(set=VIEW_SET, binding=VIEW_VOLUME_BINDING) uniform sampler3D volumeFog;
layout(std140, set=VIEW_SET, binding=VIEW_VOLUME_PARAMS_BINDING) uniform VolumeParams {
    vec4 grid;   // x, y, z: the froxel counts; w: 1 when the volume is on
    vec4 range;  // x: nearest slice depth, y: farthest, z: log(far / near)
    vec4 fog;    // x: density scale, y: 1 / height scale (0: none), z: unused, w: anisotropy
    vec4 sun;    // x: sun gain, y: shadow maximum distance, z: shadow fade start, w: shadow cascade count
    vec4 lamps;  // x: lamp gain, y: shadow bias, z: 1 for the debug view
    vec4 jitter; // x, y, z: this frame's sample offsets, w: history weight
    mat4 toPreviousClip;
} volumeParams;

bool volumeActive()
{
    return cVolumetrics && volumeParams.grid.w > 0.5 && linearView();
}

// The same density as the compute shader's fogDensity() (and Render::Volume::fogExtinction).
float volumeDensity()
{
    if (scene.fogScale <= 0.0)
        return 0.0;
    float fogEnd = scene.fogStart + 1.0 / scene.fogScale;
    return volumeParams.fog.x * 2.0 / max(fogEnd - scene.fogStart * 0.5, 1.0);
}

// `src` seen through the fog between the eye and the point `viewPos` (view space). The fog is mixed in the display
// encoding like the old fog, as colour = mix(src, fogTint, 1 - transmittance), so a volume with only the weather's fog
// colour in it looks as the old exponential fog did.
vec3 applyVolumeFog(vec3 src, vec3 viewPos)
{
    float depth = max(-viewPos.z, 1e-3);
    vec2 uv = gl_FragCoord.xy / scene.resolution;
    float slices = volumeParams.grid.z;
    float volumeFar = volumeParams.range.y;
    float coordinate = clamp(log(depth / volumeParams.range.x) / volumeParams.range.z, 0.0, 1.0) * slices;
    // Texel i holds the fog up to the far side of slice i, at texture position i + 0.5, so slice coordinate c is at
    // position c - 0.5; before the first texel the fog blends in from nothing at the camera.
    vec4 v = textureLod(volumeFog, vec3(uv, (max(coordinate, 1.0) - 0.5) / slices), 0.0);
    v = mix(vec4(0.0, 0.0, 0.0, 1.0), v, min(coordinate, 1.0));
    float transmittance = v.a;
    float scattering = 1.0 - transmittance;
    vec3 tint = scattering > 1e-4 ? v.rgb / scattering : scene.fogColor.xyz;
    // Beyond the grid the same density goes on, towards the same tint, so the horizon joins the old fog.
    // `volumetrics debug`: the fog alone, holding the sun's shadow (white lit, dark shadowed) instead of light.
    if (cVolumetricsDebug)
        return tint;
    transmittance *= exp(-volumeDensity() * max(depth - volumeFar, 0.0));
    return toLinear(mix(toEncoded(src), toEncoded(tint), 1.0 - transmittance));
}
