#ifndef OPENEDEN_VSGADAPTERS_DAE_DAEANIM_H
#define OPENEDEN_VSGADAPTERS_DAE_DAEANIM_H

#include <vsg/io/ReaderWriter.h>

namespace VFS
{
    class Manager;
}

namespace vsgAdapters
{
    /*
     * openeden-vsg: reads the animations of a COLLADA (.dae) model into an Anim::ControllerMap, with text keys from the
     * .txt file of the same name. Replaces OpenMW 0.51's OSG path (Resource::RetrieveAnimationsVisitor).
     * Depends on assimp, animation, vfs.
     */
    class daeAnimation : public vsg::ReaderWriter
    {
        const VFS::Manager& mVfs;

    public:
        explicit daeAnimation(const VFS::Manager& vfs)
            : mVfs(vfs)
        {
        }
        vsg::ref_ptr<vsg::Object> read(std::istream&, vsg::ref_ptr<const vsg::Options> = {}) const override;
        bool getFeatures(Features& features) const override;
    };
}

#endif
