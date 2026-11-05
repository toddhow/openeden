// The Slang twin of terrain/data.glsl (task 0072): a terrain batch's uniform. The GLSL block `Data` holds one member,
// `batch`, so the twin's block does too. Change both together.
#ifndef TERRAIN_DATA_SLANG
#define TERRAIN_DATA_SLANG

struct Batch
{
    float2 origin;
    /*int*/ float numVerts;
    float vertexScale;
    // Task 0087: see data.glsl. The padding keeps the block at the GLSL's 32 bytes.
    float fade;
    float pad0;
    float pad1;
    float pad2;
};

struct Data
{
    Batch batch;
};

#endif
