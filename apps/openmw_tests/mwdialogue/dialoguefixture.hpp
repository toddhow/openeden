// Task 0085, move 3, stage 3b: a fixture that runs the real dialogue manager and journal (apps/openmw/mwdialogue) in
// a real MWWorld::ESMStore with the real NPC class, the real script compiler and interpreter, behind the test doubles
// of the MWBase managers. It builds on the crafting fixture (store, player, doubles of world, window manager,
// mechanics manager and Lua manager) and adds a double of the sound manager.
//
// Records and numbers here are TEST INPUTS, not claimed to be Morrowind's data: the tests pin what the current code
// does with them, so that moving the code cannot change a result.
#ifndef OPENEDEN_TESTS_MWDIALOGUE_DIALOGUEFIXTURE_H
#define OPENEDEN_TESTS_MWDIALOGUE_DIALOGUEFIXTURE_H

#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <components/compiler/extensions.hpp>
#include <components/esm3/dialoguecondition.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loaddial.hpp>
#include <components/esm3/loadfact.hpp>
#include <components/translation/translation.hpp>

#include "apps/openmw/mwbase/soundmanager.hpp"
#include "apps/openmw/mwdialogue/dialoguemanagerimp.hpp"
#include "apps/openmw/mwdialogue/journalimp.hpp"
#include "apps/openmw/mwworld/timestamp.hpp"
#include "apps/openmw_tests/mwmechanics/craftingfixture.hpp"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100)
#endif

