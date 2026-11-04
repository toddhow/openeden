// The sun's glare, vertex stage, in Slang (task 0072, stage b): the twin of sky/sunglare/main.vert (no defines, no
// inputs: a triangle that covers the screen).
[shader("vertex")]
float4 vertMain(uint vertexIndex : SV_VulkanVertexID) : SV_Position
{
    float2 vertex = float2((vertexIndex << 1) & 2, vertexIndex & 2) * 2 - 1;
    return float4(vertex, 0.0, 1.0);
}
