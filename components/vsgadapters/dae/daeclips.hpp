#ifndef OPENEDEN_VSGADAPTERS_DAE_DAECLIPS_H
#define OPENEDEN_VSGADAPTERS_DAE_DAECLIPS_H

#include <vsg/io/ReaderWriter.h>

namespace VFS
{
    class Manager;
}

namespace vsgAdapters
{
    /*
     * openeden (task 0124, stage C3): reads the animations of a COLLADA (.dae) model as an Anim::AnimSet, with text keys
     * from the .txt file of the same name. It replaces vsgAdapters::daeAnimation, which handed the engine a bare
     * Anim::ControllerMap: the set is what the animation system shares between actors, and the group table and the
     * clip CreativeMotion plays are derived from it once.
     *
     * The set still carries the keyframe controllers the model's bones are written from until stage C5; they are an
     * implementation detail of the set, not what this reader is registered for. Depends on assimp, animation, vfs.
     */
    class daeClips : public vsg::ReaderWriter
    {
        const VFS::Manager& mVfs;

    public:
        explicit daeClips(const VFS::Manager& vfs)
            : mVfs(vfs)
        {
        }
        /// The Anim::AnimSet of the model (a null object for a model with no animations or another extension).
        vsg::ref_ptr<vsg::Object> read(std::istream&, vsg::ref_ptr<const vsg::Options> = {}) const override;
        bool getFeatures(Features& features) const override;
    };
}

#endif