namespace DialogueTest
{
    using namespace MWBase;
    using CraftingTest::unexpectedCall;

#include "apps/openmw_tests/mwmechanics/fakes/soundmanager.inc"
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace DialogueTest
{
    class DlgWorld : public CraftingTest::FakeWorld
    {
    public:
        std::map<std::string, float, std::less<>> mGlobals;
        bool mExterior = true;
        bool mSwimming = false;
        int mWeather = 0;
        std::string mCellName;
        int mGlobalUpdates = 0;

        void updateDialogueGlobals() override { ++mGlobalUpdates; }
        int getGlobalInt(MWWorld::GlobalVariableName name) const override
        {
            auto it = mGlobals.find(name.getValue());
            return it == mGlobals.end() ? 0 : static_cast<int>(it->second);
        }
        float getGlobalFloat(MWWorld::GlobalVariableName name) const override
        {
            auto it = mGlobals.find(name.getValue());
            return it == mGlobals.end() ? 0.f : it->second;
        }
        char getGlobalVariableType(MWWorld::GlobalVariableName name) const override
        {
            return mGlobals.find(name.getValue()) == mGlobals.end() ? ' ' : 'f';
        }
        bool isCellExterior() const override { return mExterior; }
        bool isCellQuasiExterior() const override { return false; }
        std::string_view getCellName(const MWWorld::CellStore* cell) const override { return mCellName; }
        std::string_view getCellName(const MWWorld::Cell& cell) const override { return mCellName; }
        int getCurrentWeatherScriptId() const override { return mWeather; }
        bool isSwimming(const MWWorld::ConstPtr& object) const override { return mSwimming; }
        MWWorld::TimeStamp getTimeStamp() const override { return MWWorld::TimeStamp(0.f, 0); }
    };

    class DlgMechanics : public CraftingTest::FakeMechanicsManager
    {
    public:
        std::map<std::string, int> mDeaths;
        bool mAware = true;
        bool mAggressive = false;
        // persuasion outcome handed to getPersuasionDispositionChange
        bool mPersuadeSuccess = true;
        int mPersuadeTemp = 5;
        int mPersuadePerm = 2;
        // The derived disposition of the actor; it is mDerivedZero plus the actor's base disposition, as the real
        // manager adds the base disposition to its other terms.
        int mDerivedZero = 10;
        std::vector<int> mPersuasionTypes;

        int getDerivedDisposition(const MWWorld::Ptr& ptr, bool clamp) override
        {
            return mDerivedZero + ptr.getClass().getNpcStats(ptr).getBaseDisposition();
        }
        int countDeaths(const ESM::RefId& id) const override
        {
            auto it = mDeaths.find(id.getRefIdString());
            return it == mDeaths.end() ? 0 : it->second;
        }
        bool awarenessCheck(const MWWorld::Ptr& ptr, const MWWorld::Ptr& observer, bool useCache) override
        {
            return mAware;
        }
        bool isAggressive(const MWWorld::Ptr& ptr, const MWWorld::Ptr& target) override { return mAggressive; }
        void getPersuasionDispositionChange(
            const MWWorld::Ptr& npc, PersuasionType type, bool& success, int& tempChange, int& permChange) override
        {
            mPersuasionTypes.push_back(static_cast<int>(type));
            success = mPersuadeSuccess;
            tempChange = mPersuadeTemp;
            permChange = mPersuadePerm;
        }
    };

    struct DialogueResponse
    {
        std::string mActor;
        std::string mInfo;
        std::string mTopic;
    };

    class DlgLua : public CraftingTest::FakeLuaManager
    {
    public:
        std::vector<DialogueResponse> mResponses;
        std::vector<std::pair<std::string, int>> mQuestUpdates;

        void onDialogueResponse(
            const MWWorld::Ptr& actor, const ESM::DialInfo& info, const ESM::Dialogue& record) override
        {
            mResponses.push_back({ actor.getCellRef().getRefId().getRefIdString(), info.mId.getRefIdString(),
                record.mId.getRefIdString() });
        }
        void questUpdated(const ESM::RefId& questId, int stage) override
        {
            mQuestUpdates.emplace_back(questId.getRefIdString(), stage);
        }
    };

    class DlgSound : public GeneratedSoundManagerBase
    {
    public:
        bool mSayActive = false;
        std::vector<std::string> mSaid;

        bool sayActive(const MWWorld::ConstPtr& reference) const override { return mSayActive; }
        void say(const MWWorld::ConstPtr& reference, VFS::Path::NormalizedView filename) override
        {
            mSaid.emplace_back(filename.value());
        }
    };

    /// Collects the responses a conversation hands to the window.
    class Responses : public MWBase::DialogueManager::ResponseCallback
    {
    public:
        struct Line
        {
            std::string mTitle;
            std::string mText;
        };
        std::vector<Line> mLines;
        void addResponse(std::string_view title, std::string_view text) override
        {
            mLines.push_back({ std::string(title), std::string(text) });
        }
    };

    struct InfoSpec
    {
        std::string mId;
        std::string mResponse;
        std::string mResultScript;
        std::string mActor, mRace, mClass, mFaction, mPcFaction, mCell;
        bool mFactionLess = false;
        int mDisposition = 0;
        int mRank = -1;
        int mPcRank = -1;
        int mGender = -1; // DialInfo::Gender
        int mJournalIndex = -1;
        ESM::DialInfo::QuestStatus mQuestStatus = ESM::DialInfo::QS_None;
        std::vector<ESM::DialogueCondition> mSelects;
    };

    /// Dialogue records for the store are made by writing a TES3 file in memory and loading it, the way the game
    /// loads content files.
    class DialogueFile
    {
    public:
        DialogueFile();
        DialogueFile& dialogue(const std::string& id, ESM::Dialogue::Type type);
        DialogueFile& info(const InfoSpec& spec);
        std::unique_ptr<std::istream> finish() { return std::move(mStream); }

    private:
        std::unique_ptr<std::stringstream> mStream;
        ESM::ESMWriter mWriter;
    };

    ESM::DialogueCondition condition(ESM::DialogueCondition::Function function, ESM::DialogueCondition::Comparison cmp,
        float value, std::string variable = {}, std::uint8_t index = 0);

    class DialogueRulesTest : public CraftingTest::CraftingRulesTest
    {
    protected:
        void SetUp() override;
        void TearDown() override;

        /// Loads the records written to \a file into the store and sets the dialogue store up.
        void loadDialogue(DialogueFile& file);

        void addFaction(const std::string& id, const std::vector<std::pair<std::string, int>>& reactions);
        MWWorld::Ptr addNpc(const std::string& id, const std::string& race, const std::string& cls,
            const std::string& faction, bool female, int factionRank = 0);
        MWWorld::Ptr addCreature(const std::string& id);

        Compiler::Extensions mExtensions;
        Translation::Storage mTranslation;
        DlgWorld mDlgWorld;
        DlgMechanics mDlgMechanics;
        DlgLua mDlgLua;
        DlgSound mSound;
        std::unique_ptr<MWDialogue::Journal> mJournal;
        std::unique_ptr<MWDialogue::DialogueManager> mDialogue;

        std::vector<std::unique_ptr<ESM::NPC>> mNpcRecords;
        std::vector<std::unique_ptr<MWWorld::LiveCellRef<ESM::NPC>>> mNpcRefs;
        std::vector<std::unique_ptr<ESM::Creature>> mCreatureRecords;
        std::vector<std::unique_ptr<MWWorld::LiveCellRef<ESM::Creature>>> mCreatureRefs;
        int mNextRefNum = 100;
    };
}

#endif
