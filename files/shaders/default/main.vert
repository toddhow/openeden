#pragma import_defines (PARTICLE, BILLBOARD, MORPH, SKIN, TEXMAT, VERTEX, NORMAL, COLOR, TEXCOORD, ENV_MAP, INSTANCE)

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
#elif defined(VERTEX)
    vec4 vertex = vec4(inVertex, 1.0);
#endif

#ifdef SKIN
    mat4 skinMat =
        inBoneWeights.x * boneMatrices[int(inBoneIndices.x)] +
        inBoneWeights.y * boneMatrices[int(inBoneIndices.y)] +
        inBoneWeights.z * boneMatrices[int(inBoneIndices.z)] +
        inBoneWeights.w * boneMatrices[int(inBoneIndices.w)];
    vertex = skinMat * vertex;
 #ifdef NORMAL
    vec3 normal = (vec4(inNormal,0) * inverse(skinMat)).xyz;
 #endif
#elif defined(NORMAL)
    vec3 normal = inNormal;
#endif

#ifdef INSTANCE
    // OpenEden: OpenMW's groundcover instance transform: rotate, scale, then offset within the chunk.
    float instanceHeight = vertex.z;
    mat4 instanceRotation = groundcoverRotation(inInstanceRotation);
    vertex = vec4((instanceRotation * vec4(vertex.xyz * inInstanceOffset.w, 1.0)).xyz + inInstanceOffset.xyz, 1.0);
 #ifdef NORMAL
    normal = mat3(instanceRotation) * normal;
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
    vec2 displacement = groundcoverDisplacement(worldPos, instanceHeight, scene.time, scene.groundcoverPlayer.xyz,
        scene.groundcoverPlayer.w, int(scene.groundcoverParams.y), int(scene.groundcoverParams.z));
    viewPos.xyz += transpose(mat3(scene.invView)) * vec3(displacement, 0.0);
#endif

    vert_out.viewPos = viewPos.xyz;
    gl_Position = pc.projection * viewPos;
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
 //ifdef ENV_MAP
    vec3 viewVec = normalize(viewPos.xyz);
    vec3 r = reflect( viewVec, viewNormal );
    float m = 2.0 * length(r+vec3(0,0,1));
    vert_out.envUV = r.xy/m + 0.5;
 //endif
    vert_out.pointLightDiffuse = pointLightDiffuse(clipPosToScreenCoord(gl_Position), normalize(viewNormal), viewPos.xyz);
#endif
#ifdef COLOR
    vert_out.color = inColor;
#endif
}
