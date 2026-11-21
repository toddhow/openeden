struct Batch
{
    vec2 origin;
    /*int*/ float numVerts;
    float vertexScale;
    // OpenEden (task 0087): the share of the dither this draw keeps (1: all of it, no pixel dropped). A world LOD
    // terrain tile that appears or goes away dithers in or out over a fraction of a second; the fragment stage reads
    // it. The three floats after it keep the block at 32 bytes (std140 rounds a struct up to 16).
    float fade;
    float pad0;
    float pad1;
    float pad2;
};
