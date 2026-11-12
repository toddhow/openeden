#include "context.hpp"

#include <vsg/io/read.h>
#include <vsg/utils/SharedObjects.h>

#include <components/animation/animset.hpp>
#include <components/animation/controllermap.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/readnode.hpp>

namespace MWAnim
{
    Context::~Context() {}

    const std::string& Context::pickSkeleton(bool firstPerson, bool isFemale, bool isBeast, bool isWerewolf) const
    {
        auto& f = firstPerson ? firstPersonFiles : files;
        if (isWerewolf)
            return f.wolfskin;
        else if (isBeast)
            return f.baseanimkna;
        else if (isFemale)
            return f.baseanimfemale;
        else
            return f.baseanim;
    }

    vsg::ref_ptr<vsg::Node> Context::readEffect(const std::string& fname) const
    {
        return vsgUtil::readNode(fname, effectOptions);
    }

    vsg::ref_ptr<vsg::Node> Context::readNode(const std::string& fname) const
    {
        return vsgUtil::readNode(fname, nodeOptions);
    }

    vsg::ref_ptr<vsg::Node> Context::readActor(const std::string& fname) const
    {
        return vsgUtil::readNode(fname, actorOptions);
    }

    vsg::ref_ptr<Anim::ControllerMap> Context::readAnimation(const std::string& file) const
    {
        const vsg::ref_ptr<vsg::Object> object = vsg::read(file, animOptions);
        // A reader may hand over a bare ControllerMap (a Morrowind .kf) or an AnimSet (a COLLADA model, task 0124
        // stage C3). The set is what the options' shared cache keeps, so every actor that plays the file gets the same
        // set from AnimSet::forSource and the clip made from it once. Its controllers are immutable once read.
        if (const auto* set = dynamic_cast<const Anim::AnimSet*>(object.get()))
            return vsg::ref_ptr<Anim::ControllerMap>(const_cast<Anim::ControllerMap*>(set->controllers.get()));
        // dynamic_cast, as vsg::read_cast does: Object::cast only knows types made with vsg::Inherit.
        return vsg::ref_ptr<Anim::ControllerMap>(dynamic_cast<Anim::ControllerMap*>(object.get()));
    }

    std::vector<vsg::ref_ptr<Anim::ControllerMap>> Context::readAnimations(
        const std::vector<std::string>& fileNames) const
    {
        std::vector<vsg::ref_ptr<Anim::ControllerMap>> ret;
        for (auto& file : fileNames)
        {
            if (auto anim = readAnimation(file))
                ret.emplace_back(anim);
        }
        return ret;
    }

    void Context::prune() const
    {
        auto pruneSharedObjects = [](const vsg::ref_ptr<const vsg::Options>& options) {
            if (options->sharedObjects)
                options->sharedObjects->prune();
        };

        for (auto& options : partBoneOptions)
            pruneSharedObjects(options);
        pruneSharedObjects(nodeOptions);
        pruneSharedObjects(effectOptions);
        pruneSharedObjects(actorOptions);
        pruneSharedObjects(textureOptions);
        // pruneSharedObjects(animOptions);
    }
}
