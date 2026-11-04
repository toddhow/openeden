// Helpers shared by the tests of task 0074e, the fresh review of task 0074d (findings 1, 3, 5, 7 and 9): recording
// controllers, models, skeletons and custom-bone files, a reader that gives them or throws, compile contexts (one
// that counts what the actor's real compile() compiles), the read options of an MWAnim::Context, and an inventory
// light item. Only what exists before task 0074e, so that each test file compiles with the files before it alone.

#ifndef OPENMW_TESTS_MWRENDER_PARTSTESTSUPPORT_H
#define OPENMW_TESTS_MWRENDER_PARTSTESTSUPPORT_H

#include "apps/openmw/mwclass/light.hpp"
#include "apps/openmw/mwworld/livecellref.hpp"
#include "apps/openmw/mwworld/ptr.hpp"

#include <components/animation/bones.hpp>
#include <components/animation/contents.hpp>
#include <components/animation/controller.hpp>
#include <components/animation/meta.hpp>
#include <components/animation/transform.hpp>
#include <components/animation/update.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/cellref.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/mwanimation/bones.hpp>
#include <components/mwanimation/context.hpp>
#include <components/mwanimation/object.hpp>
#include <components/mwanimation/play.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/name.hpp>

#include <vsg/app/CompileManager.h>
#include <vsg/app/Viewer.h>
#include <vsg/core/Object.h>
#include <vsg/core/Visitor.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/io/Options.h>
#include <vsg/io/ReaderWriter.h>
#include <vsg/lighting/Light.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Node.h>
#include <vsg/state/ResourceHints.h>

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace PartsTestSupport
{
    inline constexpr float frameTime = 1.f / 60.f;

    // A controller that records every time it is run with. Its cloneIfRequired() is the default (null), so
    // MWAnim::decorate shares it: the recorder a test holds is the one that runs.
    struct Recorder : public Anim::Controller
    {
        Recorder(bool autoPlay, int phaseGroup)
        {
            hints.autoPlay = autoPlay;
            hints.phaseGroup = phaseGroup;
        }
        void run(vsg::Object& /*target*/, float time) const override { mTimes.push_back(time); }
        mutable std::vector<float> mTimes;
    };

    // An auto-play controller (phase group 0 runs from 0, a group above 0 from a random time).
    inline vsg::ref_ptr<Recorder> autoPlayRecorder(int phaseGroup)
    {
        return vsg::ref_ptr{ new Recorder(true, phaseGroup) };
    }

    // A controller that is not auto-play: the static list (run with time 0), the face or the weapon list.
    inline vsg::ref_ptr<Recorder> listRecorder()
    {
        return vsg::ref_ptr{ new Recorder(false, 0) };
    }

    // A model as the NIF reader gives one: its root says it has controllers (Anim::Meta), and each controller sits on
    // an Anim::Transform child of its own (Anim::cloneIfRequired collects controllers on transforms and switches).
    inline vsg::ref_ptr<vsg::Node> makeModel(const std::vector<vsg::ref_ptr<Recorder>>& controllers)
    {
        auto root = vsg::Group::create();
        Anim::Contents contents;
        contents.add(Anim::Contents::Controllers);
        vsg::ref_ptr<Anim::Meta> meta(new Anim::Meta(contents));
        meta->attachTo(*root);
        for (const vsg::ref_ptr<Recorder>& controller : controllers)
        {
            vsg::ref_ptr<Anim::Transform> child(new Anim::Transform);
            controller->attachTo(*child);
            root->addChild(child);
        }
        return root;
    }

    // A model with no controller (a torch's mesh).
    inline vsg::ref_ptr<vsg::Node> makePlainModel()
    {
        auto root = vsg::Group::create();
        root->addChild(vsg::ref_ptr<Anim::Transform>(new Anim::Transform));
        return root;
    }

    inline vsg::ref_ptr<Anim::Transform> makeBone(const std::string& name)
    {
        vsg::ref_ptr<Anim::Transform> bone(new Anim::Transform);
        vsgUtil::setName(*bone, name);
        return bone;
    }

    // A skeleton "Bip01" with the given bones under it, under a plain group. Name a bone "Bip01 Head" for the head
    // (MWAnim::Actor::assignBones looks up "bip01 head").
    inline vsg::ref_ptr<vsg::Group> makeSkeleton(const std::vector<std::string>& boneNames)
    {
        auto scene = vsg::Group::create();
        vsg::ref_ptr<Anim::Transform> root = makeBone("Bip01");
        for (const std::string& name : boneNames)
            root->addChild(makeBone(name));
        scene->addChild(root);
        return scene;
    }

    // A custom-bone file of a modded skeleton: "Bip01" with the given bones under it, each marked as a custom bone
    // (Anim::sCustomBoneKey), so Anim::Bones::addCustomBones copies each under the actor's own "Bip01".
    inline vsg::ref_ptr<vsg::Group> makeCustomBones(const std::vector<std::string>& boneNames)
    {
        auto scene = vsg::Group::create();
        vsg::ref_ptr<Anim::Transform> root = makeBone("Bip01");
        for (const std::string& name : boneNames)
        {
            vsg::ref_ptr<Anim::Transform> bone = makeBone(name);
            bone->setValue(Anim::sCustomBoneKey, true);
            root->addChild(bone);
        }
        scene->addChild(root);
        return scene;
    }

    // What the reader throws for a name on its broken list: a type of its own, so a test catches exactly it.
    struct ReadError : public std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    // A thrown value of a type that is not a std::exception.
    struct NotAnException
    {
        int mCode = 0;
    };

    // What a test's own callable (a part update, a quiver change, node work) throws.
    struct UpdateError : public std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    // Gives the object registered under the name the requested path ends with (names in lower case), throws for a
    // name on a broken list, and gives nothing otherwise: an animation file it does not know is then "no animation"
    // (MWAnim::Context::readAnimations skips it), while vsgUtil::readNode throws its own std::runtime_error for a
    // model, so register every model a test expects to be read.
    class PartsReader : public vsg::ReaderWriter
    {
    public:
        using vsg::ReaderWriter::read;

        void add(const std::string& name, vsg::ref_ptr<vsg::Object> object) { mObjects[name] = std::move(object); }

        // A read of the name throws ReadError.
        void breakWithReadError(const std::string& name) { mBroken[name] = Break::ReadError; }
        // A read of the name throws NotAnException.
        void breakWithNotAnException(const std::string& name) { mBroken[name] = Break::NotAnException; }

        // Called with the requested name (lower case) at the start of every read.
        std::function<void(const std::string&)> onRead;

        vsg::ref_ptr<vsg::Object> read(
            const vsg::Path& filename, vsg::ref_ptr<const vsg::Options> /*options*/) const override
        {
            const std::string requested = Misc::StringUtils::lowerCase(filename.string());
            if (onRead)
                onRead(requested);
            for (const auto& [name, kind] : mBroken)
            {
                if (!requested.ends_with(name))
                    continue;
                if (kind == Break::ReadError)
                    throw ReadError("cannot read " + requested);
                throw NotAnException{ 7 };
            }
            for (const auto& [name, object] : mObjects)
            {
                if (requested.ends_with(name))
                    return object;
            }
            return {};
        }

    private:
        enum class Break
        {
            ReadError,
            NotAnException
        };
        std::map<std::string, Break> mBroken;
        std::map<std::string, vsg::ref_ptr<vsg::Object>> mObjects;
    };

    // MWAnim::Object's destructor detaches its node through the context's compile context; here that does nothing.
    // It has no compile manager: for tests in which nothing compiles.
    inline vsg::ref_ptr<vsgUtil::CompileContext> quietCompileContext()
    {
        vsg::ref_ptr<vsgUtil::CompileContext> compile(new vsgUtil::CompileContext);
        compile->onDetached = [](vsg::ref_ptr<vsg::Object>) {};
        return compile;
    }

    // A compile context that counts compiles: its compile manager comes from a viewer with no window, whose compile
    // traversal has no Vulkan context, so vsg::CompileManager::compile returns VK_SUCCESS and
    // vsgUtil::CompileContext::compile calls onCompiled. This is how a test sees that an actor's real compile() ran.
    // Owns the viewer, so declare it before the MWAnim::Context that holds its context. Not copyable or movable: the
    // callback holds its address.
    class CountingCompile
    {
    public:
        CountingCompile()
        {
            mContext->compileManager = vsg::CompileManager::create(*mViewer, vsg::ResourceHints::create());
            mContext->onCompiled = [this](const vsg::CompileResult& /*result*/) {
                ++mCount;
                if (onCompile)
                    onCompile();
            };
            mContext->onDetached = [](vsg::ref_ptr<vsg::Object>) {};
        }
        CountingCompile(const CountingCompile&) = delete;
        CountingCompile& operator=(const CountingCompile&) = delete;

        const vsg::ref_ptr<vsgUtil::CompileContext>& context() const { return mContext; }
        int count() const { return mCount; }

        // Called after each compile is counted (a test that records the order of calls).
        std::function<void()> onCompile;

    private:
        vsg::ref_ptr<vsg::Viewer> mViewer = vsg::Viewer::create();
        vsg::ref_ptr<vsgUtil::CompileContext> mContext{ new vsgUtil::CompileContext };
        int mCount = 0;
    };

    // Every read option of the context reads through `reader` (one vsg::Options for all of them, and one per part
    // type for the part bones), and `compile` is its compile context.
    inline void setUpContext(MWAnim::Context& context, vsg::ref_ptr<vsgUtil::CompileContext> compile,
        const vsg::ref_ptr<PartsReader>& reader)
    {
        context.compileContext = std::move(compile);
        auto options = vsg::Options::create();
        options->readerWriters.push_back(reader);
        const vsg::ref_ptr<const vsg::Options> shared = options;
        context.nodeOptions = shared;
        context.effectOptions = shared;
        context.actorOptions = shared;
        context.textureOptions = shared;
        context.animOptions = shared;
        context.partBoneOptions.assign(static_cast<std::size_t>(ESM::PRT_Count), shared);
    }

    // An NPC's Play: a detached copy of the skeleton's bones (MWAnim::attachBonesAndNode attaches the bones a part
    // needs under the actor's node). The const reference chooses Anim::Bones' copying constructor.
    inline std::unique_ptr<MWAnim::Play> npcPlay(const vsg::Group& skeleton)
    {
        return std::make_unique<MWAnim::Play>(MWAnim::Bones(skeleton));
    }

    // A creature's skeleton is its own model, attached under its node, and its bones are attached (MWAnim::attachNode
    // hangs a part off the bone itself).
    inline void addCreatureSkeleton(MWAnim::Object& creature, const vsg::ref_ptr<vsg::Group>& skeleton)
    {
        creature.nodeToAddChildrenTo()->addChild(skeleton);
        vsg::Group& attached = *skeleton;
        creature.animation = std::make_unique<MWAnim::Play>(MWAnim::Bones(attached));
    }

    // How many times `controller` is in a list.
    inline std::size_t countIn(const Anim::Update& list, const Anim::Controller* controller)
    {
        std::size_t count = 0;
        for (const auto& entry : list.controllers)
        {
            if (entry.first == controller)
                ++count;
        }
        return count;
    }

    // Every light in a subgraph, also under switches whose children are off (Wielding's switches start off).
    class CollectLights : public vsg::Visitor
    {
    public:
        CollectLights() { overrideMask = vsg::MASK_ALL; }

        using vsg::Visitor::apply;

        void apply(vsg::Node& node) override { node.traverse(*this); }

        void apply(vsg::Light& light) override
        {
            mLights.push_back(&light);
            light.traverse(*this);
        }

        std::vector<vsg::Light*> mLights;
    };

    inline std::vector<vsg::Light*> lightsUnder(vsg::Node& node)
    {
        CollectLights visitor;
        node.accept(visitor);
        return visitor.mLights;
    }

    // Every node in a subgraph, also under switches whose children are off.
    class CountNodes : public vsg::Visitor
    {
    public:
        CountNodes() { overrideMask = vsg::MASK_ALL; }

        using vsg::Visitor::apply;

        void apply(vsg::Node& node) override
        {
            ++mCount;
            node.traverse(*this);
        }

        std::size_t mCount = 0;
    };

    inline std::size_t countNodes(vsg::Node& node)
    {
        CountNodes visitor;
        node.accept(visitor);
        return visitor.mCount;
    }

    // The colours a light takes over `frames` frames of the actor's update.
    template <class Actor>
    std::set<std::tuple<float, float, float>> lightColours(Actor& actor, const vsg::Light& light, int frames)
    {
        std::set<std::tuple<float, float, float>> colours;
        for (int i = 0; i < frames; ++i)
        {
            actor.update(frameTime);
            colours.emplace(light.color.r, light.color.g, light.color.b);
        }
        return colours;
    }

    inline ESM::Light lightRecord(int flags, const std::string& model)
    {
        ESM::Light light;
        light.blank();
        light.mId = ESM::RefId::stringRefId("test_parts_light");
        light.mModel = model;
        light.mData.mFlags = flags;
        light.mData.mRadius = 128;
        light.mData.mColor = 0x0040a0ff;
        return light;
    }

    inline ESM::CellRef lightCellRef(const ESM::Light& record)
    {
        // LiveCellRef's constructor looks the class up (MWWorld::Class::get).
        MWClass::Light::registerSelf();
        ESM::CellRef cellRef;
        cellRef.blank();
        cellRef.mRefID = record.mId;
        return cellRef;
    }

    // A light item as an inventory holds one (MWClass::Light::getModel gives `model`; no enchantment, so no glow
    // colour). With ESM::Light::Carry it is a carried light. Not copyable: the reference points at the record.
    struct LightItem
    {
        LightItem(int flags, const std::string& model)
            : mRecord(lightRecord(flags, model))
            , mCellRef(lightCellRef(mRecord))
            , mRef(mCellRef, &mRecord)
        {
        }
        LightItem(const LightItem&) = delete;
        LightItem& operator=(const LightItem&) = delete;

        ESM::Light mRecord;
        ESM::CellRef mCellRef;
        MWWorld::LiveCellRef<ESM::Light> mRef;

        MWWorld::ConstPtr ptr() const { return MWWorld::ConstPtr(&mRef); }
    };

    // Task 0074f: a bow model as the decorated weapon models have one: its root says it has placeholders
    // (Anim::Contents::Placeholders), and an Anim::Transform named "ArrowBone" is the ammunition placeholder that
    // MWAnim::clonePlaceholdersIfRequired finds (Placeholders::attachAmmo). Every decoration clones that placeholder,
    // so two actors holding the same model hold different placeholders.
    inline vsg::ref_ptr<vsg::Node> makeBowModel()
    {
        auto root = vsg::Group::create();
        Anim::Contents contents;
        contents.add(Anim::Contents::Controllers);
        contents.add(Anim::Contents::Placeholders);
        vsg::ref_ptr<Anim::Meta> meta(new Anim::Meta(contents));
        meta->attachTo(*root);
        vsg::ref_ptr<Anim::Transform> arrowBone(new Anim::Transform);
        vsgUtil::setName(*arrowBone, "ArrowBone");
        root->addChild(arrowBone);
        return root;
    }
}

#endif
