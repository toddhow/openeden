// Task 0286: a script's MoveTo goes through the world's cell move (PapyrusWorldHooks::moveToCell) when
// PapyrusHost::setCellMoves is on: the reference keeps its identity (no new reference, nothing hidden, the quest's
// alias still holds it), at any distance from the player. The relocation of task 0268 stays as the fallback when the
// world refuses. Generated scripts, a fake game and fake hooks. Written first and run red against a host that has no
// cell move.
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/papyrus_world.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>

#include <openeden/game/world_source_service.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"
#include "../papyrus/papyrustestutil.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using PexAsm::Bool;
    using PexAsm::Flt;
    using PexAsm::Id;
    using PexAsm::ScriptBuilder;

    constexpr FormId kQuestA = 0x01000800;
    constexpr FormId kActor = 0x01000A01;
    constexpr FormId kMarker = 0x01000A02;
    constexpr FormId kBaseActor = 0x01000B01;
    constexpr FormId kBaseHost = 0x01000B00;
    constexpr FormId kCellFar = 0x01000C01;
    constexpr FormId kCellNear = 0x01000C02;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    class FakeData final : public PapyrusGameData
    {
    public:
        bool readPex(const std::string& name, std::vector<std::uint8_t>& bytes) override
        {
            const auto it = mScripts.find(name);
            if (it == mScripts.end())
                return false;
            bytes = it->second;
            return true;
        }
        std::string formClass(FormId form) const override
        {
            const auto it = mClasses.find(form);
            return it != mClasses.end() ? it->second : std::string();
        }
        bool referencePosition(FormId reference, double& x, double& y, double& z) const override
        {
            const auto it = mPositions.find(reference);
            if (it == mPositions.end())
                return false;
            x = it->second[0];
            y = it->second[1];
            z = it->second[2];
            return true;
        }
        FormId baseOf(FormId reference) const override
        {
            const auto it = mBases.find(reference);
            return it != mBases.end() ? it->second : 0;
        }
        FormId cellOf(FormId reference) const override
        {
            const auto it = mCellOf.find(reference);
            return it != mCellOf.end() ? it->second : 0;
        }
        void add(const ScriptBuilder& script)
        {
            std::string lower = script.name();
            for (char& c : lower)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            mScripts[lower] = script.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        }
        std::map<std::string, std::vector<std::uint8_t>> mScripts;
        std::map<FormId, std::string> mClasses;
        std::map<FormId, std::array<double, 3>> mPositions;
        std::map<FormId, FormId> mBases;
        std::map<FormId, FormId> mCellOf;
    };

    class FakeHooks final : public PapyrusWorldHooks
    {
    public:
        struct CellMove
        {
            FormId mReference;
            FormId mCell;
            ObjectMoveRequest mRequest;
        };
        bool moveObject(FormId reference, const ObjectMoveRequest&) override
        {
            mMoves.push_back(reference);
            return true;
        }
        bool moveToCell(FormId reference, FormId destinationCell, const ObjectMoveRequest& request) override
        {
            mCellMoves.push_back({ reference, destinationCell, request });
            return mAccept;
        }
        bool setObjectEnabled(FormId reference, bool enabled, float) override
        {
            mEnables.emplace_back(reference, enabled);
            return true;
        }
        ObjectPlaceResult relocateObject(FormId, FormId, const ObjectMoveRequest&, FormId cell) override
        {
            ++mRelocations;
            return { static_cast<FormId>(openeden::sdk::game::kCreatedObjectBase + mRelocations), cell };
        }
        std::vector<FormId> mMoves;
        std::vector<CellMove> mCellMoves;
        std::vector<std::pair<FormId, bool>> mEnables;
        int mRelocations = 0;
        bool mAccept = true;
    };

    ScriptBuilder probeScript()
    {
        ScriptBuilder s("Probe");
        s.parent("ObjectReference");
        s.autoProperty("Marker", "ObjectReference");
        s.function("GoToMarker")
            .local("n", "None")
            .callMethod(
                "MoveTo", Id("self"), Id("n"), { Id("::Marker_var"), Flt(5.f), Flt(0.f), Flt(0.f), Bool(false) });
        return s;
    }

    vmad::Script probeOn()
    {
        vmad::Script s;
        s.mName = "Probe";
        vmad::Property p;
        p.mName = "Marker";
        p.mType = 1;
        p.mValue.mKind = vmad::ValueKind::Object;
        p.mValue.mForm = kMarker;
        p.mValue.mAlias = -1;
        s.mProperties = { p };
        return s;
    }

    Bytes questWithAlias()
    {
        Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
        Bytes quest;
        quest.append(sub4("EDID", Bytes().zstring("Q1")));
        quest.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
        quest.append(sub4("NEXT", Bytes()));
        quest.append(sub4("ALST", Bytes().u32(1)));
        quest.append(sub4("ALID", Bytes().zstring("Actor")));
        quest.append(sub4("FNAM", Bytes().u32(0)));
        quest.append(sub4("ALFR", Bytes().u32(kActor)));
        quest.append(sub4("VTCK", Bytes().u32(0)));
        quest.append(sub4("ALED", Bytes()));
        bytes.append(group4(Content::fourCC("QUST"), 0, record4("QUST", 0u, kQuestA, quest)));
        return bytes;
    }

    struct Rig
    {
        Rig()
        {
            ScriptBuilder form("Form");
            form.function("GetFormID", "Int").native();
            mData.add(form);
            ScriptBuilder ref("ObjectReference");
            ref.parent("Form");
            ref.function("MoveTo")
                .native()
                .param("akTarget", "ObjectReference")
                .param("afXOffset", "Float")
                .param("afYOffset", "Float")
                .param("afZOffset", "Float")
                .param("abMatchRotation", "Bool");
            mData.add(ref);
            mData.add(probeScript());
            mData.mClasses[kQuestA] = "Quest";
            mData.mClasses[kActor] = "ObjectReference";
            mData.mClasses[kMarker] = "ObjectReference";
            mData.mClasses[kBaseActor] = "Form";
            mData.mPositions[kActor] = { 50000.0, 50000.0, 0.0 };
            mData.mPositions[kMarker] = { 1000.0, 2000.0, 300.0 };
            mData.mBases[kActor] = kBaseActor;
            mData.mCellOf[kActor] = kCellFar;
            mData.mCellOf[kMarker] = kCellNear;
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            mHost->setWorldHooks(&mHooks);
            const Bytes bytes = questWithAlias();
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mHost->setQuests(&mIndex, &mState, [](FormId) -> const vmad::Vmad* { return nullptr; });
            mHost->startGame();
            EXPECT_EQ(mHost->attachReference(kActor, kBaseHost, false, { probeOn() }), 1u);
            tick();
        }
        ~Rig()
        {
            if (::testing::Test::HasFailure())
                for (const std::string& line : mErrors)
                    std::cerr << "  error: " << line << std::endl;
        }
        void tick(int n = 3)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(0.1f);
        }
        bool call(const char* function)
        {
            const bool ok = mHost->callFunction(kActor, "Probe", function);
            tick();
            return ok;
        }
        FakeData mData;
        FakeHooks mHooks;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST(SkyrimCellMoves, aMoveToIsNotAskedOfTheWorldUnlessTheSettingIsOn)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("GoToMarker"));
        EXPECT_TRUE(rig.mHooks.mCellMoves.empty());
        EXPECT_EQ(rig.mHooks.mMoves.size(), 1u) << "the move goes the old way";
    }

    TEST(SkyrimCellMoves, aMoveToIsDoneByTheWorldIntoTheTargetsCellKeepingTheReference)
    {
        Rig rig;
        rig.mHost->setCellMoves(true);
        ASSERT_TRUE(rig.call("GoToMarker"));
        ASSERT_EQ(rig.mHooks.mCellMoves.size(), 1u);
        const FakeHooks::CellMove& m = rig.mHooks.mCellMoves[0];
        EXPECT_EQ(m.mReference, kActor);
        EXPECT_EQ(m.mCell, kCellNear) << "the cell of the marker it goes to";
        ASSERT_TRUE(m.mRequest.hasPosition);
        EXPECT_EQ(m.mRequest.position[0], 1005.0) << "the marker's place plus the offsets";
        EXPECT_EQ(m.mRequest.position[1], 2000.0);
        EXPECT_EQ(m.mRequest.position[2], 300.0);
        ASSERT_TRUE(m.mRequest.hasFrom) << "where the actor stands now, for a reference whose cell id does not say";
        EXPECT_EQ(m.mRequest.from[0], 50000.0);
        EXPECT_EQ(rig.mHooks.mRelocations, 0) << "no new reference";
        EXPECT_TRUE(rig.mHooks.mEnables.empty()) << "nothing was hidden";
        EXPECT_TRUE(rig.mHooks.mMoves.empty()) << "nothing was moved in place";
        EXPECT_EQ(rig.mState.aliasFill(kQuestA, 1).mReference, kActor) << "the alias holds the same reference";
        const auto at = rig.mHost->positionOverride(kActor);
        ASSERT_TRUE(at.has_value());
        EXPECT_EQ((*at)[0], 1005.0);
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimCellMoves, aSecondMoveStartsFromTheCellTheFirstMoveEndedIn)
    {
        Rig rig;
        rig.mHost->setCellMoves(true);
        ASSERT_TRUE(rig.call("GoToMarker"));
        ASSERT_TRUE(rig.call("GoToMarker"));
        ASSERT_EQ(rig.mHooks.mCellMoves.size(), 2u);
        EXPECT_EQ(rig.mHooks.mCellMoves[1].mReference, kActor);
        EXPECT_EQ(rig.mHooks.mCellMoves[1].mRequest.from[0], 1005.0) << "from where the first move put it";
    }

    TEST(SkyrimCellMoves, aWorldThatRefusesLeavesTheOlderWaysToRun)
    {
        Rig rig;
        rig.mHost->setCellMoves(true);
        rig.mHooks.mAccept = false;
        ASSERT_TRUE(rig.call("GoToMarker"));
        EXPECT_EQ(rig.mHooks.mCellMoves.size(), 1u);
        EXPECT_EQ(rig.mHooks.mMoves.size(), 1u) << "the move inside the cell ran";
        EXPECT_EQ(rig.mHost->positionOverride(kActor).value_or(std::array<double, 3>{ 0, 0, 0 })[0], 1005.0);
    }

    TEST(SkyrimCellMoves, theRelocationOfTheMainQuestIsOnlyTheFallback)
    {
        Rig rig;
        rig.mHost->setCellMoves(true);
        rig.mHost->setRelocation(true, 100000.0);
        PapyrusPlayerView view;
        view.mKnown = true;
        view.mX = 1000.0;
        view.mY = 2000.0;
        rig.mHost->setPlayerView(view);
        ASSERT_TRUE(rig.call("GoToMarker"));
        EXPECT_EQ(rig.mHooks.mRelocations, 0) << "the world moved the actor itself";
        rig.mHooks.mAccept = false;
        ASSERT_TRUE(rig.call("GoToMarker"));
        EXPECT_EQ(rig.mHooks.mRelocations, 1) << "the world refused, the older glue made a new reference";
    }
}
