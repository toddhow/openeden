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
