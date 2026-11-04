// The values of the GLSL include "material.settings" (Resource::ShaderSettings) as specialization constants, with the
// ids of Resource::settingsConstantTable (task 0072). The pipeline code always sets them; the default is that of
// `[Shaders] clamp lighting` in files/settings-default.cfg. Change this file and the table together.
#ifndef LIB_MATERIAL_MATERIALSETTINGS_SLANG
#define LIB_MATERIAL_MATERIALSETTINGS_SLANG

[vk::constant_id(1080)] const bool cClampLighting = true;

#endif
