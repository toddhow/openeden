// The values of the GLSL include "shadow.settings" (Resource::ShaderSettings) as specialization constants, with the
// ids of Resource::settingsConstantTable (task 0072). Its "#define" lines (CLOUD_SHADOW, CLOUD_SHADOW_DEBUG,
// VOLUME_CLOUD_SHADOW, DEBUG_SHADOW) are part of a stage's define set instead. The pipeline code always sets the values;
// the defaults are those of files/settings-default.cfg. Change this file and the table together.
#ifndef LIB_VIEW_SHADOWSETTINGS_SLANG
#define LIB_VIEW_SHADOWSETTINGS_SLANG

[vk::constant_id(1060)] const bool cEnableShadows = false;
[vk::constant_id(1061)] const bool cPCSS = false;
[vk::constant_id(1062)] const int cShadowMapCount = 3;
[vk::constant_id(1063)] const int cBlockerSamples = 16;
[vk::constant_id(1064)] const int cShadowSamples = 16;
[vk::constant_id(1065)] const float cShadowMaxDist = 8192.0;
[vk::constant_id(1066)] const float cShadowFadeStart = 0.9;

#endif
