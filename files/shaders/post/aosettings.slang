// The values of the GLSL include "ao.settings" (Resource::ShaderSettings: [Rendering] ao radius, ao slices and ao steps)
// as specialization constants, with the ids of Resource::settingsConstantTable (task 0072, stage g). The pipeline code
// always sets them; the defaults are those of files/settings-default.cfg. Change this file and the table together.
#ifndef POST_AOSETTINGS_SLANG
#define POST_AOSETTINGS_SLANG

[vk::constant_id(1100)] const float AO_RADIUS = 100.0;
[vk::constant_id(1101)] const int AO_SLICES = 2;
[vk::constant_id(1102)] const int AO_STEPS = 6;

#endif
