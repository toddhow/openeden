#ifndef OPENEDEN_VSGADAPTERS_DAE_DAE_H
#define OPENEDEN_VSGADAPTERS_DAE_DAE_H

#include <vsg/io/ReaderWriter.h>

namespace Pipeline
{
    class Builder;
}

namespace vsgAdapters
{
    /*
     * openeden-vsg: reads a COLLADA (.dae) model into the same scene graph structures the nif reader builds: named
     * Anim::Transform nodes, Anim::Skin for skinned meshes and vsgopenmw pipelines. Replaces OpenMW 0.51's OSG COLLADA
     * path (Resource::SceneManager::loadNode). Depends on assimp, animation, pipeline.
     */
    class dae : public vsg::ReaderWriter
    {
        const Pipeline::Builder& mBuilder;
        vsg::ref_ptr<const vsg::Options> mImageOptions;
        const int mDepthSortedBin;

    public:
        dae(const Pipeline::Builder& builder, vsg::ref_ptr<const vsg::Options> imageOptions, int depthSortedBin)
            : mBuilder(builder)
            , mImageOptions(imageOptions)
            , mDepthSortedBin(depthSortedBin)
        {
        }
        vsg::ref_ptr<vsg::Object> read(std::istream&, vsg::ref_ptr<const vsg::Options> = {}) const override;

        bool getFeatures(Features& features) const override;
    };
}

#endif
