// The object shader's vertex stage, in Slang (task 0072, stage f): the twin of default/main.vert. It must declare what
// the GLSL declares with the same defines (checked by SlangPermutations); the define sets with a file are listed in
// default/main.vert.sets. A set with TEXCOORD or PARTICLE names its count of texture coordinate sets (UVSETS_<n>,
// lib/uvsets.slang), which the GLSL takes as the specialization constant numUvSets; the pipeline chooses the twin of its
// count. The statements follow the GLSL one for one and its comments give the reasons; change both together. GLSL's
// M * v is mul(M, v) and v * M is mul(v, M); mat3(m) of a mat4 is (float3x3)m; inverse comes from the glsl module.
import glsl;

#include "lib/pushconstants.slang"
#include "lib/material/descriptors_vert.slang"
import lib.view.scene;
import lib.view.screencoord;
#include "lib/light/pointdiffuse.slang"

[[vk::constant_id(START_UVSET_CONSTANTS + TEXMAT_BINDING)]] const int texmatUv = 0;

#include "lib/attributes.slang"

#ifdef MORPH
[[vk::binding(MORPH_BINDING, TEXTURE_SET)]] StructuredBuffer<float4> morphdata;
[[vk::binding(MORPH_WEIGHTS_BINDING, TEXTURE_SET)]] StructuredBuffer<float> morphweights;
#endif

#ifdef PARTICLE
import lib.particle.data;
[[vk::binding(PARTICLE_BINDING, TEXTURE_SET)]] StructuredBuffer<Particle> particles;
#endif

#ifdef BILLBOARD
import lib.math.billboard;
#endif

#ifdef INSTANCE
#include "lib/groundcover.slang"
#endif

#include "lib/inout.slang"

