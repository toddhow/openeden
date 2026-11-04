// Tests for task 0074d, the review of task 0074b (findings 2 and 4): how MWRender::Npc and MWRender::WieldingCreature
// use their part controllers and auto-play clocks, through the functions the game runs (insertBoundedPart,
// removeIndividualPart, runPartUpdate, removeAllParts, attachHolstered, removeHolstered, attachMergedCopy, linkPart,
// removeParts and update), not through the helpers PartControllers and Wielding::clearPartControllers alone. Written
// from the task's test specification before the implementation and run red against stubs; each test names the finding
// or the mutation of the production code (a) to (e) it catches.

#include "apps/openmw/mwclass/light.hpp"
#include "apps/openmw/mwrender/itemlight.hpp"
#include "apps/openmw/mwrender/mergedbody.hpp"
#include "apps/openmw/mwrender/npc.hpp"
#include "apps/openmw/mwrender/wieldingcreature.hpp"
#include "apps/openmw/mwworld/inventorystore.hpp"
#include "apps/openmw/mwworld/livecellref.hpp"
#include "apps/openmw/mwworld/ptr.hpp"

#include <components/animation/autoplay.hpp>
#include <components/animation/contents.hpp>
#include <components/animation/controller.hpp>
#include <components/animation/meta.hpp>
#include <components/animation/transform.hpp>
#include <components/animation/update.hpp>
#include <components/body/mergedruntime.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/cellref.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/mwanimation/attach.hpp>
#include <components/mwanimation/bones.hpp>
#include <components/mwanimation/context.hpp>
#include <components/mwanimation/decorate.hpp>
#include <components/mwanimation/face.hpp>
#include <components/mwanimation/partbones.hpp>
#include <components/mwanimation/play.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/name.hpp>

