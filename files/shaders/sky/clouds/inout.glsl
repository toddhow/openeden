vec4 color;
#ifdef TEXCOORD
vec2 texCoord;
#endif
// OpenEden (task 0038): the vertex in the dome's own space, and the direction to the sun in it, for the lit look.
vec3 domeDir;
vec3 sunLocal;
