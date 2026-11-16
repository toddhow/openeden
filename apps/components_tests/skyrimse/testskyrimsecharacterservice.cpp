// Task 0225: the character creation service (the working choice, the saved choice and the script namespace) over fake
// hooks and a fake bindings service. No plugin bytes, no game data.
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/character.hpp>
#include <game_skyrimse/character_service.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::character;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    constexpr FormId kNord = 0x13746;
    constexpr FormId kArgonian = 0x13740;
    constexpr FormId kList = 0x900;
    constexpr FormId kHair1 = 0x1001;
    constexpr FormId kHair2 = 0x1002;
    constexpr FormId kEyes1 = 0x1010;

    RaceRecord race(FormId id, const char* editor, const char* name)
    {
        RaceRecord r;
        r.mCommon.mId = id;
        r.mCommon.mEditorId = editor;
        r.mName.mPresent = true;
        r.mName.mText = name;
        r.mFlags = 1;
        r.mSkeleton[0] = "m.nif";
        r.mSkeleton[1] = "f.nif";
        r.mHeadParts = { { Sex::Male, 0, kHair1 }, { Sex::Male, 1, kEyes1 }, { Sex::Female, 0, kHair1 },
            { Sex::Female, 1, kEyes1 } };
        return r;
    }

    HeadPartRecord part(FormId id, std::uint32_t type, const char* editor)
    {
        HeadPartRecord h;
        h.mCommon.mId = id;
        h.mCommon.mEditorId = editor;
        h.mType = type;
        h.mFlags = 7;
        h.mValidRaces = kList;
        return h;
    }

    std::unique_ptr<ActorIndex> makeIndex()
    {
        ActorRecords r;
        RaceRecord nord = race(kNord, "NordRace", "Nord");
        nord.mSkillBoosts = { { 7, 10 } };
        nord.mSpells = { 0xA0 };
        nord.mDescription.mPresent = true;
        nord.mDescription.mText = "Tough.";
        r.mRaces.push_back(nord);
        r.mRaces.push_back(race(kArgonian, "ArgonianRace", "Argonian"));
        ActorFormList list;
        list.mCommon.mId = kList;
        list.mForms = { kNord, kArgonian };
        r.mFormLists.push_back(list);
        r.mHeadParts = { part(kHair1, 3, "HairA"), part(kHair2, 3, "HairB"), part(kEyes1, 2, "EyesA") };
        auto index = std::make_unique<ActorIndex>();
        index->add(r);
        return index;
    }

    class MemoryWriter final : public openeden::sdk::ChunkWriter
    {
    public:
        void write(const void* data, std::size_t size) override
        {
            const auto* bytes = static_cast<const unsigned char*>(data);
            mBytes.insert(mBytes.end(), bytes, bytes + size);
        }
        std::vector<unsigned char> mBytes;
    };

    class MemoryReader final : public openeden::sdk::ChunkReader
    {
    public:
        explicit MemoryReader(const std::vector<unsigned char>& bytes)
            : mBytes(bytes)
        {
        }
        bool read(void* data, std::size_t size) override
        {
            if (size > remaining())
                return false;
            std::memcpy(data, mBytes.data() + mPos, size);
            mPos += size;
            return true;
        }
        std::size_t remaining() const override { return mBytes.size() - mPos; }

    private:
        const std::vector<unsigned char>& mBytes;
        std::size_t mPos = 0;
    };

    class FakeBindings final : public openeden::sdk::BindingsService
    {
    public:
        openeden::sdk::DeclareResult declareNamespace(const openeden::sdk::NamespaceDescriptor& d) override
        {
            mName = std::string(d.name);
            mVersion = d.version;
            mMenuSafe = d.menuSafe;
            for (const auto& f : d.functions)
                mFunctions[std::string(f.name)] = f;
            return { true, {} };
        }
        openeden::sdk::BindingResult call(const std::string& name, std::vector<BindingValue> args = {})
        {
            const auto it = mFunctions.find(name);
            EXPECT_NE(it, mFunctions.end()) << name;
            mArgs = std::move(args);
            return it->second.handler->call(mArgs);
        }
        std::string mName;
        openeden::sdk::Version mVersion{ 0, 0 };
        bool mMenuSafe = true;
        std::map<std::string, openeden::sdk::FunctionDescriptor> mFunctions;
        std::vector<BindingValue> mArgs;
    };

    BindingValue i32(std::int32_t v)
    {
        BindingValue b;
        b.type = BindingType::Int32;
        b.integer = v;
        return b;
    }
    BindingValue flag(bool v)
    {
        BindingValue b;
        b.type = BindingType::Bool;
        b.boolean = v;
        return b;
    }
    BindingValue str(const std::string& v)
    {
        BindingValue b;
        b.type = BindingType::String;
        b.text = v;
        return b;
    }

    struct CharacterFx : testing::Test
    {
        std::unique_ptr<ActorIndex> mIndex = makeIndex();
        bool mAccessReady = true;
        std::vector<Choice> mBody;
        struct RaceCall
        {
            FormId mRace;
            bool mFresh;
        };
        std::vector<RaceCall> mRaceCalls;
        std::vector<std::string> mLog;
        std::unique_ptr<CharacterService> mService;
        FakeBindings mBindings;

        Hooks hooks()
        {
            Hooks h;
            h.mAccess = [this](Access& out) {
                if (!mAccessReady)
                    return false;
                out.mIndex = mIndex.get();
                out.mText = [](const LString& s, std::uint32_t, StringTableKind) { return s.mText; };
                return true;
            };
            h.mApplyBody = [this](const Choice& c) { mBody.push_back(c); };
            h.mRace = [this](const RaceOption& r, bool fresh) { mRaceCalls.push_back({ r.mId, fresh }); };
            h.mLog = [this](const std::string& line) { mLog.push_back(line); };
            return h;
        }

        std::unique_ptr<CharacterService> make()
        {
            auto s = std::make_unique<CharacterService>(hooks());
            EXPECT_TRUE(s->declare(mBindings).ok);
            return s;
        }

        void SetUp() override { mService = make(); }
    };

    TEST_F(CharacterFx, theNamespaceIsDeclaredWithItsVersionAndFunctions)
    {
        EXPECT_EQ(mBindings.mName, "skyrimse.character");
        EXPECT_EQ(mBindings.mVersion.major, 1);
        EXPECT_FALSE(mBindings.mMenuSafe);
        for (const char* name : { "ready", "needed", "revision", "race_count", "race_name", "race_desc", "race_skills",
                 "race_abilities", "race_index", "female", "name", "part_count", "part_name", "part_index",
                 "choose_race", "choose_female", "step_part", "set_name", "finish" })
            EXPECT_TRUE(mBindings.mFunctions.count(name)) << name;
        for (const char* name : { "choose_race", "choose_female", "step_part", "set_name", "finish" })
            EXPECT_EQ(mBindings.mFunctions[name].access, openeden::sdk::BindingAccess::Mutate) << name;
        for (const char* name : { "ready", "needed", "race_name", "part_name" })
            EXPECT_EQ(mBindings.mFunctions[name].access, openeden::sdk::BindingAccess::Read) << name;
    }

    TEST_F(CharacterFx, nothingIsReadyUntilTheRecordsAreThereAndThenTheCreationIsNeeded)
    {
        mAccessReady = false;
        EXPECT_FALSE(mService->poll());
        EXPECT_FALSE(mService->ready());
        EXPECT_FALSE(mService->needed());
        EXPECT_FALSE(mBindings.call("ready").value.boolean);
        EXPECT_FALSE(mBindings.call("needed").value.boolean);
        EXPECT_EQ(mBindings.call("race_count").value.integer, 0);
        mAccessReady = true;
        EXPECT_TRUE(mService->poll());
        EXPECT_TRUE(mService->ready());
        EXPECT_TRUE(mService->needed());
        EXPECT_TRUE(mBindings.call("ready").value.boolean);
        EXPECT_TRUE(mBindings.call("needed").value.boolean);
        EXPECT_EQ(mBindings.call("race_count").value.integer, 2);
    }

    TEST_F(CharacterFx, theReadFunctionsAnswerTheWorkingChoice)
    {
        ASSERT_TRUE(mService->poll());
        // sorted by name: Argonian, Nord; the Nord is the default
        EXPECT_EQ(mBindings.call("race_index").value.integer, 1);
        EXPECT_EQ(std::string(mBindings.call("race_name", { i32(1) }).value.text), "Nord");
        EXPECT_EQ(std::string(mBindings.call("race_desc", { i32(1) }).value.text), "Tough.");
        EXPECT_EQ(std::string(mBindings.call("race_skills", { i32(1) }).value.text), "Two-Handed +10");
        EXPECT_EQ(mBindings.call("race_abilities", { i32(1) }).value.integer, 1);
        EXPECT_EQ(std::string(mBindings.call("race_name", { i32(9) }).value.text), "") << "out of range";
        EXPECT_FALSE(mBindings.call("female").value.boolean);
        EXPECT_EQ(mBindings.call("part_count", { str("hair") }).value.integer, 3);
        EXPECT_EQ(mBindings.call("part_index", { str("hair") }).value.integer, 1);
        EXPECT_EQ(std::string(mBindings.call("part_name", { str("hair") }).value.text), "HairA");
        EXPECT_EQ(mBindings.call("part_count", { str("tail") }).value.integer, 0) << "an unknown part";
        EXPECT_EQ(std::string(mBindings.call("name").value.text), "");
    }

    TEST_F(CharacterFx, theMutateFunctionsChangeTheChoiceAndTheBodyFollowsEachChange)
    {
        ASSERT_TRUE(mService->poll());
        const std::uint64_t r0 = mService->revision();
        mBindings.call("step_part", { str("hair"), i32(1) });
        EXPECT_EQ(std::string(mBindings.call("part_name", { str("hair") }).value.text), "HairB");
        EXPECT_GT(mService->revision(), r0);
        ASSERT_FALSE(mBody.empty());
        EXPECT_EQ(mBody.back().mParts[static_cast<std::size_t>(Part::Hair)], kHair2);

        mBindings.call("choose_race", { i32(0) });
        EXPECT_EQ(mBindings.call("race_index").value.integer, 0);
        EXPECT_EQ(mBody.back().mRace, kArgonian);
        mBindings.call("choose_female", { flag(true) });
        EXPECT_TRUE(mBindings.call("female").value.boolean);
        EXPECT_TRUE(mBody.back().mFemale);
        mBindings.call("set_name", { str("  Ria ") });
        EXPECT_EQ(std::string(mBindings.call("name").value.text), "Ria");
        EXPECT_EQ(mBody.back().mName, "Ria");
        EXPECT_TRUE(mRaceCalls.empty()) << "the race is not final until finish";
    }

    TEST_F(CharacterFx, aBadRaceIndexChangesNothing)
    {
        ASSERT_TRUE(mService->poll());
        const std::uint64_t r0 = mService->revision();
        mBindings.call("choose_race", { i32(7) });
        mBindings.call("choose_race", { i32(-1) });
        EXPECT_EQ(mService->revision(), r0);
        EXPECT_EQ(mBindings.call("race_index").value.integer, 1);
    }

    TEST_F(CharacterFx, theMutateFunctionsBeforeTheRecordsAreThereDoNothing)
    {
        mAccessReady = false;
        mBindings.call("choose_race", { i32(0) });
        mBindings.call("step_part", { str("hair"), i32(1) });
        mBindings.call("set_name", { str("x") });
        mBindings.call("finish");
        EXPECT_FALSE(mService->done());
        EXPECT_TRUE(mBody.empty());
    }

    TEST_F(CharacterFx, finishKeepsTheChoiceAndTellsTheRaceOnceAsFresh)
    {
        ASSERT_TRUE(mService->poll());
        mBindings.call("set_name", { str("Ria") });
        mBindings.call("finish");
        EXPECT_TRUE(mService->done());
        EXPECT_FALSE(mService->needed());
        EXPECT_FALSE(mBindings.call("needed").value.boolean);
        ASSERT_EQ(mRaceCalls.size(), 1u);
        EXPECT_EQ(mRaceCalls[0].mRace, kNord);
        EXPECT_TRUE(mRaceCalls[0].mFresh);
        EXPECT_EQ(mService->current().mName, "Ria");
        ASSERT_FALSE(mBody.empty());
        EXPECT_EQ(mBody.back().mName, "Ria");
        // a second finish and later changes do nothing
        EXPECT_FALSE(mService->finish());
        mBindings.call("choose_race", { i32(0) });
        mBindings.call("set_name", { str("Other") });
        EXPECT_EQ(mService->current().mRace, kNord);
        EXPECT_EQ(mService->current().mName, "Ria");
        EXPECT_EQ(mRaceCalls.size(), 1u);
    }

    TEST_F(CharacterFx, anEmptyNameBecomesTheDefaultName)
    {
        ASSERT_TRUE(mService->poll());
        mBindings.call("finish");
        EXPECT_EQ(mService->current().mName, std::string(kDefaultName));
    }

    TEST_F(CharacterFx, theChunkWritesTheFinishedChoiceAndAnotherServiceTakesItAtOnce)
    {
        ASSERT_TRUE(mService->poll());
        mBindings.call("choose_race", { i32(0) });
        mBindings.call("choose_female", { flag(true) });
        mBindings.call("set_name", { str("Saxhleel") });
        mBindings.call("finish");
        MemoryWriter writer;
        mService->chunk().write(writer);
        ASSERT_FALSE(writer.mBytes.empty());
        EXPECT_EQ(mService->chunk().chunkName(), "character");
        EXPECT_EQ(mService->chunk().chunkVersion(), 1u);

        // a new process: the chunk is read BEFORE the records are there
        FakeBindings bindings2;
        mBody.clear();
        mRaceCalls.clear();
        auto second = std::make_unique<CharacterService>(hooks());
        ASSERT_TRUE(second->declare(bindings2).ok);
        MemoryReader reader(writer.mBytes);
        ASSERT_TRUE(second->chunk().read(reader, 1));
        EXPECT_TRUE(second->done());
        EXPECT_FALSE(second->needed());
        ASSERT_EQ(mBody.size(), 1u) << "the body takes the saved choice without waiting for the records";
        EXPECT_EQ(mBody[0].mRace, kArgonian);
        EXPECT_TRUE(mBody[0].mFemale);
        EXPECT_EQ(mBody[0].mName, "Saxhleel");
        EXPECT_TRUE(mRaceCalls.empty());
        EXPECT_TRUE(second->poll());
        EXPECT_FALSE(second->needed());
        ASSERT_EQ(mRaceCalls.size(), 1u);
        EXPECT_EQ(mRaceCalls[0].mRace, kArgonian);
        EXPECT_FALSE(mRaceCalls[0].mFresh) << "a loaded character does not get its starting skills again";
        EXPECT_EQ(std::string(bindings2.call("name").value.text), "Saxhleel");
    }

    TEST_F(CharacterFx, aChunkWrittenBeforeFinishKeepsTheCreationNeeded)
    {
        ASSERT_TRUE(mService->poll());
        MemoryWriter writer;
        mService->chunk().write(writer);
        FakeBindings bindings2;
        auto second = std::make_unique<CharacterService>(hooks());
        ASSERT_TRUE(second->declare(bindings2).ok);
        MemoryReader reader(writer.mBytes);
        ASSERT_TRUE(second->chunk().read(reader, 1));
        EXPECT_FALSE(second->done());
        EXPECT_TRUE(second->poll());
        EXPECT_TRUE(second->needed());
    }

    TEST_F(CharacterFx, aSavedChoiceThatIsNoLongerValidIsRefusedWhenTheRecordsAreReadAndTheCreationIsOfferedAgain)
    {
        Choice bad;
        bad.mRace = 0x13747; // not a playable race of this index
        bad.mName = "Ghost";
        const std::vector<std::uint8_t> blob = encodeChoice(bad);
        MemoryWriter writer;
        const std::uint8_t done = 1;
        writer.writeValue(done);
        writer.writeValue(static_cast<std::uint32_t>(blob.size()));
        writer.write(blob.data(), blob.size());
        FakeBindings bindings2;
        auto second = std::make_unique<CharacterService>(hooks());
        ASSERT_TRUE(second->declare(bindings2).ok);
        MemoryReader reader(writer.mBytes);
        ASSERT_TRUE(second->chunk().read(reader, 1));
        EXPECT_TRUE(second->poll());
        EXPECT_FALSE(second->done());
        EXPECT_TRUE(second->needed());
        bool logged = false;
        for (const std::string& line : mLog)
            logged = logged || line.find("refused") != std::string::npos;
        EXPECT_TRUE(logged);
    }

    TEST_F(CharacterFx, aDamagedOrNewerChunkIsRefused)
    {
        const auto tryRead = [&](std::vector<unsigned char> bytes, std::uint32_t version) {
            auto s = std::make_unique<CharacterService>(hooks());
            MemoryReader reader(bytes);
            return s->chunk().read(reader, version);
        };
        EXPECT_FALSE(tryRead({}, 1));
        EXPECT_FALSE(tryRead({ 1 }, 1));
        EXPECT_FALSE(tryRead({ 1, 255, 255, 255, 255 }, 1)) << "a length beyond the bytes";
        EXPECT_FALSE(tryRead({ 0 }, 2)) << "a newer version";
        EXPECT_TRUE(tryRead({ 0 }, 1)) << "not finished: only the flag";
    }

    TEST_F(CharacterFx, theRevisionMovesWithEveryChangeAndNotWithoutOne)
    {
        ASSERT_TRUE(mService->poll());
        const std::uint64_t a = mService->revision();
        EXPECT_EQ(mService->revision(), a);
        mBindings.call("choose_female", { flag(true) });
        const std::uint64_t b = mService->revision();
        EXPECT_GT(b, a);
        mBindings.call("finish");
        EXPECT_GT(mService->revision(), b) << "finishing changes what a script sees";
    }
}
