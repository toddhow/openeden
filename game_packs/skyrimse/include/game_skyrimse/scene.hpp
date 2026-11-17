#ifndef OPENEDEN_GAME_SKYRIMSE_SCENE_H
#define OPENEDEN_GAME_SKYRIMSE_SCENE_H

// Skyrim SE scenes (task 0194, milestone M-S6 "scenes and more story events"). Pack side only: decode SCEN records and
// merge a load order. The runner that plays a scene is in scene_runner.hpp. Pure: no engine, Papyrus or Qt type.
//
// Layout, VERIFIED on the five masters (2,141 SCEN records, 10,753 phases, 16,435 actions; walker of the task,
// aggregates only; names are xEdit's wbSCEN, RECALLED-VERIFY):
//  - EDID, optional VMAD (scene fragments, see vmad::parseScene), FNAM u32 scene flags (values seen: 0, 1, 3, 4, 5, 6,
//  7,
//    12, 16, 19, 20, 23), then the phases, the actors, the actions, PNAM u32 (the scene's quest), INAM u32 (the last
//    action index), VNAM (16 bytes, not read), and CTDA after VNAM (59 conditions: the scene's own).
//  - Phase: HNAM (empty) opens it, NAM0 name, start conditions (CTDA, CIS1, CIS2), NEXT (empty), completion conditions,
//    NEXT, optional script data (SCHR, SCDA, SCTX, QNAM; the fragments are in the VMAD), WNAM u32 (editor width), HNAM
//    closes it. Conditions: 1,104 start and 2,175 completion in the data; none anywhere else but the 59 scene ones.
//    The most used functions in them: fn_550 IsSceneActionComplete (920 of 3,338), GetStageDone, GetDistance,
//    GetVMQuestVariable, GetStage.
//  - Actor: ALID u32 (the quest alias), LNAM u32 flags, DNAM u32 behaviour flags.
//  - Action: ANAM u16 type (0 dialogue 11,991, 1 package 3,549, 2 timer 895), NAM0 name, ALID u32 (the actor's alias;
//    0 for a timer), INAM u32 (the action's index; the number fn_550 names), FNAM u32 flags (dialogue only), SNAM u32
//    start phase, ENAM u32 end phase, dialogue: DATA u32 (the topic, DIAL; 0 in 3,121 of them), HTID u32 (alias to look
//    at, 0xFFFFFFFF none), DMAX f32, DMIN f32, DEMO, DEVA; package: PNAM u32 (one or more packages); timer: a second
//    SNAM f32 (the seconds); ANAM (empty) closes the action.

#include <game_skyrimse/conditions.hpp>
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/survey.hpp>
#include <game_skyrimse/vmad.hpp>

