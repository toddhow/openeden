vec2 texCoord;
vec3 viewPos;
vec3 viewNormal;
vec3 pointLightDiffuse;
#ifdef DEBUG_CHUNKS
vec2 localTexCoord;
#endif
#ifdef TERRAIN_MAPS
// OpenEden: the view-space tangent and binormal for terrain normal maps (task 0012, V4i).
vec3 viewTangent;
vec3 viewBinormal;
#endif
// OpenEden (task 0088): the fade code of a live chunk, from the draw's instance index: the share of the dither it keeps
// (1 for a solid one), or, below 0, -1 minus the fade of the chunk that replaces it (it keeps the pixels that one drops).
flat float chunkFade;
