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
