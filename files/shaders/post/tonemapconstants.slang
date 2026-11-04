// The constants of the tonemap pass in Slang (task 0072, stage g): the twin of post/tonemapconstants.glsl, which repeats
// Render::Hdr (components/render/hdrmath.hpp) and Render::Sss (sssmath.hpp); written as the GLSL writes them and read
// back by RenderTonemapConstants. Change all of them together.
#ifndef POST_TONEMAPCONSTANTS_SLANG
#define POST_TONEMAPCONSTANTS_SLANG

static const float3 SSS_CHANNEL_SIGMA = float3(1, 0.449999988, 0.200000003);
static const float SSS_NARROW_SIGMA = 0.119999997;
static const float SSS_REACH = 3;
static const int SSS_TAPS = 16;
static const float SSS_EPSILON = 0.00100000005;
static const float SSS_GAIN_MIN = 0.5;
static const float SSS_GAIN_MAX = 2;
static const float DISPLAY_GAMMA = 2.20000005;
static const float REINHARD_WHITE = 4;
static const float MAX_TARGET_VALUE = 65504;

#endif
