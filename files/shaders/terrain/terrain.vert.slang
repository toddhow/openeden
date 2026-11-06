// The terrain's vertex stage, in Slang (task 0072, terrain family): the twin of terrain/main.vert, for TERRAIN_MAPS and
// DEBUG_CHUNKS (terrain.settings) in every combination (terrain/main.vert.sets). It must declare what the GLSL declares
// (checked by SlangPermutations); the statements follow it one for one, and the reasons are in the GLSL. GLSL's
// m[c][r] is m[r][c] here and mul(M, v) is M * v.
import glsl;

#include "lib/pushconstants.slang"
#include "lib/sets.slang"
import lib.view.scene;
import lib.view.screencoord;
#include "lib/light/pointdiffuse.slang"
#include "terrain/bindings.slang"
#include "terrain/data.slang"
#include "terrain/inout.slang"

[[vk::binding(HEIGHTMAP_BINDING, TEXTURE_SET)]] Sampler2D heightMap;
[[vk::binding(NORMALMAP_BINDING, TEXTURE_SET)]] Sampler2D normalMap;

static const float cellWorldSize = 8192;

[[vk::binding(BATCH_DATA_BINDING, TEXTURE_SET)]] ConstantBuffer<Data> data;

struct VertexOutput
{
    TerrainInout io;
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(uint vertexIndexU : SV_VulkanVertexID, uint instanceIndexU : SV_VulkanInstanceID)
{
    int vertexIndex = int(vertexIndexU);
    int instanceIndex = int(instanceIndexU);
    VertexOutput output;
    // Task 0088: see the GLSL.
    int lod = instanceIndex & 0xf;
    int shiftSize = (instanceIndex >> 8) & 0xf;
    int dropped = ((instanceIndex >> 4) & 0xf) | (((instanceIndex >> 12) & 0xf) << 4);
    int numVerts = int(data.batch.numVerts - 1) / (1 << (shiftSize + lod)) + 1;
    int2 vert = int2(vertexIndex / numVerts, vertexIndex % numVerts);
    bool inverted = ((instanceIndex >> 23) & 1) != 0;
    float2 gridPos = float2((instanceIndex >> 16) & 0xf, (instanceIndex >> 24) & 0xff);
    float scale = 1.0 / float(1 << shiftSize);
    float2 uv = scale * (gridPos + float2(vert) / float(numVerts - 1));
    uint texWidth, texHeight;
    heightMap.GetDimensions(texWidth, texHeight);
    int2 texSize = int2(int(texWidth), int(texHeight));
    // One vertex per texel, so the vertex reads its own texel (see the GLSL).
    int2 texel = clamp(int2(round(uv * float2(texSize - 1))), int2(0), texSize - 1);
    float height = heightMap.Load(int3(texel, 0)).x;
    float3 cameraPos = mul(inverse(pc.modelview), float4(0, 0, 0, 1)).xyz;
    float2 cameraCellPos = cameraPos.xy / cellWorldSize;
    float2 offset = (data.batch.origin - cameraCellPos) * cellWorldSize;
    float3 vertex = float3(offset + uv * data.batch.vertexScale, height - cameraPos.z);
    float4 viewPos = mul(pc.modelview, float4(vertex, 0.f));
    viewPos.w = 1;
    output.position = mul(pc.projection, viewPos);

    float3 normal = normalMap.Load(int3(texel, 0)).xyz;
    float3 viewNormal = normalize(mul(inverse(transpose(pc.modelview)), float4(normal, 0)).xyz);
    output.io.viewPos = viewPos.xyz;
    output.io.texCoord = uv;
#ifdef DEBUG_CHUNKS
    output.io.localTexCoord = float2(vert) / float(numVerts - 1);
#endif
    output.io.viewNormal = viewNormal;
    float share = 1.0 - float(dropped) / 255.0;
    output.io.chunkFade = inverted ? -1.0 - share : share;
#ifdef TERRAIN_MAPS
    // OpenMW's terrain tangent frame (see the GLSL).
    float3 modelNormal = normalize(normal);
    float3 modelBinormal = cross(modelNormal, float3(1.0, 0.0, 0.0));
    float3 modelTangent = -normalize(cross(modelNormal, modelBinormal));
    float4x4 normalMatrix = inverse(transpose(pc.modelview));
    output.io.viewTangent = mul(normalMatrix, float4(modelTangent, 0)).xyz;
    output.io.viewBinormal = mul(normalMatrix, float4(modelBinormal, 0)).xyz;
#endif
    output.io.pointLightDiffuse
        = pointLightDiffuse(clipPosToScreenCoord(output.position), viewNormal, viewPos.xyz);
    return output;
}
