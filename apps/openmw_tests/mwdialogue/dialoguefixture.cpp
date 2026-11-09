// Task 0085, move 3, stage 3b: the dialogue fixture (see dialoguefixture.hpp).
#include "dialoguefixture.hpp"

#include <components/compiler/extensions0.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/loadclas.hpp>

#include "apps/openmw/mwclass/creature.hpp"
#include "apps/openmw/mwstate/loading.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"

namespace DialogueTest
{
    namespace
    {
        struct Loading : MWState::Loading
        {
            bool run(float) override { return false; }
        };

        // The previous info of each dialogue while a file is being written, so that infos keep the order they are
        // given in.
        std::map<std::string, std::string>& lastInfo()
        {
            static std::map<std::string, std::string> last;
            return last;
        }
    }

    DialogueFile::DialogueFile()
        : mStream(std::make_unique<std::stringstream>())
    {
        lastInfo().clear();
        mWriter.setFormatVersion(ESM::DefaultFormatVersion);
        mWriter.save(*mStream);
    }

    static std::string sCurrentDialogue;

    DialogueFile& DialogueFile::dialogue(const std::string& id, ESM::Dialogue::Type type)
    {
        ESM::Dialogue record;
        record.blank();
        record.mId = ESM::RefId::stringRefId(id);
        record.mStringId = id;
        record.mType = type;
        mWriter.startRecord(ESM::REC_DIAL);
        record.save(mWriter, false);
        mWriter.endRecord(ESM::REC_DIAL);
        sCurrentDialogue = id;
        return *this;
    }

    DialogueFile& DialogueFile::info(const InfoSpec& spec)
    {
        ESM::DialInfo record;
        record.blank();
        record.mId = ESM::RefId::stringRefId(spec.mId);
        const std::string& previous = lastInfo()[sCurrentDialogue];
        record.mPrev = previous.empty() ? ESM::RefId() : ESM::RefId::stringRefId(previous);
        lastInfo()[sCurrentDialogue] = spec.mId;
        record.mResponse = spec.mResponse;
        record.mResultScript = spec.mResultScript;
        auto id
            = [](const std::string& value) { return value.empty() ? ESM::RefId() : ESM::RefId::stringRefId(value); };
        record.mActor = id(spec.mActor);
        record.mRace = id(spec.mRace);
        record.mClass = id(spec.mClass);
        record.mFaction = spec.mFactionLess ? ESM::RefId::stringRefId("FFFF") : id(spec.mFaction);
        record.mPcFaction = id(spec.mPcFaction);
        record.mCell = id(spec.mCell);
        record.mData.mRank = static_cast<signed char>(spec.mRank);
        record.mData.mPCrank = static_cast<signed char>(spec.mPcRank);
        record.mData.mGender = static_cast<signed char>(spec.mGender);
        if (spec.mJournalIndex != -1)
            record.mData.mJournalIndex = spec.mJournalIndex;
        else
            record.mData.mDisposition = spec.mDisposition;
        record.mQuestStatus = spec.mQuestStatus;
        record.mSelects = spec.mSelects;
        mWriter.startRecord(ESM::REC_INFO);
        record.save(mWriter, false);
        mWriter.endRecord(ESM::REC_INFO);
        return *this;
    }

    ESM::DialogueCondition condition(ESM::DialogueCondition::Function function, ESM::DialogueCondition::Comparison cmp,
        float value, std::string variable, std::uint8_t index)
    {
        ESM::DialogueCondition result;
        result.mFunction = function;
        result.mComparison = cmp;
        result.mVariable = std::move(variable);
        result.mIndex = index;
        if (value == static_cast<float>(static_cast<std::int32_t>(value)))
            result.mValue = static_cast<std::int32_t>(value);
        else
            result.mValue = value;
        return result;
    }

    void DialogueRulesTest::SetUp()
    {
        CraftingTest::CraftingRulesTest::SetUp();

        mDlgWorld.mPlayer = mPlayer;
        mDlgWorld.mStore = &mStore;
        mDlgWorld.mGlobals["dayspassed"] = 12.f;
        mDlgWorld.mGlobals["month"] = 3.f;
        mDlgWorld.mGlobals["day"] = 17.f;
        mEnvironment->setWorld(mDlgWorld);
        mEnvironment->setMechanicsManager(mDlgMechanics);
        mEnvironment->setLuaManager(mDlgLua);
        mEnvironment->setSoundManager(mSound);

        addStringGmst("sServiceRefusal", "Service refused");
        addStringGmst("sAdmireSuccess", "Admire success");
        addStringGmst("sAdmireFail", "Admire fail");
        addStringGmst("sIntimidateSuccess", "Intimidate success");
        addStringGmst("sIntimidateFail", "Intimidate fail");
        addStringGmst("sTauntSuccess", "Taunt success");
        addStringGmst("sTauntFail", "Taunt fail");
        addStringGmst("sBribeSuccess", "Bribe success");
        addStringGmst("sBribeFail", "Bribe fail");

        Compiler::registerExtensions(mExtensions);
        mJournal = std::make_unique<MWDialogue::Journal>();
        mDialogue = std::make_unique<MWDialogue::DialogueManager>(mExtensions, mTranslation);
        mEnvironment->setJournal(*mJournal);
        mEnvironment->setDialogueManager(*mDialogue);

        MWClass::Creature::registerSelf();
    }