struct VertexOutput
{
    CommonInout io;
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(VertexAttributes input, uint vertexIndexU : SV_VulkanVertexID)
{
    VertexOutput output;
    int vertexIndexI = int(vertexIndexU);
#ifdef MORPH
    float4 vertex = float4(0, 0, 0, 1);
    uint numMorphs = uint(morphdata[0].w);
    for (int i = 0; uint(i) < numMorphs; ++i)
        vertex.xyz += morphweights[i] * morphdata[vertexIndexU * numMorphs + uint(i)].xyz;
#elif defined(PARTICLE)
    Particle particle = particles[vertexIndexI / 6];
    int secondTriangle = vertexIndexI / 3;
    int vertexIndex = (secondTriangle * 3) + ((vertexIndexI + secondTriangle) % 3) * (-secondTriangle * 2 + 1);
    float2 texCoord = float2(float(vertexIndex & 1), float((vertexIndex >> 1) & 1));
    float4 vertex = float4(particle.positionSize.xyz, 1.0);
    float deadFactor = isDead(particle) ? 0 : 1;
    float3 xAxis = mul(float4(1, 0, 0, 0), pc.modelview).xyz;
    float3 yAxis = mul(float4(0, 1, 0, 0), pc.modelview).xyz;
    // osgParticle's corners at +- size on each axis (see the GLSL).
    float2 offsetCoord = (texCoord - 0.5) * 2.0;
    vertex.xyz
        += (offsetCoord.x * normalize(xAxis) + offsetCoord.y * normalize(yAxis)) * particle.positionSize.w * deadFactor;

    output.io.texCoord[0] = texCoord;
    output.io.color = particle.color * deadFactor;
#ifdef SOFT_PARTICLE
    output.io.softParticleSize = particle.maxAge.y;
#endif
#elif defined(VERTEX)
    float4 vertex = float4(input.inVertex, 1.0);
#endif

#ifdef SKIN
    float4x4 skinMat = input.inBoneWeights.x * boneMatrices[int(input.inBoneIndices.x)]
        + input.inBoneWeights.y * boneMatrices[int(input.inBoneIndices.y)]
        + input.inBoneWeights.z * boneMatrices[int(input.inBoneIndices.z)]
        + input.inBoneWeights.w * boneMatrices[int(input.inBoneIndices.w)];
#ifdef GBUFFER_MOTION
    // The second half of the bone array holds the previous frame's matrices (Anim::Skin::keepPreviousMatrices).
    uint boneArrayLength, boneStride;
    boneMatrices.GetDimensions(boneArrayLength, boneStride);
    uint boneCount = boneArrayLength / 2u;
    float4x4 prevSkinMat = input.inBoneWeights.x * boneMatrices[int(input.inBoneIndices.x) + int(boneCount)]
        + input.inBoneWeights.y * boneMatrices[int(input.inBoneIndices.y) + int(boneCount)]
        + input.inBoneWeights.z * boneMatrices[int(input.inBoneIndices.z) + int(boneCount)]
        + input.inBoneWeights.w * boneMatrices[int(input.inBoneIndices.w) + int(boneCount)];
    float4 prevVertex = mul(prevSkinMat, vertex);
#endif
    vertex = mul(skinMat, vertex);
#ifdef NORMAL
    float3 normal = mul(float4(input.inNormal, 0), inverse(skinMat)).xyz;
#endif
#ifdef NORMAL_MAP
    float3 tangent = mul(float4(input.inTangent.xyz, 0), inverse(skinMat)).xyz;
#endif
#elif defined(NORMAL)
    float3 normal = input.inNormal;
#ifdef NORMAL_MAP
    float3 tangent = input.inTangent.xyz;
#endif
#endif

#ifdef INSTANCE
    // OpenMW's groundcover instance transform: rotate, scale, then offset within the chunk.
    float instanceHeight = vertex.z;
    float4x4 instanceRotation = groundcoverRotation(input.inInstanceRotation);
    vertex = float4(mul(instanceRotation, float4(vertex.xyz * input.inInstanceOffset.w, 1.0)).xyz
            + input.inInstanceOffset.xyz,
        1.0);
#ifdef NORMAL
    normal = mul((float3x3)instanceRotation, normal);
#endif
#ifdef NORMAL_MAP
    tangent = mul((float3x3)instanceRotation, tangent);
#endif
#endif

#ifdef BILLBOARD
    float4 viewPos = mul(billboardModelView(pc.modelview), vertex);
#else
    float4 viewPos = mul(pc.modelview, vertex);
#endif

#ifdef INSTANCE
    // Wind and stomping move the vertex in the world's XY plane; beyond the fade-out distance the instance is dropped.
    float3 worldPos = mul(scene.invView, viewPos).xyz;
    float2 displacement = groundcoverDisplacement(worldPos, instanceHeight, scene.time, scene.groundcoverPlayer.xyz,
        scene.groundcoverPlayer.w, int(scene.groundcoverParams.y), int(scene.groundcoverParams.z));
    viewPos.xyz += mul(transpose((float3x3)scene.invView), float3(displacement, 0.0));
#endif

    output.io.viewPos = viewPos.xyz;
    output.position = mul(pc.projection, viewPos);
#ifdef GBUFFER_MOTION
    // The same vertex with the previous frame's bones, through this frame's camera and model.
    float4 prevClip = mul(pc.projection, mul(pc.modelview, prevVertex));
    output.io.motion = (prevClip.w > 0.0 && output.position.w > 0.0)
        ? 0.5 * (output.position.xy / output.position.w - prevClip.xy / prevClip.w)
        : float2(0.0);
#endif
#ifdef INSTANCE
    if (length(mul(pc.modelview, float4(input.inInstanceOffset.xyz, 1.0)).xyz) > scene.groundcoverParams.x)
        output.position = float4(0.0, 0.0, 0.0, 1.0);
#endif

#ifdef TEXCOORD
    for (int i = 0; i < NUM_UV_SETS; ++i)
        output.io.texCoord[i] = input.inTexCoord[i];
#endif
#ifdef TEXMAT
    output.io.texCoord[texmatUv] = mul(texMat.texmat, float4(output.io.texCoord[texmatUv], 0.0, 1.0)).xy;
#endif
#ifdef NORMAL
    float3 viewNormal = mul(float4(normal, 0), inverse(pc.modelview)).xyz;
    output.io.viewNormal = viewNormal;
#ifdef NORMAL_MAP
    // OpenMW's generateTangentSpace (see the GLSL).
    float3 modelTangent = normalize(tangent);
    float3 binormal = cross(normalize(normal), modelTangent) * input.inTangent.w;
    output.io.viewTangent = mul(float4(modelTangent, 0), inverse(pc.modelview)).xyz;
    output.io.viewBinormal = mul(float4(binormal, 0), inverse(pc.modelview)).xyz;
    output.io.viewNormal = mul(float4(normalize(normal), 0), inverse(pc.modelview)).xyz;
#endif
    float3 viewVec = normalize(viewPos.xyz);
    float3 r = reflect(viewVec, viewNormal);
    float m = 2.0 * length(r + float3(0, 0, 1));
    output.io.envUV = r.xy / m + 0.5;
    output.io.pointLightDiffuse
        = pointLightDiffuse(clipPosToScreenCoord(output.position), normalize(viewNormal), viewPos.xyz);
#endif
#ifdef COLOR
    output.io.color = input.inColor;
#endif
    return output;
}
