// The Slang twin of lib/light/directional.glsl (task 0072): the sun's diffuse light. Include after lib/view/scene.slang.
// Change both together.
#ifndef LIB_LIGHT_DIRECTIONAL_SLANG
#define LIB_LIGHT_DIRECTIONAL_SLANG

import lib.light.equations;
import lib.view.scene;

float3 directionalDiffuse(float3 viewNormal)
{
    return scene.lightDiffuse.xyz * lambert(scene.lightViewPos.xyz, viewNormal);
}

#endif
