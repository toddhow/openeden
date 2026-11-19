// Task 0344 (follow-up of task 0340): an alias that a script filled with ForceRefTo (Farkas as the Observer of C01) is
// seen by the conditions of a package that quest's alias gives. The package world's alias view (what the Papyrus host
// holds) must answer GetIsAliasRef on the speaker and the other alias questions, not only the quest state's fills
// from data.
#include <game_skyrimse/dialogue.hpp>

#include <gtest/gtest.h>

#include <span>

namespace
{
    using namespace game_skyrimse;

    constexpr FormId kQuest = 0x0006E803;
    constexpr FormId kFarkas = 0x0001A692;
    constexpr FormId kFarkasRef = 0x000A1111;
    constexpr FormId kOther = 0x000A2222;
    constexpr std::uint32_t kObserver = 7;

    survey::Ctda isAliasRef(std::uint32_t alias, std::uint32_t compare = 0, float value = 1.f)
    {
        survey::Ctda c;
        c.mFunction = 566; // GetIsAliasRef
        c.mParam1 = alias;
        c.mCompare = compare; // ==
        c.mComparison = value;
        return c;
    }

    struct Fixture
    {
        ActorIndex mActors;
        DialogueIndex mDialogue;
        SkyrimConditionWorld mWorld{ mActors, mDialogue };

        explicit Fixture(FormId reference = kFarkasRef)
        {
            DialogueSpeaker speaker;
            speaker.mBase = kFarkas;
            speaker.mReference = reference;
            mWorld.setSpeaker(speaker);
            mWorld.setQuestContext(kQuest);
        }

        bool holds(const survey::Ctda& c)
        {
            conditions::Evaluator e;
            return e.evaluate(std::span<const survey::Ctda>(&c, 1), mWorld);
        }
    };

    TEST(SkyrimAliasForced, aReferenceAScriptForcedIntoTheAliasIsTheSpeaker)
    {
        Fixture f;
        f.mWorld.setAliasView([](std::uint32_t alias) { return alias == kObserver ? kFarkasRef : kNullFormId; });
        EXPECT_TRUE(f.mWorld.answers(isAliasRef(kObserver)));
        EXPECT_TRUE(f.holds(isAliasRef(kObserver)));
    }

    TEST(SkyrimAliasForced, anotherActorIsNotTheAliasAndAnEmptyAliasHoldsNobody)
    {
        Fixture f(kOther);
        f.mWorld.setAliasView([](std::uint32_t alias) { return alias == kObserver ? kFarkasRef : kNullFormId; });
        EXPECT_FALSE(f.holds(isAliasRef(kObserver)));
        EXPECT_FALSE(f.holds(isAliasRef(kObserver + 1)));
    }

    TEST(SkyrimAliasForced, theNegatedFormIsTrueForTheOthers)
    {
        Fixture f(kOther);
        f.mWorld.setAliasView([](std::uint32_t) { return kFarkasRef; });
        EXPECT_TRUE(f.holds(isAliasRef(kObserver, 0, 0.f)));
    }

    TEST(SkyrimAliasForced, withNoViewTheAnswerIsAsBefore)
    {
        Fixture f;
        EXPECT_FALSE(f.mWorld.answers(isAliasRef(kObserver)));
        EXPECT_FALSE(f.holds(isAliasRef(kObserver)));
    }
}
