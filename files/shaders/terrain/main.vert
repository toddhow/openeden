#pragma import_defines (TERRAIN_MAPS)
#include "lib/pushconstants.glsl"
#include "lib/view/scene.glsl"
#include "lib/view/screencoord.glsl"
#include "lib/light/pointdiffuse.glsl"

#include "bindings.glsl"
#include "data.glsl"
#include "terrain.settings"

layout(set=TEXTURE_SET, binding=HEIGHTMAP_BINDING) uniform sampler2D heightMap;
layout(set=TEXTURE_SET, binding=NORMALMAP_BINDING) uniform sampler2D normalMap;

layout(location=0) out Outputs
{
#include "inout.glsl"
} vert_out;

const float cellWorldSize = 8192;

layout(std140, set=TEXTURE_SET, binding=BATCH_DATA_BINDING) uniform Data {
    Batch batch;
};

out gl_PerVertex{ vec4 gl_Position; };
void main()
{
    // OpenEden (task 0088): lod and shift are the low nibbles of their bytes; the high nibbles hold the share of the dither
    // a fading live chunk drops (255 is all of it), see Terrain::encodeChunkFade.
    int lod = gl_InstanceIndex & 0xf;
    int shiftSize = (gl_InstanceIndex >> 8) & 0xf;
    int dropped = ((gl_InstanceIndex >> 4) & 0xf) | (((gl_InstanceIndex >> 12) & 0xf) << 4);
    int numVerts = int(batch.numVerts-1) / (1 << (shiftSize + lod)) + 1;
    ivec2 vert = ivec2(gl_VertexIndex / numVerts, gl_VertexIndex % numVerts);
    // Bit 23 says the share is INVERTED (task 0088, the complementary dither): the chunk keeps the pixels that the
    // incoming chunk, with the fade in the nibbles, drops. The grid x is at most 127 (a batch of 16 cells over 1/8 cell chunks), bits 16 to 22.
    bool inverted = ((gl_InstanceIndex >> 23) & 1) != 0;
    vec2 gridPos = vec2((gl_InstanceIndex >> 16) & 0x7f, (gl_InstanceIndex >> 24) & 0xff);
    float scale = 1.0 / (1 << shiftSize);
    vec2 uv = scale * (gridPos + vert / float(numVerts-1));
    ivec2 texSize = textureSize(heightMap, 0);
    // One vertex per texel, so the vertex reads its own texel. The sampler repeats and filters, and the old normalised
    // coordinate (uv - 0.5) * size / (size + 1) + 0.5 put the first and last vertex half a texel short of the texel
    // centre, blending in the opposite side of the batch: a border height wrong by a fraction of the whole batch's
    // relief, and different in the neighbouring batch, which is what opened the crack (task 0012, terrain crack).
    ivec2 texel = clamp(ivec2(round(uv * vec2(texSize - 1))), ivec2(0), texSize - 1);
    float height = texelFetch(heightMap, texel, 0).x;
    vec3 cameraPos = (inverse(pc.modelview) * vec4(0,0,0,1)).xyz;
    vec2 cameraCellPos = cameraPos.xy / cellWorldSize; // = scene.cameraCellCalculatedFromDoubleMatrix;
    vec2 offset = (batch.origin - cameraCellPos) * cellWorldSize;
    vec3 vertex = vec3(offset + uv * batch.vertexScale, height - cameraPos.z);
    vec4 viewPos = pc.modelview * vec4(vertex, 0.f);
    viewPos.w = 1;
    gl_Position = pc.projection * viewPos;

    vec3 normal = texelFetch(normalMap, texel, 0).xyz;
    vec3 viewNormal = normalize((inverse(transpose(pc.modelview)) * vec4(normal, 0)).xyz);
    vert_out.viewPos = viewPos.xyz;
    vert_out.texCoord = uv;
    #ifdef DEBUG_CHUNKS
    vert_out.localTexCoord = vert / float(numVerts-1);
    #endif
    vert_out.viewNormal = viewNormal;
    float share = 1.0 - float(dropped) / 255.0;
    vert_out.chunkFade = inverted ? -1.0 - share : share;
#ifdef TERRAIN_MAPS
    // OpenMW's terrain tangent frame (compatibility/terrain.vert): (1, 0, 0) through generateTangentSpace, then the
    // tangent re-derived at right angles to the normal.
    vec3 modelNormal = normalize(normal);
    vec3 modelBinormal = cross(modelNormal, vec3(1.0, 0.0, 0.0));
    vec3 modelTangent = -normalize(cross(modelNormal, modelBinormal));
    mat4 normalMatrix = inverse(transpose(pc.modelview));
    vert_out.viewTangent = (normalMatrix * vec4(modelTangent, 0)).xyz;
    vert_out.viewBinormal = (normalMatrix * vec4(modelBinormal, 0)).xyz;
#endif
    vert_out.pointLightDiffuse = pointLightDiffuse(clipPosToScreenCoord(gl_Position), viewNormal, viewPos.xyz);
}
