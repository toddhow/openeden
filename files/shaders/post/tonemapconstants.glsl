// The constants of the tonemap pass (post/tonemap.frag), which repeat Render::Hdr (components/render/hdrmath.hpp) and
// Render::Sss (sssmath.hpp). Task 0072, stage g: tonemap.cpp used to write them in from there with nine significant
// digits; they are written here as it wrote them (the GLSL compiler folds 1.0 / DISPLAY_GAMMA in double precision, so
// the text is kept, not only the value), and RenderTonemapConstants reads them back. Change all of them together, and
// post/tonemapconstants.slang.
#define SSS_CHANNEL_SIGMA vec3(1, 0.449999988, 0.200000003)
#define SSS_NARROW_SIGMA 0.119999997
#define SSS_REACH 3
#define SSS_TAPS 16
#define SSS_EPSILON 0.00100000005
#define SSS_GAIN_MIN 0.5
#define SSS_GAIN_MAX 2
#define DISPLAY_GAMMA 2.20000005
#define REINHARD_WHITE 4
#define MAX_TARGET_VALUE 65504
