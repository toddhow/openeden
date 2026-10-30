#if defined(COLOR) || defined(PARTICLE)
vec4 color;
#endif
#if defined(TEXCOORD) || defined(PARTICLE)
vec2 texCoord[numUvSets];
#endif
vec3 viewPos;
#ifdef NORMAL
vec3 viewNormal;
 //ifdef ENV_MAP
 vec2 envUV;
 //endif
#endif
#ifdef NORMAL_MAP
// OpenEden: the view-space tangent and binormal, which with viewNormal make OpenMW's normalToViewMatrix (task 0012).
vec3 viewTangent;
vec3 viewBinormal;
#endif
vec3 pointLightDiffuse;