#include <vsg/core/Object.h>
#include <vsg/core/Visitor.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/io/Options.h>
#include <vsg/io/ReaderWriter.h>
#include <vsg/lighting/Light.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Node.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
    constexpr float frameTime = 1.f / 60.f;

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
    vsg::ref_ptr<Recorder> autoPlayRecorder(int phaseGroup)
    {
        return vsg::ref_ptr{ new Recorder(true, phaseGroup) };
    }

    // A controller that is not auto-play: the static list (run with time 0), the face or the weapon list.
    vsg::ref_ptr<Recorder> listRecorder()
    {
        return vsg::ref_ptr{ new Recorder(false, 0) };
    }

    // A model as the NIF reader gives one: its root says it has controllers (Anim::Meta), and each controller sits on
    // an Anim::Transform child of its own (Anim::cloneIfRequired collects controllers on transforms and switches).
    vsg::ref_ptr<vsg::Node> makeModel(const std::vector<vsg::ref_ptr<Recorder>>& controllers)
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
    vsg::ref_ptr<vsg::Node> makePlainModel()
    {
        auto root = vsg::Group::create();
        root->addChild(vsg::ref_ptr<Anim::Transform>(new Anim::Transform));
        return root;
    }

    // Gives the model registered under the name the requested path ends with (names in lower case), or nothing.
    class ModelReader : public vsg::ReaderWriter
    {
    public:
        using vsg::ReaderWriter::read;

        void add(const std::string& name, vsg::ref_ptr<vsg::Node> model) { mModels[name] = std::move(model); }

        vsg::ref_ptr<vsg::Object> read(
            const vsg::Path& filename, vsg::ref_ptr<const vsg::Options> /*options*/) const override
        {
            const std::string requested = Misc::StringUtils::lowerCase(filename.string());
            for (const auto& [name, model] : mModels)
            {
                if (requested.ends_with(name))
                    return model;
            }
            return {};
        }

    private:
        std::map<std::string, vsg::ref_ptr<vsg::Node>> mModels;
    };

    // MWAnim::Object's destructor detaches its node through the context's compile context; here that does nothing.
    vsg::ref_ptr<vsgUtil::CompileContext> quietCompileContext()
    {
        vsg::ref_ptr<vsgUtil::CompileContext> compile(new vsgUtil::CompileContext);
        compile->onDetached = [](vsg::ref_ptr<vsg::Object>) {};
        return compile;
    }

    vsg::ref_ptr<Anim::Transform> makeBone(const std::string& name)
    {
        vsg::ref_ptr<Anim::Transform> bone(new Anim::Transform);
        vsgUtil::setName(*bone, name);
        return bone;
    }

    // A skeleton "Bip01" with the given bones under it, under a plain group.
    vsg::ref_ptr<vsg::Group> makeSkeleton(const std::vector<std::string>& boneNames)
    {
        auto scene = vsg::Group::create();
        vsg::ref_ptr<Anim::Transform> root = makeBone("Bip01");
        for (const std::string& name : boneNames)
            root->addChild(makeBone(name));
        scene->addChild(root);
        return scene;
    }

    ESM::Light lightRecord(int flags)
    {
        ESM::Light light;
        light.blank();
        light.mId = ESM::RefId::stringRefId("test_light");
        light.mData.mFlags = flags;
        light.mData.mRadius = 128;
        light.mData.mColor = 0x0040a0ff;
        return light;
    }

    // How many times `controller` is in a list.
    std::size_t countIn(const Anim::Update& list, const Anim::Controller* controller)
    {
        std::size_t count = 0;
        for (const auto& entry : list.controllers)
        {
            if (entry.first == controller)
                ++count;
        }
        return count;
    }

    // How many times `controller` is in any group of an auto-play clock.
    std::size_t countIn(const Anim::AutoPlay& clock, const Anim::Controller* controller)
    {
        std::size_t count = 0;
        for (const Anim::AutoPlay::Group& group : clock.groups)
            count += countIn(static_cast<const Anim::Update&>(group), controller);
        return count;
    }

    // Whether any controller of an auto-play clock runs on `target`.
    bool runsOn(const Anim::AutoPlay& clock, const vsg::Object* target)
    {
        for (const Anim::AutoPlay::Group& group : clock.groups)
        {
            for (const auto& entry : group.controllers)
            {
                if (entry.second == target)
                    return true;
            }
        }
        return false;
    }

    // The first light an auto-play clock runs a controller on.
    vsg::Light* findLight(const Anim::AutoPlay& clock)
    {
        for (const Anim::AutoPlay::Group& group : clock.groups)
        {
            for (const auto& entry : group.controllers)
            {
                if (auto light = dynamic_cast<vsg::Light*>(entry.second))
                    return light;
            }
        }
        return nullptr;
    }

    // The group of an auto-play clock that runs a controller on `light`.
    const Anim::AutoPlay::Group* findGroupOf(const Anim::AutoPlay& clock, const vsg::Light* light)
    {
        for (const Anim::AutoPlay::Group& group : clock.groups)
        {
            for (const auto& entry : group.controllers)
            {
                if (entry.second == light)
                    return &group;
            }
        }
        return nullptr;
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

    std::vector<vsg::Light*> lightsUnder(vsg::Node& node)
    {
        CollectLights visitor;
        node.accept(visitor);
        return visitor.mLights;
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

    // Every run's time is the one before plus a frame: an auto-play clock that kept running.
    ::testing::AssertionResult continuous(const std::vector<float>& times)
    {
        for (std::size_t i = 1; i < times.size(); ++i)
        {
            if (times[i] != times[i - 1] + frameTime)
                return ::testing::AssertionFailure()
                    << "run " << i << " at " << times[i] << ", the run before at " << times[i - 1];
        }
        return ::testing::AssertionSuccess();
    }

    // An inventory light item (a light record without the Carry flag), as ContainerStore holds one.
    ESM::CellRef lightCellRef(const ESM::Light& record)
    {
        // LiveCellRef's constructor looks the class up (MWWorld::Class::get).
        MWClass::Light::registerSelf();
        ESM::CellRef cellRef;
        cellRef.blank();
        cellRef.mRefID = record.mId;
        return cellRef;
    }

    struct InventoryLight
    {
        ESM::Light mRecord = lightRecord(ESM::Light::Flicker);
        ESM::CellRef mCellRef = lightCellRef(mRecord);
        MWWorld::LiveCellRef<ESM::Light> mRef{ mCellRef, &mRecord };

        MWWorld::ConstPtr ptr() const { return MWWorld::ConstPtr(&mRef); }
    };

    // An Npc built without a record (Npc's test constructor), with the protected part functions in reach.
    struct TestNpc : public MWRender::Npc
    {
        explicit TestNpc(const MWAnim::Context& mwContext)
            : MWRender::Npc(mwContext, TestSetup{})
        {
        }

        using MWRender::Npc::attachHolstered;
        using MWRender::Npc::attachMergedCopy;
        using MWRender::Npc::clearControllers;
        using MWRender::Npc::insertBoundedPart;
        using MWRender::Npc::MergedLink;
        using MWRender::Npc::mFace;
        using MWRender::Npc::mHolsteredShield;
        using MWRender::Npc::mHolsteredShieldClock;
        using MWRender::Npc::mMergedBody;
        using MWRender::Npc::mMergedControllers;
        using MWRender::Npc::mMergedDrawn;
        using MWRender::Npc::mMergedHidden;
        using MWRender::Npc::mMergedNode;
        using MWRender::Npc::mMergedTemplate;
        using MWRender::Npc::mPartControllers;
        using MWRender::Npc::mScabbard;
        using MWRender::Npc::mScabbardClock;
        using MWRender::Npc::mStaticControllers;
        using MWRender::Npc::mWeaponControllers;
        using MWRender::Npc::removeAllParts;
        using MWRender::Npc::removeHolstered;
        using MWRender::Npc::removeIndividualPart;
        using MWRender::Npc::runPartUpdate;
    };

    struct NpcPartsTest : public ::testing::Test
    {
        vsg::ref_ptr<ModelReader> mReader{ new ModelReader };
        // The part bones of MWAnim::sPartBones the tests use, and the sheathing bones of a modded skeleton.
        vsg::ref_ptr<vsg::Group> mSkeleton = makeSkeleton(
            { "Head", "Chest", "Groin", "Weapon Bone", "Shield Bone", "Bip01 Weapon", "Bip01 AttachShield" });
        MWAnim::Context mContext;
        // Declared after the context: it goes first.
        std::unique_ptr<TestNpc> mNpc;

        void SetUp() override
        {
            mContext.compileContext = quietCompileContext();
            auto options = vsg::Options::create();
            options->readerWriters.push_back(mReader);
            const vsg::ref_ptr<const vsg::Options> partOptions = options;
            mContext.partBoneOptions.assign(static_cast<std::size_t>(ESM::PRT_Count), partOptions);
            mNpc = std::make_unique<TestNpc>(mContext);
            // An NPC's skeleton is a detached copy of its bones (MWAnim::attachBonesAndNode attaches the bones a part
            // needs under the actor's node).
            const vsg::Group& skeleton = *mSkeleton;
            mNpc->animation = std::make_unique<MWAnim::Play>(MWAnim::Bones(skeleton));
            ASSERT_NE(mNpc->mFace, nullptr);
        }

        std::string addModel(const std::string& name, const std::vector<vsg::ref_ptr<Recorder>>& controllers)
        {
            mReader->add(name, makeModel(controllers));
            return name;
        }

        void insertPart(ESM::PartReferenceType type, const std::string& model)
        {
            mNpc->insertBoundedPart(type, model, MWAnim::sPartBones.at(type), std::nullopt, nullptr);
        }

        void runFrames(int frames)
        {
            for (int i = 0; i < frames; ++i)
                mNpc->update(frameTime);
        }
    };

    // N1, mutation (a): an equipment change never empties the object's own auto-play groups, which hold the inventory
    // lights nothing would add again.
    TEST_F(NpcPartsTest, an_inventory_light_keeps_flickering_through_clear_controllers_and_a_part_update)
    {
        TestNpc& npc = *mNpc;
        InventoryLight item;
        MWRender::addItemLight(npc, item.ptr(), item.mRecord);
        vsg::Light* light = findLight(npc.autoPlay);
        ASSERT_NE(light, nullptr);
        EXPECT_GT(lightColours(npc, *light, 30).size(), 10u);
        const Anim::AutoPlay::Group* group = findGroupOf(npc.autoPlay, light);
        ASSERT_NE(group, nullptr);
        const float timerBefore = group->timer;

        npc.clearControllers();

        ASSERT_EQ(findLight(npc.autoPlay), light);
        group = findGroupOf(npc.autoPlay, light);
        ASSERT_NE(group, nullptr);
        EXPECT_EQ(group->timer, timerBefore);
        EXPECT_GT(lightColours(npc, *light, 30).size(), 10u);
        group = findGroupOf(npc.autoPlay, light);
        ASSERT_NE(group, nullptr);
        const float timerBeforeUpdate = group->timer;

        npc.runPartUpdate([] {});

        ASSERT_EQ(findLight(npc.autoPlay), light);
        group = findGroupOf(npc.autoPlay, light);
        ASSERT_NE(group, nullptr);
        EXPECT_EQ(group->timer, timerBeforeUpdate);
        EXPECT_GT(lightColours(npc, *light, 30).size(), 10u);
    }

    // N2, mutation (b): Npc::update runs the parts' clocks, and an update that keeps a part keeps its clock running.
    TEST_F(NpcPartsTest, a_kept_part_runs_its_auto_play_clock_on_every_update)
    {
        TestNpc& npc = *mNpc;
        vsg::ref_ptr<Recorder> steady = autoPlayRecorder(0);
        vsg::ref_ptr<Recorder> random = autoPlayRecorder(2);
        const std::string hair = addModel("meshes/test/hair.nif", { steady, random });

        npc.runPartUpdate([&] { insertPart(ESM::PRT_Hair, hair); });
        runFrames(10);

        ASSERT_EQ(steady->mTimes.size(), 10u);
        ASSERT_EQ(random->mTimes.size(), 10u);
        // A part inserted gets a fresh clock: group 0 starts at 0.
        EXPECT_EQ(steady->mTimes.front(), frameTime);
        EXPECT_TRUE(continuous(steady->mTimes));
        EXPECT_TRUE(continuous(random->mTimes));

        // An update that inserts and removes nothing keeps the hair.
        npc.runPartUpdate([] {});
        runFrames(5);

        ASSERT_EQ(steady->mTimes.size(), 15u);
        ASSERT_EQ(random->mTimes.size(), 15u);
        EXPECT_TRUE(continuous(steady->mTimes));
        EXPECT_TRUE(continuous(random->mTimes));
    }

    // N3, mutation (c): a part's auto-play controllers run in the part's own clock, never in the object's autoPlay
    // (nothing removes them from it), so a removed part never runs again.
    TEST_F(NpcPartsTest, a_removed_part_never_runs_its_auto_play_controllers_again)
    {
        TestNpc& npc = *mNpc;
        vsg::ref_ptr<Recorder> steady = autoPlayRecorder(0);
        vsg::ref_ptr<Recorder> random = autoPlayRecorder(2);
        const std::string hair = addModel("meshes/test/hair.nif", { steady, random });
        npc.runPartUpdate([&] { insertPart(ESM::PRT_Hair, hair); });
        EXPECT_EQ(countIn(npc.autoPlay, steady.get()), 0u);
        EXPECT_EQ(countIn(npc.autoPlay, random.get()), 0u);
        EXPECT_EQ(countIn(npc.mPartControllers.autoPlay(ESM::PRT_Hair), steady.get()), 1u);
        EXPECT_EQ(countIn(npc.mPartControllers.autoPlay(ESM::PRT_Hair), random.get()), 1u);
        runFrames(5);
        ASSERT_EQ(steady->mTimes.size(), 5u);
        ASSERT_EQ(random->mTimes.size(), 5u);

        npc.runPartUpdate([&] { npc.removeIndividualPart(ESM::PRT_Hair); });
        runFrames(5);

        EXPECT_EQ(steady->mTimes.size(), 5u);
        EXPECT_EQ(random->mTimes.size(), 5u);
        EXPECT_EQ(countIn(npc.autoPlay, steady.get()), 0u);
        EXPECT_EQ(countIn(npc.autoPlay, random.get()), 0u);
        EXPECT_TRUE(npc.mPartControllers.autoPlay(ESM::PRT_Hair).groups.empty());
    }

    // N4, finding 2: a part inserted and then removed in the same part update leaves no controller in the static list,
    // and a kept part's static controller is linked once.
    TEST_F(NpcPartsTest, a_part_inserted_and_removed_in_one_update_leaves_no_static_controller)
    {
        TestNpc& npc = *mNpc;
        vsg::ref_ptr<Recorder> kept = listRecorder();
        vsg::ref_ptr<Recorder> passing = listRecorder();
        const std::string groin = addModel("meshes/test/groin.nif", { kept });
        const std::string cuirass = addModel("meshes/test/cuirass.nif", { passing });
        npc.runPartUpdate([&] { insertPart(ESM::PRT_Groin, groin); });
        ASSERT_EQ(countIn(npc.mStaticControllers, kept.get()), 1u);

        npc.runPartUpdate([&] {
            insertPart(ESM::PRT_Cuirass, cuirass);
            npc.removeIndividualPart(ESM::PRT_Cuirass);
        });

        EXPECT_EQ(countIn(npc.mStaticControllers, passing.get()), 0u);
        EXPECT_EQ(countIn(npc.mStaticControllers, kept.get()), 1u);
        const std::size_t keptRuns = kept->mTimes.size();
        runFrames(3);
        EXPECT_TRUE(passing->mTimes.empty());
        EXPECT_EQ(kept->mTimes.size(), keptRuns + 3);
    }

    // N4, finding 2: the same for a head, whose controllers run in the face.
    TEST_F(NpcPartsTest, a_head_inserted_and_removed_in_one_update_leaves_no_face_controller)
    {
        TestNpc& npc = *mNpc;
        vsg::ref_ptr<Recorder> kept = listRecorder();
        vsg::ref_ptr<Recorder> passing = listRecorder();
        const std::string groin = addModel("meshes/test/groin.nif", { kept });
        const std::string head = addModel("meshes/test/head.nif", { passing });
        npc.runPartUpdate([&] { insertPart(ESM::PRT_Groin, groin); });
        ASSERT_EQ(countIn(npc.mStaticControllers, kept.get()), 1u);

        npc.runPartUpdate([&] {
            insertPart(ESM::PRT_Head, head);
            npc.removeIndividualPart(ESM::PRT_Head);
        });

        EXPECT_EQ(countIn(*npc.mFace, passing.get()), 0u);
        EXPECT_EQ(countIn(npc.mStaticControllers, passing.get()), 0u);
        EXPECT_EQ(countIn(npc.mStaticControllers, kept.get()), 1u);
        const std::size_t keptRuns = kept->mTimes.size();
        runFrames(3);
        EXPECT_TRUE(passing->mTimes.empty());
        EXPECT_EQ(kept->mTimes.size(), keptRuns + 3);
    }

    // N5, finding 2: after removeAllParts (a rebuild, a failed compile) no controller of a part or of a holstered
    // item runs, and no list holds one.
    TEST_F(NpcPartsTest, after_removing_every_part_no_part_or_holstered_controller_runs)
    {
        TestNpc& npc = *mNpc;
        const std::vector<std::pair<std::string, vsg::ref_ptr<Recorder>>> recorders = {
            { "cuirass, static list", listRecorder() },
            { "hair, its clock", autoPlayRecorder(0) },
            { "head, the face", listRecorder() },
            { "weapon, the weapon list", listRecorder() },
            { "holstered, its clock", autoPlayRecorder(0) },
            { "holstered, static list", listRecorder() },
        };
        const std::string cuirass = addModel("meshes/test/cuirass.nif", { recorders[0].second });
        const std::string hair = addModel("meshes/test/hair.nif", { recorders[1].second });
        const std::string head = addModel("meshes/test/head.nif", { recorders[2].second });
        const std::string weapon = addModel("meshes/test/weapon.nif", { recorders[3].second });
        vsg::ref_ptr<vsg::Node> holstered = makeModel({ recorders[4].second, recorders[5].second });
        npc.runPartUpdate([&] {
            insertPart(ESM::PRT_Cuirass, cuirass);
            insertPart(ESM::PRT_Hair, hair);
            insertPart(ESM::PRT_Head, head);
            insertPart(ESM::PRT_Weapon, weapon);
            // updateHolsteredWeapon, inside the part update as in the game.
            MWAnim::PlaceholderResult result = MWAnim::decorate(holstered, nullptr);
            npc.mScabbard = npc.attachHolstered(holstered, result, "Bip01 Weapon", std::nullopt, &npc.mScabbardClock);
        });
        ASSERT_FALSE(npc.mScabbard.empty());
        runFrames(2);
        for (const auto& [name, recorder] : recorders)
        {
            ASSERT_EQ(recorder->mTimes.size(), 2u) << name;
        }

        npc.removeAllParts();

        ASSERT_NE(npc.mFace, nullptr);
        for (const auto& [name, recorder] : recorders)
        {
            EXPECT_EQ(countIn(npc.mStaticControllers, recorder.get()), 0u) << name;
            EXPECT_EQ(countIn(npc.mWeaponControllers, recorder.get()), 0u) << name;
            EXPECT_EQ(countIn(*npc.mFace, recorder.get()), 0u) << name;
        }
        runFrames(3);
        for (const auto& [name, recorder] : recorders)
        {
            EXPECT_EQ(recorder->mTimes.size(), 2u) << name;
        }
    }

    // N6, mutations (b) and (e): a holstered item's auto-play controllers run in its own clock on every update, and
    // never again once removeHolstered took it away.
    void checkHolsteredClock(NpcPartsTest& test, std::vector<vsg::Node*>& path, Anim::AutoPlay& clock,
        const std::string& bone, const std::string& model)
    {
        TestNpc& npc = *test.mNpc;
        vsg::ref_ptr<Recorder> steady = autoPlayRecorder(0);
        vsg::ref_ptr<Recorder> random = autoPlayRecorder(3);
        vsg::ref_ptr<vsg::Node> node = makeModel({ steady, random });
        npc.runPartUpdate([&] {
            MWAnim::PlaceholderResult result = MWAnim::decorate(node, nullptr);
            path = npc.attachHolstered(node, result, bone, std::nullopt, &clock);
        });
        ASSERT_FALSE(path.empty()) << model;
        test.runFrames(5);
        ASSERT_EQ(steady->mTimes.size(), 5u) << model;
        ASSERT_EQ(random->mTimes.size(), 5u) << model;
        EXPECT_EQ(steady->mTimes.front(), frameTime) << model;
        EXPECT_TRUE(continuous(steady->mTimes)) << model;
        EXPECT_TRUE(continuous(random->mTimes)) << model;
        EXPECT_EQ(countIn(npc.autoPlay, steady.get()), 0u) << model;

        npc.runPartUpdate([&] { npc.removeHolstered(path, clock); });
        test.runFrames(5);

        EXPECT_EQ(steady->mTimes.size(), 5u) << model;
        EXPECT_EQ(random->mTimes.size(), 5u) << model;
        EXPECT_TRUE(clock.groups.empty()) << model;
    }

    TEST_F(NpcPartsTest, a_holstered_weapon_runs_its_clock_until_it_is_removed)
    {
        checkHolsteredClock(*this, mNpc->mScabbard, mNpc->mScabbardClock, "Bip01 Weapon", "scabbard");
    }

    TEST_F(NpcPartsTest, a_holstered_shield_runs_its_clock_until_it_is_removed)
    {
        checkHolsteredClock(
            *this, mNpc->mHolsteredShield, mNpc->mHolsteredShieldClock, "Bip01 AttachShield", "holstered shield");
    }

    // N7, mutation (d): a kept merged copy's controllers are added to the static list once per part update, by the
    // relink at its end, not also by attachMergedCopy.
    TEST_F(NpcPartsTest, a_kept_merged_copy_is_linked_once_per_part_update)
    {
        TestNpc& npc = *mNpc;
        vsg::ref_ptr<Recorder> skin = listRecorder();
        const vsg::ref_ptr<vsg::Object> boneMatrices = vsg::Object::create();
        const vsg::ref_ptr<vsg::Node> templateNode = vsg::Group::create();
        // The actor's copy, attached as attachMergedCopy attaches a new one.
        npc.mMergedNode = vsg::Group::create();
        npc.mMergedBody = MWAnim::attachBonesAndNode(
            npc.mMergedNode, *npc.nodeToAddChildrenTo(), npc.animation->bones, MWAnim::sPartBones.at(ESM::PRT_Groin));
        ASSERT_FALSE(npc.mMergedBody.empty());
        npc.mMergedTemplate = templateNode;
        npc.mMergedHidden = {};
        npc.mMergedControllers = { TestNpc::MergedLink{ skin.get(), boneMatrices.get(), nullptr } };
        // Without it the first-draw log line reads the (empty) reference.
        npc.mMergedDrawn = true;

        ESM::Race race;
        race.blank();
        MWRender::MergedBodies::TemplateResult result;
        result.mNode = templateNode;
        result.mReason = Body::MergedBodyReason::Merged;
        const std::vector<int> hidden = npc.mMergedHidden;

        bool kept = false;
        npc.runPartUpdate([&] { kept = npc.attachMergedCopy(race, false, result, hidden); });

        EXPECT_TRUE(kept);
        EXPECT_TRUE(npc.mMergedNode.valid());
        EXPECT_EQ(countIn(npc.mStaticControllers, skin.get()), 1u);
        runFrames(3);
        EXPECT_EQ(skin->mTimes.size(), 3u);

        // The next equipment change keeps it again.
        kept = false;
        npc.runPartUpdate([&] { kept = npc.attachMergedCopy(race, false, result, hidden); });

        EXPECT_TRUE(kept);
        EXPECT_EQ(countIn(npc.mStaticControllers, skin.get()), 1u);
        runFrames(2);
        EXPECT_EQ(skin->mTimes.size(), 5u);
    }

    // A creature that wields (MWAnim::createCreature for a creature with weapons), with the part functions in reach.
    struct TestCreature : public MWRender::WieldingCreature
    {
        using MWRender::WieldingCreature::linkPart;
        using MWRender::WieldingCreature::mPartControllers;
        using MWRender::WieldingCreature::removeParts;
        using MWRender::WieldingCreature::WieldingCreature;
    };

    struct CreaturePartsTest : public ::testing::Test
    {
        MWAnim::Context mContext;
        // Declared after the context: it goes first.
        std::unique_ptr<TestCreature> mCreature;

        void SetUp() override
        {
            mContext.compileContext = quietCompileContext();
            mCreature = std::make_unique<TestCreature>(mContext);
            // A creature's skeleton is its own model, attached under its node; its bones are attached
            // (MWAnim::attachNode hangs a part off the bone itself).
            vsg::ref_ptr<vsg::Group> skeleton = makeSkeleton({ "Weapon Bone", "Weapon Bone Left", "Shield Bone" });
            mCreature->nodeToAddChildrenTo()->addChild(skeleton);
            mCreature->animation = std::make_unique<MWAnim::Play>(MWAnim::Bones(*skeleton));
        }

        void runFrames(int frames)
        {
            for (int i = 0; i < frames; ++i)
                mCreature->update(frameTime);
        }
    };

    // C1, mutation (b): WieldingCreature::update runs its parts' clocks.
    TEST_F(CreaturePartsTest, a_linked_weapon_runs_its_auto_play_clock_on_every_update)
    {
        TestCreature& creature = *mCreature;
        vsg::ref_ptr<Recorder> steady = autoPlayRecorder(0);
        vsg::ref_ptr<Recorder> random = autoPlayRecorder(1);
        creature.linkPart(MWWorld::InventoryStore::Slot_CarriedRight, makeModel({ steady, random }), nullptr,
            "Weapon Bone", std::nullopt);
        runFrames(10);

        ASSERT_EQ(steady->mTimes.size(), 10u);
        ASSERT_EQ(random->mTimes.size(), 10u);
        EXPECT_EQ(steady->mTimes.front(), frameTime);
        EXPECT_TRUE(continuous(steady->mTimes));
        EXPECT_TRUE(continuous(random->mTimes));
    }

    // C2, mutation (c): the weapon's auto-play controllers are in its own clock, not in the creature's autoPlay, so
    // they go with removeParts, while the creature's own model keeps running.
    TEST_F(CreaturePartsTest, removed_parts_never_run_again_and_the_creature_s_own_model_keeps_running)
    {
        TestCreature& creature = *mCreature;
        // The creature's own model (MWAnim::createCreature: ObjectAnimation::addController).
        vsg::ref_ptr<Recorder> ownSteady = autoPlayRecorder(0);
        vsg::ref_ptr<Recorder> ownRandom = autoPlayRecorder(2);
        const vsg::ref_ptr<vsg::Object> ownTarget = vsg::Object::create();
        creature.addController(ownSteady.get(), ownTarget.get());
        creature.addController(ownRandom.get(), ownTarget.get());
        vsg::ref_ptr<Recorder> weapon = autoPlayRecorder(1);
        creature.linkPart(
            MWWorld::InventoryStore::Slot_CarriedRight, makeModel({ weapon }), nullptr, "Weapon Bone", std::nullopt);
        EXPECT_EQ(countIn(creature.autoPlay, weapon.get()), 0u);
        runFrames(5);
        ASSERT_EQ(weapon->mTimes.size(), 5u);
        ASSERT_EQ(ownSteady->mTimes.size(), 5u);

        creature.removeParts();
        runFrames(5);

        EXPECT_EQ(weapon->mTimes.size(), 5u);
        EXPECT_EQ(countIn(creature.autoPlay, weapon.get()), 0u);
        ASSERT_EQ(ownSteady->mTimes.size(), 10u);
        ASSERT_EQ(ownRandom->mTimes.size(), 10u);
        EXPECT_TRUE(continuous(ownSteady->mTimes));
        EXPECT_TRUE(continuous(ownRandom->mTimes));
    }

    // C3, finding B: a creature carrying a light in its left hand gives light, and a flickering one flickers.
    TEST_F(CreaturePartsTest, a_carried_flickering_light_gives_light_and_flickers)
    {
        TestCreature& creature = *mCreature;
        const ESM::Light torch = lightRecord(ESM::Light::Carry | ESM::Light::Flicker);
        creature.linkPart(
            MWWorld::InventoryStore::Slot_CarriedLeft, makePlainModel(), &torch, "Weapon Bone Left", std::nullopt);

        const std::vector<vsg::Light*> lights = lightsUnder(*creature.node());
        ASSERT_EQ(lights.size(), 1u);
        EXPECT_GT(lightColours(creature, *lights[0], 30).size(), 10u);
    }

    TEST_F(CreaturePartsTest, a_carried_steady_light_gives_light_and_nothing_runs_for_it)
    {
        TestCreature& creature = *mCreature;
        const ESM::Light lantern = lightRecord(ESM::Light::Carry);
        creature.linkPart(
            MWWorld::InventoryStore::Slot_CarriedLeft, makePlainModel(), &lantern, "Weapon Bone Left", std::nullopt);

        const std::vector<vsg::Light*> lights = lightsUnder(*creature.node());
        ASSERT_EQ(lights.size(), 1u);
        EXPECT_FALSE(runsOn(creature.autoPlay, lights[0]));
        for (const Anim::AutoPlay& clock : creature.mPartControllers.clocks())
        {
            EXPECT_FALSE(runsOn(clock, lights[0]));
        }
        EXPECT_EQ(lightColours(creature, *lights[0], 30).size(), 1u);
    }
}
