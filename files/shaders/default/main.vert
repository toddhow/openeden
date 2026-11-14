#pragma import_defines (PARTICLE, BILLBOARD, MORPH, SKIN, TEXMAT, VERTEX, NORMAL, COLOR, TEXCOORD, ENV_MAP, INSTANCE, INSTANCE_TINT, NORMAL_MAP, SOFT_PARTICLE, GBUFFER_MOTION, LOD_SWAY)

#include "lib/pushconstants.glsl"
#include "lib/material/descriptors_vert.glsl"
#include "lib/view/scene.glsl"
#include "lib/view/screencoord.glsl"
#include "lib/light/pointdiffuse.glsl"

layout(constant_id=0) const int numUvSets=1;
layout(constant_id=START_UVSET_CONSTANTS+TEXMAT_BINDING) const int texmatUv=0;

#include "lib/attributes.glsl"

#ifdef MORPH
layout(std430, set=TEXTURE_SET, binding=MORPH_BINDING) readonly buffer Morph {
    vec4 morphdata[];
};
layout(std430, set=TEXTURE_SET, binding=MORPH_WEIGHTS_BINDING) readonly buffer MorphWeights {
    float morphweights[];
};
#endif

#ifdef PARTICLE
#include "lib/particle/data.glsl"
#include "lib/particle/util.glsl"
layout(std430, set=TEXTURE_SET, binding=PARTICLE_BINDING) readonly buffer ParticleBuffer{
    Particle particles[];
};
#endif

#ifdef BILLBOARD
#include "lib/math/billboard.glsl"
#endif

#ifdef INSTANCE
#include "lib/groundcover.glsl"
#endif

layout(location=0) out Outputs
{
#include "lib/inout.glsl"
} vert_out;

out gl_PerVertex{ vec4 gl_Position; };