#include <components/contentsource/plugin.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace game_skyrimse
{
    namespace SceneFlags
    {
        inline constexpr std::uint32_t BeginOnQuestStart = 0x1;
        inline constexpr std::uint32_t StopQuestOnEnd = 0x2;
        inline constexpr std::uint32_t ShowAllText = 0x4;
        inline constexpr std::uint32_t RepeatConditionsWhileTrue = 0x8;
        inline constexpr std::uint32_t Interruptible = 0x10;
    }

    enum class SceneActionType : std::uint8_t
    {
        Dialogue = 0,
        Package = 1,
        Timer = 2,
        Unknown = 255,
    };

    struct SceneActor
    {
        std::uint32_t mAlias = 0;
        std::uint32_t mFlags = 0; ///< LNAM
        std::uint32_t mBehavior = 0; ///< DNAM
    };

    struct SceneAction
    {
        SceneActionType mType = SceneActionType::Unknown;
        std::uint16_t mRawType = 0;
        std::string mName;
        std::uint32_t mAlias = 0;
        std::uint32_t mIndex = 0;
        std::uint32_t mFlags = 0;
        std::uint32_t mStartPhase = 0;
        std::uint32_t mEndPhase = 0;
        FormId mTopic = kNullFormId; ///< DATA (dialogue)
        std::uint32_t mHeadtrackAlias = 0xFFFFFFFFu; ///< HTID
        std::vector<FormId> mPackages; ///< PNAM (package)
        float mSeconds = 0.f; ///< the second SNAM (timer)
        bool mHasSeconds = false;
    };

    struct ScenePhase
    {
        std::string mName;
        std::vector<survey::Ctda> mStartConditions;
        std::vector<survey::Ctda> mCompletionConditions;
        bool mConditionsOk = true; ///< false: a CTDA was damaged
    };

    struct SceneRecord
    {
        RecordCommon mCommon;
        std::uint32_t mFlags = 0;
        FormId mQuest = kNullFormId; ///< the PNAM after the actions
        std::vector<ScenePhase> mPhases;
        std::vector<SceneActor> mActors;
        std::vector<SceneAction> mActions;
        std::vector<survey::Ctda> mConditions; ///< the scene's own (after VNAM)
        bool mConditionsOk = true;
        std::shared_ptr<const vmad::Vmad> mVmad; ///< scripts and fragments (load-order form ids); null when none
        std::uint32_t mPlugin = 0; ///< index into SceneIndex's plugins

        [[nodiscard]] const SceneActor* actor(std::uint32_t alias) const;
        [[nodiscard]] const SceneAction* actionByIndex(std::uint32_t index) const;
    };

    struct SceneLimits
    {
        std::uint64_t mMaxRecordBytes = 16ull * 1024 * 1024;
        std::uint32_t mMaxConditions = 4096;
        std::uint32_t mMaxPhases = 1024;
        std::uint32_t mMaxActors = 1024;
        std::uint32_t mMaxActions = 8192;
        std::uint32_t mMaxPackages = 1024;
        std::uint32_t mMaxErrors = 256;
        std::uint64_t mMaxRecordsPerType = 4ull * 1024 * 1024;
    };

    struct SceneRecords
    {
        std::string mName;
        std::vector<SceneRecord> mScenes;
        std::map<std::uint32_t, TypeCount> mCounts;
        std::uint64_t mUnresolvedFormIds = 0;
        std::uint64_t mOddSubrecords = 0;
        std::map<std::string, std::uint64_t> mOddBy;
        std::vector<Content::Error> mErrors;
        std::uint64_t mErrorsDropped = 0;
        std::optional<Content::Error> mWalkError;
    };

    [[nodiscard]] bool parseSceneRecords(
        const Content::Plugin& plugin, const LoadOrder& order, const SceneLimits& limits, SceneRecords& out);

    [[nodiscard]] bool parseSceneRecordsBytes(const std::string& bytes, const std::string& name, const LoadOrder& order,
        const SceneLimits& limits, SceneRecords& out, Content::Error& error);

    /// What fn_550 IsSceneActionComplete reads from a condition world (task 0194): has the action with this index of
    /// the scene ended (in the current or the last play)? A world that is not a `SceneStateSource` answers 0.
    class SceneStateSource
    {
    public:
        virtual ~SceneStateSource() = default;
        [[nodiscard]] virtual bool sceneActionComplete(FormId scene, std::uint32_t actionIndex) const = 0;
    };

    /// The extra condition functions of a scene evaluator: 550 IsSceneActionComplete.
    [[nodiscard]] const std::vector<conditions::ExtraFunction>& sceneExtraFunctions();

    /// The scenes of a whole load order: a later record replaces an earlier one with the same form id (a deleted one is
    /// removed).
    class SceneIndex
    {
    public:
        void add(SceneRecords records, FormIdResolver resolver);

        [[nodiscard]] const SceneRecord* scene(FormId id) const;
        /// The scenes of a quest, by form id.
        [[nodiscard]] std::vector<FormId> scenesOfQuest(FormId quest) const;
        [[nodiscard]] const FormIdResolver& resolverOf(const SceneRecord& scene) const
        {
            return mResolvers[scene.mPlugin];
        }
        [[nodiscard]] std::size_t count() const { return mScenes.size(); }

    private:
        std::vector<FormIdResolver> mResolvers;
        std::unordered_map<FormId, SceneRecord> mScenes;
        std::map<FormId, std::vector<FormId>> mByQuest;
    };
}

#endif
