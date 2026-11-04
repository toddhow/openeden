// The number of texture coordinate sets an object shader twin is built for (task 0072, stage f), from the set's
// UVSETS_<n> name (vsgUtil::uvSetsDefine). The GLSL takes the number as the specialization constant numUvSets (id 0)
// and sizes the vertex input and the varyings of the sets by it; a twin needs a length known when it is compiled, so it
// is built once per count and the pipeline, which knows its count, chooses it (Pipeline::Graphics::create). Counts of 1
// to 8 can be built; without such a name NUM_UV_SETS stays undefined.
#ifndef LIB_UVSETS_SLANG
#define LIB_UVSETS_SLANG

#if defined(UVSETS_1)
#define NUM_UV_SETS 1
#elif defined(UVSETS_2)
#define NUM_UV_SETS 2
#elif defined(UVSETS_3)
#define NUM_UV_SETS 3
#elif defined(UVSETS_4)
#define NUM_UV_SETS 4
#elif defined(UVSETS_5)
#define NUM_UV_SETS 5
#elif defined(UVSETS_6)
#define NUM_UV_SETS 6
#elif defined(UVSETS_7)
#define NUM_UV_SETS 7
#elif defined(UVSETS_8)
#define NUM_UV_SETS 8
#endif

#endif
