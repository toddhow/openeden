#ifndef VSGOPENMW_PIPELINE_GRAPHICS_H
#define VSGOPENMW_PIPELINE_GRAPHICS_H

#include <vsg/state/GraphicsPipeline.h>

#include <components/vsgutil/cache.hpp>

#include "options.hpp"

namespace Pipeline
{
    // openeden-vsg: which alpha-to-coverage shader modes a draw gets (task 0012, V4j).
    struct CoverageModes
    {
        bool alphaToCoverage = false;
        bool adjustCoverage = false;
    };

    /// As OpenMW's ShaderVisitor: only alpha-tested, unblended draws of the default shader with a material (which holds
    /// the cutoff) get either mode, and only when its switch is on.
    CoverageModes coverageModes(const Options& key, bool alphaToCoverage, bool adjustCoverage);

    /*
     * Creates general purpose graphics pipelines.
     */
    class Graphics
    {
        vsg::ref_ptr<const vsg::Options> mShaderOptions;
        vsgUtil::RefCache<Options, vsg::BindGraphicsPipeline> mPipelineCache;
        class VertexStageOptions : public ShaderOptions
        {
        };
        class FragmentStageOptions : public ShaderOptions
        {
        };
        vsgUtil::RefCache<VertexStageOptions, vsg::ShaderStage> mVertexStageCache;
        vsgUtil::RefCache<FragmentStageOptions, vsg::ShaderStage> mFragmentStageCache;
        vsg::ref_ptr<vsg::PipelineLayout> mLayout;

    public:
        Graphics(vsg::ref_ptr<const vsg::Options> readShaderOptions);
        ~Graphics();

        // openeden-vsg: `[Shaders] antialias alpha test` (with multisampling) and `adjust coverage for alpha test`;
        // set before the first pipeline is created.
        bool alphaToCoverage = false;
        bool adjustCoverage = false;
        // openeden (task 0023, F1b): `[Rendering] slang shaders`. When set, a shader that has a precompiled SPIR-V file
        // (<path>/main.vert.spv or main.frag.spv, built from Slang) next to its GLSL uses that file in place of the
        // GLSL. The GLSL is still read first, for its stage and entry point, and stays the fallback when there is no
        // .spv.
        bool preferSlang = false;
        // openeden (task 0026, F3): how many colour outputs the main pass has beyond the scene's own; 0 (the default)
        // leaves every pipeline as it was. Set by MWRender::RenderManager before the first pipeline is created.
        unsigned int gbufferOutputs = 0;
        // openeden (task 0027): 0 leaves the lighting as it was, 1 the vanilla look of physically based lighting, 2 the
        // physical look. With gbufferOutputs of 4 the draws that write the G-buffer also write the material. Set before
        // the first pipeline is created.
        unsigned int pbrLook = 0;
        // Task 0028: lit draws take their ambient light from the baked light probes (a buffer that may hold no volume).
        bool giEnabled = false;
        // Task 0036: lit draws also scale the ambient light by the exterior sky-visibility field (implies the GI
        // define).
        bool giSkyEnabled = false;
        // Task 0037: lit draws get rain wetness, with puddles, and the debug view of it (needs pbrLook above zero).
        bool wetnessEnabled = false;
        bool wetnessPuddles = false;
        bool wetnessDebug = false;
        // Task 0039: lit draws get snow and ash cover, and the debug view of it (needs pbrLook above zero).
        bool coverEnabled = false;
        bool coverDebug = false;
        // Task 0045: parallax occlusion mapping for draws with a height file: 0 off, 1 low, 2 medium, 3 high.
        unsigned int parallaxQuality = 0;

        vsg::ref_ptr<vsg::BindGraphicsPipeline> create(const Options& key) const;
        vsg::ref_ptr<vsg::ShaderStage> create(const VertexStageOptions& key) const;
        vsg::ref_ptr<vsg::ShaderStage> create(const FragmentStageOptions& key) const;
        vsg::ref_ptr<vsg::ShaderStage> createShaderStage(const ShaderOptions& key, const std::string& suffix) const;
        inline vsg::ref_ptr<vsg::BindGraphicsPipeline> getOrCreate(const Options& key) const
        {
            return mPipelineCache.getOrCreate(key, *this);
        }
    };
}

#endif
