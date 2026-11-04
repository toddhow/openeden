// The values of the GLSL include "light.settings" (Resource::ShaderSettings) as specialization constants, with the ids
// of Resource::settingsConstantTable (task 0072). The pipeline code always sets them; the defaults are those of the
// fallback lines in files/openmw.cfg. Change this file and the table together.
#ifndef LIB_LIGHT_LIGHTSETTINGS_SLANG
#define LIB_LIGHT_LIGHTSETTINGS_SLANG

[vk::constant_id(1040)] const bool LightAttenuation_UseConstant = false;
[vk::constant_id(1041)] const float LightAttenuation_ConstantValue = 0.0;
[vk::constant_id(1042)] const bool LightAttenuation_UseLinear = true;
[vk::constant_id(1043)] const int LightAttenuation_LinearMethod = 1;
[vk::constant_id(1044)] const float LightAttenuation_LinearValue = 3.0;
[vk::constant_id(1045)] const float LightAttenuation_LinearRadiusMult = 1.0;
[vk::constant_id(1046)] const bool LightAttenuation_UseQuadratic = false;
[vk::constant_id(1047)] const int LightAttenuation_QuadraticMethod = 2;
[vk::constant_id(1048)] const float LightAttenuation_QuadraticValue = 16.0;
[vk::constant_id(1049)] const float LightAttenuation_QuadraticRadiusMult = 1.0;
[vk::constant_id(1050)] const bool cClassicFalloff = false;

#endif
