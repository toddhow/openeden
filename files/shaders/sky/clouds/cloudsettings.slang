// The values of cloud.settings (Resource::ShaderSettings) for the Slang twin of the clouds (task 0072, stage b), as
// specialization constants with the ids of Resource::settingsConstantTable; change both together.
#ifndef SKY_CLOUDS_CLOUDSETTINGS_SLANG
#define SKY_CLOUDS_CLOUDSETTINGS_SLANG

[vk::constant_id(1020)] const bool cCloudsLit = false;
[vk::constant_id(1021)] const bool cVolumetricClouds = false;

#endif
