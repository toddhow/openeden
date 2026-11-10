#ifndef VSGOPENMW_PIPELINE_GRAPHICS_H
#define VSGOPENMW_PIPELINE_GRAPHICS_H

#include <string>
#include <vector>

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
        // Task 0072: `[Rendering] slang shaders`, from mShaderOptions when the Graphics is made.
        const bool mPreferSlang;
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
        // Task 0072: `[Rendering] slang shaders`: when true, a stage whose define set has a Slang twin may draw with it
        // (Graphics::create takes both stages' twins or neither). Review of the stage f and g rounds, finding 7: taken
        // from the shader options (vsgUtil::prefersSlang) when the Graphics is made, so every stage it makes knows it;
        // review of the member-name and fix rounds, item 4: read-only, so that it cannot drift from them.
        bool prefersSlang() const { return mPreferSlang; }
        // OpenEden (task 0047): shader paths that exist only as Slang (no GLSL pair beside them). For these the
        // precompiled <path>/main.vert.spv and main.frag.spv are always the shader, whatever `prefersSlang()` says, and
        // a missing file is an error (ask `hasSlangShader` first); set before the first pipeline using it is made.
        std::vector<std::string> slangOnlyPaths;
        // True when <path>/main.vert.spv and main.frag.spv can be read (there was a slangc at build time).
        bool hasSlangShader(const std::string& path) const;
        // openeden (task 0026, F3): how many colour outputs the main pass has beyond the scene's own; 0 (the default)
        // leaves every pipeline as it was. Set by MWRender::RenderManager before the first pipeline is created.
        unsigned int gbufferOutputs = 0;
        // Task 0075: with TAA on, the slope factor of the depth bias that blended, depth-tested pipelines get (see
        // Render::Taa::overlayDepthBiasSlope); 0 leaves every pipeline as it was. Set before the first pipeline is
        // made.
        float overlayDepthBiasSlope = 0.f;
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
        // Task 0077: lit draws sample the point lights' shadow maps (needs pbrLook above zero).
        bool pointShadowEnabled = false;
        // Task 0045: parallax occlusion mapping for draws with a height file: 0 off, 1 low, 2 medium, 3 high.
        unsigned int parallaxQuality = 0;

        vsg::ref_ptr<vsg::BindGraphicsPipeline> create(const Options& key) const;
        vsg::ref_ptr<vsg::ShaderStage> create(const VertexStageOptions& key) const;
        vsg::ref_ptr<vsg::ShaderStage> create(const FragmentStageOptions& key) const;
        vsg::ref_ptr<vsg::ShaderStage> createShaderStage(const ShaderOptions& key, const std::string& suffix) const;
        /// The name of the bool value create() sets on a pipeline that draws with GLSL only because the Slang version
        /// of one of its stages was still being compiled (vsgUtil::findSlangPermutation, `pending`).
        static constexpr const char* slangPendingKey = "openeden.slangPending";

        inline vsg::ref_ptr<vsg::BindGraphicsPipeline> getOrCreate(const Options& key) const
        {
            auto pipeline = mPipelineCache.getOrCreate(key, *this);
            bool pending = false;
            if (pipeline && pipeline->getValue(slangPendingKey, pending) && pending)
            {
                // Not kept: objects already built keep this pipeline (nothing is swapped), the next object that
                // needs the same key builds it again, and gets the Slang twin once the worker has finished.
                mPipelineCache.removeObjects(
                    [&pipeline](const vsg::ref_ptr<vsg::BindGraphicsPipeline>& cached) { return cached == pipeline; });
            }
            return pipeline;
        }
    };
}

#endif
