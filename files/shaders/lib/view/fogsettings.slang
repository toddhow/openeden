// The values of fog.settings (Resource::ShaderSettings) for the Slang twins (task 0072, stage b). The GLSL gets them as
// constants written into its source; a twin is compiled at build time, so they are specialization constants with
// the ids of Resource::settingsConstantTable (components/resource/shadersettings.cpp), and the pipeline code gives the
// values (vsgUtil::addSettingsConstants). Change the ids and the table together; the defaults are the values with
// every option off.
#ifndef LIB_VIEW_FOGSETTINGS_SLANG
#define LIB_VIEW_FOGSETTINGS_SLANG

[vk::constant_id(1000)] const bool cRadialFog = false;
[vk::constant_id(1001)] const bool cExponentialFog = false;
[vk::constant_id(1002)] const bool cSunGlow = false;
[vk::constant_id(1003)] const bool cSunGlowDebug = false;
[vk::constant_id(1004)] const float cSunGlowStrength = 0.0;
[vk::constant_id(1005)] const bool cVolumetrics = false;
[vk::constant_id(1006)] const bool cVolumetricsDebug = false;
[vk::constant_id(1007)] const bool cAerialHaze = false;
[vk::constant_id(1008)] const float cAerialVisibility = 1.0;
[vk::constant_id(1009)] const float cAerialScaleHeight = 1.0;
[vk::constant_id(1010)] const float cAerialWeatherGain = 0.0;

#endif