    void DialogueRulesTest::TearDown()
    {
        mDlgWorld.mPlayer = MWWorld::Ptr();
        mDialogue.reset();
        mJournal.reset();
        mNpcRefs.clear();
        mCreatureRefs.clear();
        CraftingTest::CraftingRulesTest::TearDown();
    }

    void DialogueRulesTest::loadDialogue(DialogueFile& file)
    {
        Loading loading;
        ESM::ESMReader reader;
        reader.setIndex(0);
        reader.open(file.finish(), "test");
        ESM::Dialogue* dialogue = nullptr;
        mStore.load(reader, loading, dialogue);
        mStore.getWritable<ESM::Dialogue>().setUp();
    }

    void DialogueRulesTest::addFaction(const std::string& id, const std::vector<std::pair<std::string, int>>& reactions)
    {
        ESM::Faction faction;
        faction.blank();
        faction.mId = rid(id);
        faction.mName = id;
        // a rank exists for the player only when it has a name (NpcStats::setFactionRank)
        for (size_t i = 0; i < faction.mRanks.size(); ++i)
            faction.mRanks[i] = "rank" + std::to_string(i);
        for (const auto& [other, value] : reactions)
            faction.mReactions[rid(other)] = value;
        mStore.insertStatic(faction);
    }

    MWWorld::Ptr DialogueRulesTest::addNpc(const std::string& id, const std::string& race, const std::string& cls,
        const std::string& faction, bool female, int factionRank)
    {
        if (!mStore.get<ESM::Race>().search(rid(race)))
        {
            ESM::Race record;
            record.blank();
            record.mId = rid(race);
            mStore.insertStatic(record);
        }
        auto npc = std::make_unique<ESM::NPC>();
        npc->blank();
        npc->mId = rid(id);
        npc->mName = id;
        npc->mRace = rid(race);
        npc->mClass = cls.empty() ? ESM::RefId() : rid(cls);
        npc->mFaction = faction.empty() ? ESM::RefId() : rid(faction);
        npc->mNpdtType = ESM::NPC::NPC_DEFAULT;
        npc->mNpdt.mRank = static_cast<signed char>(factionRank);
        for (auto& attribute : npc->mNpdt.mAttributes)
            attribute = 50;
        for (auto& skill : npc->mNpdt.mSkills)
            skill = 30;
        npc->mNpdt.mHealth = 100;
        npc->mNpdt.mMana = 100;
        npc->mNpdt.mFatigue = 200;
        npc->mNpdt.mLevel = 5;
        if (female)
            npc->mFlags |= ESM::NPC::Female;
        else
            npc->mFlags &= ~ESM::NPC::Female;
        const ESM::NPC* stored = mStore.insertStatic(*npc);
        mNpcRecords.push_back(std::move(npc));

        ESM::CellRef cellRef;
        cellRef.blank();
        cellRef.mRefID = stored->mId;
        cellRef.mRefNum = ESM::RefNum{ .mIndex = static_cast<unsigned>(mNextRefNum++), .mContentFile = 0 };
        mNpcRefs.push_back(std::make_unique<MWWorld::LiveCellRef<ESM::NPC>>(cellRef, stored));
        return MWWorld::Ptr(mNpcRefs.back().get());
    }

    MWWorld::Ptr DialogueRulesTest::addCreature(const std::string& id)
    {
        auto creature = std::make_unique<ESM::Creature>();
        creature->blank();
        creature->mId = rid(id);
        creature->mData.mHealth = 100;
        creature->mData.mLevel = 3;
        const ESM::Creature* stored = mStore.insertStatic(*creature);
        mCreatureRecords.push_back(std::move(creature));

        ESM::CellRef cellRef;
        cellRef.blank();
        cellRef.mRefID = stored->mId;
        cellRef.mRefNum = ESM::RefNum{ .mIndex = static_cast<unsigned>(mNextRefNum++), .mContentFile = 0 };
        mCreatureRefs.push_back(std::make_unique<MWWorld::LiveCellRef<ESM::Creature>>(cellRef, stored));
        return MWWorld::Ptr(mCreatureRefs.back().get());
    }
}
