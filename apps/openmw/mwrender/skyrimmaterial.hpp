#ifndef OPENMW_MWRENDER_SKYRIMMATERIAL_HPP
#define OPENMW_MWRENDER_SKYRIMMATERIAL_HPP

// Task 0130 (Skyrim material mapping v1, decision D10): the numbers that turn a Skyrim lighting shader's values
// (NifScene::Flat::MaterialKey, only filled with [Game] neutral world materials) into the engine's material descriptor.
// Pure: no Vulkan or VSG scene objects, so openmw-tests can check it. The engine's PBR path ([Rendering] pbr) reads
// pbr.x (roughness) and pbr.z (reflectance) of the descriptor; both are "derive" markers (below zero) when a material
// says nothing, and this mapping sets them from Skyrim's glossiness and specular colour with the engine's own tier 1
// formulas (Render::Pbr), so a Skyrim surface is lit like a Morrowind surface that has a specular map of the same
// shininess and colour. Without pbr the specular values do nothing: the lit shader reads no material specular, only a specular map.

#include <algorithm>

#include <components/nifscene/flatten.hpp>
#include <components/pipeline/material.hpp>
#include <components/render/materialmath.hpp>

namespace MWRender::SkyrimMaterial
{
    /// The material descriptor for a shape. Only what the engine's lit shader reads is set: the shader's alpha, the PBR
    /// roughness and reflectance (read with [Rendering] pbr; the shader reads no material specular or shininess, only
    /// the specular map) and the emission. A key made without [Game] neutral world materials gives the default material.
    inline Pipeline::Data::Material makeMaterial(const NifScene::Flat::MaterialKey& key)
    {
        Pipeline::Data::Material m = Pipeline::Material::createDefault();
        m.diffuse.a = key.mAlpha;
        const vsg::vec3 specular(key.mSpecularColor[0], key.mSpecularColor[1], key.mSpecularColor[2]);
        const bool hasSpecular = specular.x > 0.f || specular.y > 0.f || specular.z > 0.f;
        if (hasSpecular)
        {
            // The PBR-leaning remap (decision D10): Blinn-Phong exponent to GGX roughness, specular colour to reflectance.
            m.pbr.x = Render::Pbr::roughnessFromShininess(key.mGlossiness);
            m.pbr.z = Render::Pbr::reflectanceFromSpecular(
                vsg::vec3(std::min(specular.x, 1.f), std::min(specular.y, 1.f), std::min(specular.z, 1.f)));
        }
        m.emissive = vsg::vec4(key.mEmissive[0], key.mEmissive[1], key.mEmissive[2], 1.f);
        return m;
    }
}

#endif
