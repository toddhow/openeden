#if defined(COLOR) || defined(PARTICLE)
vec4 color;
#endif
vec3 viewPos;
#ifdef NORMAL
vec3 viewNormal;
 //ifdef ENV_MAP
 vec2 envUV;
 //endif
#endif
#ifdef SOFT_PARTICLE
// OpenEden: the particle system's size, for soft particles (task 0012, V4k).
float softParticleSize;
#endif
#ifdef NORMAL_MAP
// OpenEden: the view-space tangent and binormal, which with viewNormal make OpenMW's normalToViewMatrix (task 0012).
vec3 viewTangent;
vec3 viewBinormal;
#endif
vec3 pointLightDiffuse;
// OpenEden: last in the block. Its length is a specialization constant but locations are assigned at compile time, so
// with more UV sets it would overlap whatever follows it, pointLightDiffuse included (task 0012).
#if defined(TEXCOORD) || defined(PARTICLE)
vec2 texCoord[numUvSets];
#endif
