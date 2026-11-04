// The Slang twin of terrain/data.glsl (task 0072): a terrain batch's uniform. The GLSL block `Data` holds one member,
// `batch`, so the twin's block does too. Change both together.
#ifndef TERRAIN_DATA_SLANG
#define TERRAIN_DATA_SLANG

struct Batch
{
    float2 origin;
    /*int*/ float numVerts;
    float vertexScale;
};

struct Data
{
    Batch batch;
};

#endif
