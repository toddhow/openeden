// Task 0202 (milestone M-S6): the object natives of the Papyrus host that reach the world: MoveTo,
// MoveToMyEditorLocation,
// SetPosition, SetAngle, Enable(abFadeIn), Disable(abFadeOut) and PlaceAtMe, through PapyrusWorldHooks, and what the
// host
// keeps and saves of them (position and angle overrides, the table of created references). Generated scripts, a fake
// game and fake hooks; no game data. Written first and run red against stubs (natives not registered, accessors empty).
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/papyrus_world.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>

#include <openeden/game/world_source_service.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"
#include "../papyrus/papyrustestutil.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using openeden::sdk::game::kCreatedObjectBase;
    using PexAsm::Bool;
    using PexAsm::Flt;
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::Nil;
    using PexAsm::ScriptBuilder;
    using PexAsm::Str;

    constexpr double kPi = 3.14159265358979323846;
    constexpr FormId kQuestA = 0x01000800;
    constexpr FormId kBarrel = 0x01000A01; // the reference scripts run on
    constexpr FormId kOther = 0x01000A02; // the reference they move to
    constexpr FormId kBaseBarrel = 0x01000B01;
    constexpr FormId kBaseHost = 0x01000B00;
    constexpr FormId kCellMare = 0x01000C01;

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
        void add(const ScriptBuilder& script)
        {
            std::string lower = script.name();
            for (char& c : lower)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            mScripts[lower] = script.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        }
        FormId cellOf(FormId reference) const override
        {
            const auto it = mCellOf.find(reference);
            return it != mCellOf.end() ? it->second : 0;
        }
        bool cellIsInterior(FormId cell) const override { return mInteriors.count(cell) != 0; }
        std::map<FormId, FormId> mCellOf;
        std::set<FormId> mInteriors;
        std::map<std::string, std::vector<std::uint8_t>> mScripts;
        std::map<FormId, std::string> mClasses;
        std::map<FormId, std::array<double, 3>> mPositions;
        std::map<FormId, FormId> mBases;
    };

    // Records what the host asked the world to do.
    class FakeHooks final : public PapyrusWorldHooks
    {
    public:
        struct Enable
        {
            FormId mReference;
            bool mEnabled;
            float mFade;
        };
        struct Move
        {
            FormId mReference;
            ObjectMoveRequest mRequest;
        };
        bool setObjectEnabled(FormId reference, bool enabled, float fadeSeconds) override
        {
            mEnables.push_back({ reference, enabled, fadeSeconds });
            mOrder.push_back("enable");
            return mHandlesEnable;
        }
        bool moveObject(FormId reference, const ObjectMoveRequest& request) override
        {
            mMoves.push_back({ reference, request });
            mOrder.push_back("move");
            return true;
        }
        ObjectPlaceResult placeObject(const ObjectPlaceRequest& request) override
        {
            mPlaces.push_back(request);
            mOrder.push_back("place");
            if (!mCanPlace)
                return {};
            return { static_cast<FormId>(kCreatedObjectBase + mPlaces.size()), kCellMare };
        }
        std::vector<Enable> mEnables;
        std::vector<Move> mMoves;
        std::vector<ObjectPlaceRequest> mPlaces;
        std::vector<std::string> mOrder;
        bool mHandlesEnable = true;
        bool mCanPlace = true;
    };

    void addClasses(FakeData& data)
    {
        ScriptBuilder form("Form");
        form.function("GetFormID", "Int").native();
        data.add(form);
        ScriptBuilder ref("ObjectReference");
        ref.parent("Form");
        ref.function("Enable").native().param("abFadeIn", "Bool");
        ref.function("Disable").native().param("abFadeOut", "Bool");
        ref.function("EnableNoWait").native().param("abFadeIn", "Bool");
        ref.function("DisableNoWait").native().param("abFadeOut", "Bool");
        ref.function("IsDisabled", "Bool").native();
        ref.function("MoveTo")
            .native()
            .param("akTarget", "ObjectReference")
            .param("afXOffset", "Float")
            .param("afYOffset", "Float")
            .param("afZOffset", "Float")
            .param("abMatchRotation", "Bool");
        ref.function("MoveToMyEditorLocation").native();
        ref.function("SetPosition").native().param("afX", "Float").param("afY", "Float").param("afZ", "Float");
        ref.function("SetAngle")
            .native()
            .param("afAngleX", "Float")
            .param("afAngleY", "Float")
            .param("afAngleZ", "Float");
        ref.function("GetPositionX", "Float").native();
        ref.function("GetPositionY", "Float").native();
        ref.function("GetPositionZ", "Float").native();
        ref.function("GetBaseObject", "Form").native();
        ref.function("PlaceAtMe", "ObjectReference")
            .native()
            .param("akFormToPlace", "Form")
            .param("aiCount", "Int")
            .param("abForcePersist", "Bool")
            .param("abInitiallyDisabled", "Bool");
        data.add(ref);
    }

    ScriptBuilder probeScript()
    {
        ScriptBuilder s("Probe");
        s.parent("ObjectReference");
        s.autoProperty("Other", "ObjectReference");
        s.autoProperty("Thing", "Form");
        s.var("x", "Float");
        s.var("y", "Float");
        s.var("z", "Float");
        s.var("flag", "Bool");
        s.var("placed", "ObjectReference");
        s.var("placedIsNone", "Bool");
        s.var("baseId", "Int");
        s.function("MoveToOther")
            .local("n", "None")
            .callMethod(
                "MoveTo", Id("self"), Id("n"), { Id("::Other_var"), Flt(10.f), Flt(20.f), Flt(30.f), Bool(true) });
        s.function("MoveToOtherNoRotation")
            .local("n", "None")
            .callMethod(
                "MoveTo", Id("self"), Id("n"), { Id("::Other_var"), Flt(1.f), Flt(2.f), Flt(3.f), Bool(false) });
        s.function("MoveOtherToSelf")
            .local("n", "None")
            .callMethod("MoveTo", Id("::Other_var"), Id("n"), { Id("self"), Flt(0.f), Flt(0.f), Flt(0.f), Bool(true) });
        s.function("MoveToNobody")
            .local("n", "None")
            .callMethod("MoveTo", Id("self"), Id("n"), { Nil(), Flt(1.f), Flt(1.f), Flt(1.f), Bool(true) });
        s.function("SetPos").local("n", "None").callMethod(
            "SetPosition", Id("self"), Id("n"), { Flt(1.f), Flt(2.f), Flt(3.f) });
        s.function("SetAng").local("n", "None").callMethod(
            "SetAngle", Id("self"), Id("n"), { Flt(90.f), Flt(0.f), Flt(180.f) });
        s.function("Home").local("n", "None").callMethod("MoveToMyEditorLocation", Id("self"), Id("n"));
        s.function("ReadPosition")
            .local("v", "Float")
            .callMethod("GetPositionX", Id("self"), Id("v"))
            .assign(Id("x"), Id("v"))
            .callMethod("GetPositionY", Id("self"), Id("v"))
            .assign(Id("y"), Id("v"))
            .callMethod("GetPositionZ", Id("self"), Id("v"))
            .assign(Id("z"), Id("v"));
        s.function("DisableFade").local("n", "None").callMethod("Disable", Id("self"), Id("n"), { Bool(true) });
        s.function("DisablePlain").local("n", "None").callMethod("Disable", Id("self"), Id("n"), { Bool(false) });
        s.function("EnableFade").local("n", "None").callMethod("Enable", Id("self"), Id("n"), { Bool(true) });
        s.function("EnablePlain").local("n", "None").callMethod("Enable", Id("self"), Id("n"), { Bool(false) });
        s.function("DisableNoWaitFade")
            .local("n", "None")
            .callMethod("DisableNoWait", Id("self"), Id("n"), { Bool(true) });
        s.function("Place")
            .local("r", "ObjectReference")
            .callMethod("PlaceAtMe", Id("self"), Id("r"), { Id("::Thing_var"), Int(1), Bool(false), Bool(false) })
            .assign(Id("placed"), Id("r"))
            .op(PexAsm::Op::CompareEq, { Id("placedIsNone"), Id("placed"), Nil() });
        s.function("PlaceThree")
            .local("r", "ObjectReference")
            .callMethod("PlaceAtMe", Id("self"), Id("r"), { Id("::Thing_var"), Int(3), Bool(false), Bool(false) })
            .assign(Id("placed"), Id("r"));
        s.function("PlaceDisabled")
            .local("r", "ObjectReference")
            .local("v", "Bool")
            .callMethod("PlaceAtMe", Id("self"), Id("r"), { Id("::Thing_var"), Int(1), Bool(false), Bool(true) })
            .assign(Id("placed"), Id("r"))
            .callMethod("IsDisabled", Id("r"), Id("v"))
            .assign(Id("flag"), Id("v"));
        s.function("ReadPlaced")
            .local("v", "Float")
            .local("f", "Form")
            .local("i", "Int")
            .callMethod("GetPositionX", Id("placed"), Id("v"))
            .assign(Id("x"), Id("v"))
            .callMethod("GetPositionZ", Id("placed"), Id("v"))
            .assign(Id("z"), Id("v"))
            .callMethod("GetBaseObject", Id("placed"), Id("f"))
            .callMethod("GetFormID", Id("f"), Id("i"))
            .assign(Id("baseId"), Id("i"));
        s.function("MovePlaced")
            .local("n", "None")
            .callMethod("SetPosition", Id("placed"), Id("n"), { Flt(7.f), Flt(8.f), Flt(9.f) })
            .callMethod("Disable", Id("placed"), Id("n"), { Bool(true) });
        return s;
    }

    vmad::Property objProp(const std::string& name, FormId form)
    {
        vmad::Property p;
        p.mName = name;
        p.mType = 1;
        p.mValue.mKind = vmad::ValueKind::Object;
        p.mValue.mForm = form;
        p.mValue.mAlias = -1;
        return p;
    }

    vmad::Script probeOn()
    {
        vmad::Script s;
        s.mName = "Probe";
        s.mProperties = { objProp("Other", kOther), objProp("Thing", kBaseBarrel) };
        return s;
    }

    struct Rig
    {
        Rig()
        {
            addClasses(mData);
            mData.add(probeScript());
            mData.mClasses[kQuestA] = "Quest";
            mData.mClasses[kBarrel] = "ObjectReference";
            mData.mClasses[kOther] = "ObjectReference";
            mData.mClasses[kBaseBarrel] = "Form";
            mData.mPositions[kBarrel] = { 100.0, 200.0, 300.0 };
            mData.mPositions[kOther] = { 3.0, 4.0, 12.0 };
            mData.mBases[kBarrel] = kBaseBarrel;
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) {
                (error ? mErrors : mLogs).push_back(line);
            });
            mHost->setWorldHooks(&mHooks);
            mHost->setEnableChanged([this](FormId reference, bool enabled) {
                mFallbackEnables.emplace_back(reference, enabled);
            });
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes quest;
            quest.append(sub4("EDID", Bytes().zstring("Q1")));
            quest.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
            quest.append(sub4("NEXT", Bytes()));
            bytes.append(group4(Content::fourCC("QUST"), 0, record4("QUST", 0u, kQuestA, quest)));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mHost->setQuests(&mIndex, &mState, [](FormId) -> const vmad::Vmad* { return nullptr; });
            mHost->startGame();
            EXPECT_EQ(mHost->attachReference(kBarrel, kBaseHost, false, { probeOn() }), 1u);
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
            const bool ok = mHost->callFunction(kBarrel, "Probe", function);
            tick();
            return ok;
        }
        std::optional<double> number(const char* name) { return mHost->numberVariable(kBarrel, "Probe", name); }

        FakeData mData;
        FakeHooks mHooks;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
        std::vector<std::pair<FormId, bool>> mFallbackEnables;
    };

    // ---- MoveTo, SetPosition, SetAngle, MoveToMyEditorLocation
    // -------------------------------------------------------

    TEST(SkyrimPapyrusObjects, MoveTo_sends_the_target_position_plus_the_offsets_to_the_world_and_records_it)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("MoveToOther"));
        ASSERT_EQ(rig.mHooks.mMoves.size(), 1u);
        const FakeHooks::Move& move = rig.mHooks.mMoves[0];
        EXPECT_EQ(move.mReference, kBarrel);
        EXPECT_TRUE(move.mRequest.hasPosition);
        EXPECT_EQ(move.mRequest.position[0], 13.0);
        EXPECT_EQ(move.mRequest.position[1], 24.0);
        EXPECT_EQ(move.mRequest.position[2], 42.0);
        EXPECT_FALSE(move.mRequest.hasAngles) << "the target's angles are not known: nothing to match";
        EXPECT_FALSE(move.mRequest.toOriginal);
        const auto recorded = rig.mHost->positionOverride(kBarrel);
        ASSERT_TRUE(recorded.has_value());
        EXPECT_EQ((*recorded)[0], 13.0);
        ASSERT_TRUE(rig.call("ReadPosition"));
        EXPECT_EQ(rig.number("x"), 13.0);
        EXPECT_EQ(rig.number("z"), 42.0);
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimPapyrusObjects, MoveTo_matches_the_rotation_of_a_target_whose_angles_a_script_set)
    {
        Rig rig;
        rig.mHost->callFunction(kBarrel, "Probe", "MoveOtherToSelf"); // moves kOther to the barrel (no angles known)
        rig.tick();
        ASSERT_TRUE(rig.call("SetAng")); // the barrel's angles are now known
        rig.mHooks.mMoves.clear();
        ASSERT_TRUE(rig.call("MoveOtherToSelf"));
        ASSERT_EQ(rig.mHooks.mMoves.size(), 1u);
        EXPECT_EQ(rig.mHooks.mMoves[0].mReference, kOther);
        EXPECT_TRUE(rig.mHooks.mMoves[0].mRequest.hasAngles) << "abMatchRotation copies the target's angles";
        EXPECT_NEAR(rig.mHooks.mMoves[0].mRequest.angles[2], kPi, 1e-9);
        ASSERT_TRUE(rig.call("MoveToOtherNoRotation")); // abMatchRotation false: the position only
        ASSERT_FALSE(rig.mHooks.mMoves.empty());
        EXPECT_FALSE(rig.mHooks.mMoves.back().mRequest.hasAngles);
    }

    TEST(SkyrimPapyrusObjects, MoveTo_into_another_cell_of_which_one_is_an_interior_is_refused_and_not_recorded)
    {
        Rig rig;
        rig.mData.mCellOf[kBarrel] = 0x100;
        rig.mData.mCellOf[kOther] = 0x200;
        rig.mData.mInteriors = { 0x100 };
        ASSERT_TRUE(rig.call("MoveToOther"));
        EXPECT_TRUE(rig.mHooks.mMoves.empty());
        EXPECT_FALSE(rig.mHost->positionOverride(kBarrel).has_value()) << "the world did not move, so nothing is recorded";
        bool said = false;
        for (const std::string& line : rig.mLogs)
            said = said || line.find("another cell") != std::string::npos;
        EXPECT_TRUE(said);
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimPapyrusObjects, MoveTo_within_one_cell_or_between_exterior_cells_or_unknown_cells_goes_through)
    {
        Rig rig;
        rig.mData.mCellOf[kBarrel] = 0x100;
        rig.mData.mCellOf[kOther] = 0x100;
        rig.mData.mInteriors = { 0x100 };
        ASSERT_TRUE(rig.call("MoveToOther"));
        EXPECT_EQ(rig.mHooks.mMoves.size(), 1u) << "same interior";
        rig.mData.mCellOf[kOther] = 0x300; // two exteriors (neither is an interior): the object stays in its home cell
        rig.mData.mInteriors.clear();
        ASSERT_TRUE(rig.call("MoveToOther"));
        EXPECT_EQ(rig.mHooks.mMoves.size(), 2u);
        rig.mData.mCellOf.erase(kOther); // unknown
        rig.mData.mInteriors = { 0x100 };
        ASSERT_TRUE(rig.call("MoveToOther"));
        EXPECT_EQ(rig.mHooks.mMoves.size(), 3u);
    }

    TEST(SkyrimPapyrusObjects, MoveTo_a_target_with_no_position_changes_nothing)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("MoveToNobody"));
        EXPECT_TRUE(rig.mHooks.mMoves.empty());
        EXPECT_FALSE(rig.mHost->positionOverride(kBarrel).has_value());
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimPapyrusObjects, SetPosition_sets_the_position_and_SetAngle_turns_degrees_into_radians)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("SetPos"));
        ASSERT_EQ(rig.mHooks.mMoves.size(), 1u);
        EXPECT_TRUE(rig.mHooks.mMoves[0].mRequest.hasPosition);
        EXPECT_FALSE(rig.mHooks.mMoves[0].mRequest.hasAngles);
        EXPECT_EQ(rig.mHooks.mMoves[0].mRequest.position[1], 2.0);
        ASSERT_TRUE(rig.call("SetAng"));
        ASSERT_EQ(rig.mHooks.mMoves.size(), 2u);
        const ObjectMoveRequest& turn = rig.mHooks.mMoves[1].mRequest;
        EXPECT_FALSE(turn.hasPosition);
        EXPECT_TRUE(turn.hasAngles);
        EXPECT_NEAR(turn.angles[0], kPi / 2.0, 1e-9);
        EXPECT_NEAR(turn.angles[1], 0.0, 1e-9);
        EXPECT_NEAR(turn.angles[2], kPi, 1e-9);
        const auto angles = rig.mHost->angleOverride(kBarrel);
        ASSERT_TRUE(angles.has_value());
        EXPECT_NEAR((*angles)[0], kPi / 2.0, 1e-9);
        EXPECT_EQ(rig.mHost->angleOverrides().size(), 1u);
        EXPECT_EQ(rig.mHost->positionOverrides().size(), 1u);
    }

    TEST(SkyrimPapyrusObjects, MoveToMyEditorLocation_puts_the_reference_back_and_forgets_the_overrides)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("SetPos"));
        ASSERT_TRUE(rig.call("SetAng"));
        rig.mHooks.mMoves.clear();
        ASSERT_TRUE(rig.call("Home"));
        ASSERT_EQ(rig.mHooks.mMoves.size(), 1u);
        EXPECT_TRUE(rig.mHooks.mMoves[0].mRequest.toOriginal);
        EXPECT_FALSE(rig.mHost->positionOverride(kBarrel).has_value());
        EXPECT_FALSE(rig.mHost->angleOverride(kBarrel).has_value());
        ASSERT_TRUE(rig.call("ReadPosition"));
        EXPECT_EQ(rig.number("x"), 100.0) << "the record's place again";
    }

    TEST(SkyrimPapyrusObjects, without_hooks_the_moves_are_only_recorded_and_nothing_fails)
    {
        Rig rig;
        rig.mHost->setWorldHooks(nullptr);
        ASSERT_TRUE(rig.call("MoveToOther"));
        ASSERT_TRUE(rig.call("SetAng"));
        EXPECT_TRUE(rig.mHost->positionOverride(kBarrel).has_value());
        EXPECT_TRUE(rig.mHost->angleOverride(kBarrel).has_value());
        EXPECT_TRUE(rig.mErrors.empty());
    }

    // ---- Enable and Disable with a fade
    // -------------------------------------------------------------------------------

    TEST(SkyrimPapyrusObjects, Disable_with_abFadeOut_asks_for_a_fade_and_without_it_for_none)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("DisableFade"));
        ASSERT_EQ(rig.mHooks.mEnables.size(), 1u);
        EXPECT_EQ(rig.mHooks.mEnables[0].mReference, kBarrel);
        EXPECT_FALSE(rig.mHooks.mEnables[0].mEnabled);
        EXPECT_EQ(rig.mHooks.mEnables[0].mFade, kPapyrusFadeSeconds);
        EXPECT_GT(kPapyrusFadeSeconds, 0.f);
        ASSERT_TRUE(rig.call("EnablePlain"));
        ASSERT_EQ(rig.mHooks.mEnables.size(), 2u);
        EXPECT_TRUE(rig.mHooks.mEnables[1].mEnabled);
        EXPECT_EQ(rig.mHooks.mEnables[1].mFade, 0.f);
        ASSERT_TRUE(rig.call("DisableNoWaitFade"));
        EXPECT_EQ(rig.mHooks.mEnables.back().mFade, kPapyrusFadeSeconds);
        EXPECT_TRUE(rig.mFallbackEnables.empty()) << "the world was told, no fallback";
    }

    TEST(SkyrimPapyrusObjects, a_change_that_changes_nothing_asks_nothing)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("EnableFade")); // it is enabled already
        EXPECT_TRUE(rig.mHooks.mEnables.empty());
        ASSERT_TRUE(rig.call("DisablePlain"));
        ASSERT_TRUE(rig.call("DisableFade")); // disabled already
        EXPECT_EQ(rig.mHooks.mEnables.size(), 1u);
    }

    TEST(SkyrimPapyrusObjects, a_hook_that_does_not_handle_it_leaves_the_old_callback_to_do_it)
    {
        Rig rig;
        rig.mHooks.mHandlesEnable = false;
        ASSERT_TRUE(rig.call("DisablePlain"));
        ASSERT_EQ(rig.mFallbackEnables.size(), 1u);
        EXPECT_EQ(rig.mFallbackEnables[0].first, kBarrel);
        EXPECT_FALSE(rig.mFallbackEnables[0].second);
        rig.mHost->setWorldHooks(nullptr);
        ASSERT_TRUE(rig.call("EnablePlain"));
        EXPECT_EQ(rig.mFallbackEnables.size(), 2u);
    }

    // ---- PlaceAtMe
    // -----------------------------------------------------------------------------------------------------

    TEST(SkyrimPapyrusObjects, PlaceAtMe_asks_the_world_for_an_object_at_the_reference_and_keeps_what_it_made)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("Place"));
        ASSERT_EQ(rig.mHooks.mPlaces.size(), 1u);
        const ObjectPlaceRequest& request = rig.mHooks.mPlaces[0];
        EXPECT_EQ(request.base, kBaseBarrel);
        EXPECT_EQ(request.nearReference, kBarrel);
        EXPECT_EQ(request.nearCell, 0u) << "the barrel is a placed reference: the hook finds its cell";
        EXPECT_EQ(request.position[0], 100.0);
        EXPECT_EQ(request.position[2], 300.0);
        EXPECT_EQ(request.scale, 1.f);
        const std::vector<PapyrusHost::CreatedObject> created = rig.mHost->createdObjects();
        ASSERT_EQ(created.size(), 1u);
        EXPECT_EQ(created[0].mReference, static_cast<FormId>(kCreatedObjectBase + 1));
        EXPECT_EQ(created[0].mBase, kBaseBarrel);
        EXPECT_EQ(created[0].mCell, kCellMare);
        EXPECT_EQ(created[0].mPosition[0], 100.0);
        EXPECT_EQ(rig.number("placedIsNone"), 0.0);
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimPapyrusObjects, a_created_reference_answers_position_and_base_and_can_be_moved_and_faded)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("Place"));
        ASSERT_TRUE(rig.call("ReadPlaced"));
        EXPECT_EQ(rig.number("x"), 100.0);
        EXPECT_EQ(rig.number("z"), 300.0);
        EXPECT_EQ(rig.number("baseId"), static_cast<double>(kBaseBarrel));
        ASSERT_TRUE(rig.call("MovePlaced"));
        const FormId created = kCreatedObjectBase + 1;
        ASSERT_FALSE(rig.mHooks.mMoves.empty());
        EXPECT_EQ(rig.mHooks.mMoves.back().mReference, created);
        EXPECT_EQ(rig.mHooks.mMoves.back().mRequest.position[0], 7.0);
        ASSERT_FALSE(rig.mHooks.mEnables.empty());
        EXPECT_EQ(rig.mHooks.mEnables.back().mReference, created);
        EXPECT_FALSE(rig.mHooks.mEnables.back().mEnabled);
        EXPECT_EQ(rig.mHooks.mEnables.back().mFade, kPapyrusFadeSeconds);
        const auto where = rig.mHost->positionOverride(created);
        ASSERT_TRUE(where.has_value());
        EXPECT_EQ((*where)[2], 9.0);
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimPapyrusObjects, the_calls_reach_the_world_in_the_order_the_script_made_them)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("Place"));
        ASSERT_TRUE(rig.call("MovePlaced"));
        const std::vector<std::string> expected{ "place", "move", "enable" };
        EXPECT_EQ(rig.mHooks.mOrder, expected);
    }

    TEST(SkyrimPapyrusObjects, PlaceAtMe_with_a_count_places_that_many_and_returns_one)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("PlaceThree"));
        EXPECT_EQ(rig.mHooks.mPlaces.size(), 3u);
        EXPECT_EQ(rig.mHost->createdObjects().size(), 3u);
    }

    TEST(SkyrimPapyrusObjects, PlaceAtMe_with_abInitiallyDisabled_disables_the_new_reference_at_once)
    {
        Rig rig;
        ASSERT_TRUE(rig.call("PlaceDisabled"));
        ASSERT_EQ(rig.mHooks.mEnables.size(), 1u);
        EXPECT_EQ(rig.mHooks.mEnables[0].mReference, static_cast<FormId>(kCreatedObjectBase + 1));
        EXPECT_FALSE(rig.mHooks.mEnables[0].mEnabled);
        EXPECT_EQ(rig.mHooks.mEnables[0].mFade, 0.f);
        EXPECT_EQ(rig.number("flag"), 1.0) << "IsDisabled";
    }

    TEST(SkyrimPapyrusObjects, PlaceAtMe_the_world_cannot_serve_gives_none_and_keeps_nothing)
    {
        Rig rig;
        rig.mHooks.mCanPlace = false;
        ASSERT_TRUE(rig.call("Place"));
        EXPECT_EQ(rig.number("placedIsNone"), 1.0);
        EXPECT_TRUE(rig.mHost->createdObjects().empty());
        rig.mHost->setWorldHooks(nullptr);
        ASSERT_TRUE(rig.call("Place"));
        EXPECT_EQ(rig.number("placedIsNone"), 1.0);
        EXPECT_TRUE(rig.mErrors.empty()) << "tolerant, not an error";
    }

    TEST(SkyrimPapyrusObjects, PlaceAtMe_near_a_reference_whose_position_is_unknown_places_nothing)
    {
        Rig rig;
        rig.mData.mPositions.erase(kBarrel);
        ASSERT_TRUE(rig.call("Place"));
        EXPECT_TRUE(rig.mHooks.mPlaces.empty());
        EXPECT_EQ(rig.number("placedIsNone"), 1.0);
    }

    // ---- save
    // ---------------------------------------------------------------------------------------------------------

    // The state ends with an 8 byte FNV-1a 64 checksum of everything before it (little endian): a test that edits the
    // body
    // must write it again, or the restore is refused for the checksum and not for the reason under test.
    void reseal(std::vector<std::uint8_t>& bytes)
    {
        std::uint64_t hash = 0xcbf29ce484222325ull;
        for (std::size_t i = 0; i + 8 < bytes.size(); ++i)
        {
            hash ^= bytes[i];
            hash *= 0x100000001b3ull;
        }
        for (int i = 0; i < 8; ++i)
            bytes[bytes.size() - 8 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(hash >> (8 * i));
    }

    TEST(SkyrimPapyrusObjects, created_references_angles_and_positions_survive_a_save_and_a_restore)
    {
        Rig a;
        ASSERT_TRUE(a.call("Place"));
        ASSERT_TRUE(a.call("SetAng"));
        ASSERT_TRUE(a.call("MoveToOther"));
        const std::vector<std::uint8_t> bytes = a.mHost->saveState();
        ASSERT_FALSE(bytes.empty());

        Rig b;
        ASSERT_TRUE(b.mHost->restoreSavedState(bytes).mOk);
        const std::vector<PapyrusHost::CreatedObject> created = b.mHost->createdObjects();
        ASSERT_EQ(created.size(), 1u);
        EXPECT_EQ(created[0].mReference, static_cast<FormId>(kCreatedObjectBase + 1));
        EXPECT_EQ(created[0].mBase, kBaseBarrel);
        EXPECT_EQ(created[0].mCell, kCellMare);
        ASSERT_TRUE(b.mHost->angleOverride(kBarrel).has_value());
        EXPECT_NEAR((*b.mHost->angleOverride(kBarrel))[2], kPi, 1e-9);
        ASSERT_TRUE(b.mHost->positionOverride(kBarrel).has_value());
        EXPECT_EQ((*b.mHost->positionOverride(kBarrel))[0], 13.0);
        EXPECT_EQ(b.mHost->saveState(), bytes) << "restore then save is the same bytes";
    }

    TEST(SkyrimPapyrusObjects, a_script_variable_that_holds_a_created_reference_survives_a_restore)
    {
        Rig a;
        ASSERT_TRUE(a.call("Place"));
        const std::vector<std::uint8_t> bytes = a.mHost->saveState();
        Rig b;
        const PapyrusRestoreOutcome outcome = b.mHost->restoreSavedState(bytes);
        ASSERT_TRUE(outcome.mOk) << outcome.mError;
        ASSERT_TRUE(b.call("ReadPlaced"));
        EXPECT_EQ(b.number("baseId"), static_cast<double>(kBaseBarrel));
    }

    TEST(SkyrimPapyrusObjects, a_state_from_before_version_three_is_still_read)
    {
        Rig a;
        ASSERT_TRUE(a.call("SetPos"));
        std::vector<std::uint8_t> bytes = a.mHost->saveState();
        ASSERT_GT(bytes.size(), 16u);
        // The version 3 section is two u32 counts (created, angles) in front of the checksum: with none of them, the
        // bytes of a version 2 state are these bytes without that section and with the version field (the u32 after the
        // magic) = 2, sealed again.
        bytes.erase(bytes.end() - 16, bytes.end() - 8); // the section, in front of the checksum
        bytes[4] = 2;
        bytes[5] = bytes[6] = bytes[7] = 0;
        reseal(bytes);
        Rig b;
        const PapyrusRestoreOutcome outcome = b.mHost->restoreSavedState(bytes);
        EXPECT_TRUE(outcome.mOk) << outcome.mError;
        EXPECT_TRUE(b.mHost->positionOverride(kBarrel).has_value());
        EXPECT_TRUE(b.mHost->createdObjects().empty());
    }

    TEST(SkyrimPapyrusObjects, a_saved_created_reference_below_the_created_range_refuses_the_state)
    {
        Rig a;
        ASSERT_TRUE(a.call("Place"));
        std::vector<std::uint8_t> bytes = a.mHost->saveState();
        // the created id is a u32 whose bytes are 01 00 80 FF (little endian 0xFF800001): make it 0x01800001
        bool patched = false;
        for (std::size_t i = bytes.size() - 8 - 3; i-- > 0;) // the last one: the created table is the last section
        {
            if (bytes[i] == 0x01 && bytes[i + 1] == 0x00 && bytes[i + 2] == 0x80 && bytes[i + 3] == 0xFF)
            {
                bytes[i + 3] = 0x01;
                patched = true;
                break;
            }
        }
        ASSERT_TRUE(patched);
        reseal(bytes);
        Rig b;
        const PapyrusRestoreOutcome outcome = b.mHost->restoreSavedState(bytes);
        EXPECT_FALSE(outcome.mOk);
        EXPECT_TRUE(b.mHost->createdObjects().empty()) << "a refusal changes nothing";
    }

    TEST(SkyrimPapyrusObjectHooks, the_pack_hooks_forward_to_their_environment_and_say_cannot_without_it)
    {
        PackWorldEnv env;
        int enables = 0, moves = 0, places = 0;
        env.setObjectEnabled = [&](FormId, bool, float) {
            ++enables;
            return true;
        };
        env.moveObject = [&](FormId, const ObjectMoveRequest&) {
            ++moves;
            return true;
        };
        env.placeObject = [&](const ObjectPlaceRequest&) {
            ++places;
            return ObjectPlaceResult{ 0xFF800009u, 7 };
        };
        PackWorldHooks hooks(std::move(env));
        EXPECT_TRUE(hooks.setObjectEnabled(1, false, 1.f));
        EXPECT_TRUE(hooks.moveObject(1, ObjectMoveRequest{}));
        const ObjectPlaceResult placed = hooks.placeObject(ObjectPlaceRequest{});
        EXPECT_EQ(placed.reference, 0xFF800009u);
        EXPECT_EQ(placed.cell, 7u);
        EXPECT_EQ(enables, 1);
        EXPECT_EQ(moves, 1);
        EXPECT_EQ(places, 1);

        PackWorldHooks bare{ PackWorldEnv{} };
        EXPECT_FALSE(bare.setObjectEnabled(1, false, 0.f));
        EXPECT_FALSE(bare.moveObject(1, ObjectMoveRequest{}));
        EXPECT_EQ(bare.placeObject(ObjectPlaceRequest{}).reference, 0u);
    }

    // Not a test of anything: with OPENEDEN_PAPYRUS_DEMO_DIR_202 set it writes scripts/oedemotick.pex there (a
    // generated
    // script, no game asset) for tools/neutral_session_check/objectsdemo.sh. Four seconds after its reference starts,
    // the
    // placement whose property Limit is 12 (A) moves 100 units along -y, places a new copy of its base near itself and
    // moves
    // that copy 80 units along +x; every other placement (B) disables itself with a fade.
    TEST(SkyrimPapyrusObjectsDemo, writes_the_demo_script_when_asked)
    {
        const char* const dir = std::getenv("OPENEDEN_PAPYRUS_DEMO_DIR_202");
        if (dir == nullptr || *dir == '\0')
            GTEST_SKIP() << "OPENEDEN_PAPYRUS_DEMO_DIR_202 is not set";
        // With OPENEDEN_DEMO_ACTOR set, A also tries SetPosition and a faded Disable on Hulda (the placement 0x1A66E of
        // the Bannered Mare): an actor is not moved or faded by the world, and its Disable takes effect at once.
        const bool actor = std::getenv("OPENEDEN_DEMO_ACTOR") != nullptr;
        ScriptBuilder s("OEDemoTick");
        s.parent("ObjectReference");
        s.autoProperty("Limit", "Int");
        s.autoProperty("Label", "String");
        s.function("OnInit")
            .local("nn", "None")
            .callMethod("RegisterForSingleUpdate", Id("self"), Id("nn"), { Flt(4.0f) });
        auto& f = s.function("OnUpdate");
        f.local("nn", "None").local("isA", "Bool").local("base", "Form").local("r", "ObjectReference");
        f.local("hf", "Form").local("h", "ObjectReference");
        f.op(PexAsm::Op::CompareEq, { Id("isA"), Id("::Limit_var"), Int(12) });
        f.jmpf(Id("isA"), actor ? 10 : 6);
        f.callMethod("MoveTo", Id("self"), Id("nn"), { Id("self"), Flt(0.f), Flt(-100.f), Flt(0.f), Bool(false) });
        f.callMethod("GetBaseObject", Id("self"), Id("base"));
        f.callMethod("PlaceAtMe", Id("self"), Id("r"), { Id("base"), Int(1), Bool(false), Bool(false) });
        f.callMethod("MoveTo", Id("r"), Id("nn"), { Id("self"), Flt(80.f), Flt(0.f), Flt(0.f), Bool(false) });
        if (actor)
        {
            f.callStatic("Game", "GetForm", Id("hf"), { Int(0x1A66E) });
            f.op(PexAsm::Op::Cast, { Id("h"), Id("hf") });
            f.callMethod("SetPosition", Id("h"), Id("nn"), { Flt(0.f), Flt(0.f), Flt(500.f) });
            f.callMethod("Disable", Id("h"), Id("nn"), { Bool(true) });
        }
        f.jmp(2);
        f.callMethod("Disable", Id("self"), Id("nn"), { Bool(true) });
        const std::vector<std::uint8_t> bytes = s.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        const std::filesystem::path folder = std::filesystem::path(dir) / "scripts";
        std::filesystem::create_directories(folder);
        std::ofstream out(folder / "oedemotick.pex", std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(out.good());
    }
}