void main()
{
#ifdef MORPH
    vec4 vertex = vec4(0,0,0,1);
    uint numMorphs = uint(morphdata[0].w);
    for (int i=0; i<numMorphs; ++i)
        vertex.xyz += morphweights[i] * morphdata[gl_VertexIndex*numMorphs+i].xyz;
#elif defined(PARTICLE)
    Particle particle = particles[gl_VertexIndex/6];
    /*bool*/ int secondTriangle = gl_VertexIndex/3;
    int vertexIndex = (secondTriangle*3) + ((gl_VertexIndex + secondTriangle) % 3) * (-secondTriangle*2+1);
    vec2 texCoord = vec2(vertexIndex & 1, (vertexIndex >> 1) & 1);
    vec4 vertex = vec4(particle.positionSize.xyz, 1.0);
    float deadFactor = isDead(particle) ? 0 : 1;
    vec3 xAxis = (vec4(1,0,0,0) * pc.modelview).xyz;
    vec3 yAxis = (vec4(0,1,0,0) * pc.modelview).xyz;
    // OpenEden: osgParticle, which drew OpenMW's NIF particles, put the corners at +- size on each axis, so a quad is
    // twice the particle size across (ParticleSystem::drawImplementation, OSG 3.6.5).
    vec2 offsetCoord = (texCoord - 0.5) * 2.0;
    vertex.xyz += (offsetCoord.x * normalize(xAxis) + offsetCoord.y * normalize(yAxis)) * particle.positionSize.w * deadFactor;

    vert_out.texCoord[0] = texCoord;
    vert_out.color = particle.color * deadFactor;
 #ifdef SOFT_PARTICLE
    vert_out.softParticleSize = particle.maxAge.y;
 #endif
#elif defined(VERTEX)
    vec4 vertex = vec4(inVertex, 1.0);
#endif

#ifdef SKIN
    mat4 skinMat =
        inBoneWeights.x * boneMatrices[int(inBoneIndices.x)] +
        inBoneWeights.y * boneMatrices[int(inBoneIndices.y)] +
        inBoneWeights.z * boneMatrices[int(inBoneIndices.z)] +
        inBoneWeights.w * boneMatrices[int(inBoneIndices.w)];
#ifdef GBUFFER_MOTION
    // The second half of the bone array holds the previous frame's matrices (Anim::Skin::keepPreviousMatrices).
    uint boneCount = uint(boneMatrices.length()) / 2u;
    mat4 prevSkinMat =
        inBoneWeights.x * boneMatrices[int(inBoneIndices.x) + int(boneCount)] +
        inBoneWeights.y * boneMatrices[int(inBoneIndices.y) + int(boneCount)] +
        inBoneWeights.z * boneMatrices[int(inBoneIndices.z) + int(boneCount)] +
        inBoneWeights.w * boneMatrices[int(inBoneIndices.w) + int(boneCount)];
    vec4 prevVertex = prevSkinMat * vertex;
#endif
    vertex = skinMat * vertex;
 #ifdef NORMAL
    vec3 normal = (vec4(inNormal,0) * inverse(skinMat)).xyz;
 #endif
 #ifdef NORMAL_MAP
    vec3 tangent = (vec4(inTangent.xyz,0) * inverse(skinMat)).xyz;
 #endif
#elif defined(NORMAL)
    vec3 normal = inNormal;
 #ifdef NORMAL_MAP
    vec3 tangent = inTangent.xyz;
 #endif
#endif

#ifdef INSTANCE
    // OpenEden: OpenMW's groundcover instance transform: rotate, scale, then offset within the chunk.
    float instanceHeight = vertex.z;
#ifdef INSTANCE_TINT
    mat4 instanceRotation = groundcoverTiltRotation(inInstanceRotation.xy, inInstanceRotation.z);
#else
    mat4 instanceRotation = groundcoverRotation(inInstanceRotation);
#endif
    vertex = vec4((instanceRotation * vec4(vertex.xyz * inInstanceOffset.w, 1.0)).xyz + inInstanceOffset.xyz, 1.0);
 #ifdef NORMAL
    normal = mat3(instanceRotation) * normal;
 #endif
 #ifdef NORMAL_MAP
    tangent = mat3(instanceRotation) * tangent;
 #endif
#endif

    vec4 viewPos =
#ifdef BILLBOARD
    billboardModelView(pc.modelview)
#else
    pc.modelview
#endif
     * vertex;

#ifdef INSTANCE
    // Wind and stomping move the vertex in the world's XY plane; beyond the fade-out distance the instance is dropped.
    vec3 worldPos = (scene.invView * viewPos).xyz;
#ifdef INSTANCE_TINT
    float groundcoverTime = scene.time * inInstanceTint.w; // the grass record's wave period
#else
    float groundcoverTime = scene.time;
#endif
    vec2 displacement = groundcoverDisplacement(worldPos, instanceHeight, groundcoverTime, scene.groundcoverPlayer.xyz,
        scene.groundcoverPlayer.w, int(scene.groundcoverParams.y), int(scene.groundcoverParams.z));
    viewPos.xyz += transpose(mat3(scene.invView)) * vec3(displacement, 0.0);
#endif

#if defined(LOD_SWAY) && defined(COLOR)
    {
        // OpenEden (task 0096, stage c): far foliage bends in the wind. texmat[0] holds the wind push in world units at
        // the full weight (xy) and the clock's angle (z), written every frame by MWRender::LodClusters; the vertex's
        // weight is the square root of its share, in the colour alpha (Lod::swayEncodeWeight). Twin: Lod::swayWave.
        vec3 swayWorld = (scene.invView * viewPos).xyz;
        float swayAngle = texmat[0].z + 0.00041 * swayWorld.x + 0.00033 * swayWorld.y;
        float swayWave = 0.5 + 0.35 * sin(swayAngle) + 0.15 * sin(3.0 * swayAngle + 1.3);
        vec2 swayWind = texmat[0].xy;
        vec2 swayPush = swayWind * swayWave + vec2(-swayWind.y, swayWind.x) * (0.25 * cos(swayAngle));
        // texmat[0].w is the distance boost's reference (0: none): the push grows with the distance, 1 to 6 times.
        if (texmat[0].w > 0.0)
            swayPush *= clamp(length(viewPos.xyz) / texmat[0].w, 1.0, 6.0);
        viewPos.xyz += transpose(mat3(scene.invView)) * vec3(swayPush * (inColor.a * inColor.a), 0.0);
    }
#endif

    vert_out.viewPos = viewPos.xyz;
    gl_Position = pc.projection * viewPos;
#ifdef GBUFFER_MOTION
    // The same vertex with the previous frame's bones, through this frame's camera and model: only the bones' motion.
    vec4 prevClip = pc.projection * (pc.modelview * prevVertex);
    vert_out.motion = (prevClip.w > 0.0 && gl_Position.w > 0.0)
        ? 0.5 * (gl_Position.xy / gl_Position.w - prevClip.xy / prevClip.w) : vec2(0.0);
#endif
#ifdef INSTANCE
    if (length((pc.modelview * vec4(inInstanceOffset.xyz, 1.0)).xyz) > scene.groundcoverParams.x)
        gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
#endif

#ifdef TEXCOORD
    for (int i=0; i<numUvSets;++i)
        vert_out.texCoord[i] = inTexCoord[i];
#endif
#ifdef TEXMAT
    vert_out.texCoord[texmatUv] = (texmat * vec4(vert_out.texCoord[texmatUv], 0.0, 1.0)).xy;
#endif
#ifdef NORMAL
    vec3 viewNormal = (vec4(normal,0) * inverse(pc.modelview)).xyz;
    vert_out.viewNormal = viewNormal;
 #ifdef NORMAL_MAP
    // OpenMW's generateTangentSpace: the binormal is N x T scaled by the handedness, from the normalized model-space
    // vectors, and all three go to view space with the normal matrix.
    vec3 modelTangent = normalize(tangent);
    vec3 binormal = cross(normalize(normal), modelTangent) * inTangent.w;
    vert_out.viewTangent = (vec4(modelTangent,0) * inverse(pc.modelview)).xyz;
    vert_out.viewBinormal = (vec4(binormal,0) * inverse(pc.modelview)).xyz;
    vert_out.viewNormal = (vec4(normalize(normal),0) * inverse(pc.modelview)).xyz;
 #endif
 //ifdef ENV_MAP
    vec3 viewVec = normalize(viewPos.xyz);
    vec3 r = reflect( viewVec, viewNormal );
    float m = 2.0 * length(r+vec3(0,0,1));
    vert_out.envUV = r.xy/m + 0.5;
 //endif
    vert_out.pointLightDiffuse = pointLightDiffuse(clipPosToScreenCoord(gl_Position), normalize(viewNormal), viewPos.xyz);
#endif
#ifdef COLOR
 #if defined(LOD_SWAY)
    vert_out.color = vec4(inColor.rgb, 1.0); // the alpha byte carried the sway weight
 #else
    vert_out.color = inColor;
 #endif
 #ifdef INSTANCE_TINT
    vert_out.color.rgb *= inInstanceTint.rgb; // the land colour and the grass record's colour range
 #endif
#endif
}
