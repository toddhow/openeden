// Task 0292: fn_630 GetVMScriptVariable (Faralda's bridge package asks the bridge trigger's script variable) is answered
// by the live source the Papyrus host gives (what the running scripts of the object hold). Written first and run red
// against a world that answered 0 for every script variable.
#include <game_skyrimse/dialogue.hpp>
#include <game_skyrimse/survey.hpp>

#include <gtest/gtest.h>

#include <optional>
#include <string>

namespace
{
    using namespace game_skyrimse;

    survey::Ctda getVMScriptVariable(std::uint32_t object, const char* name)
    {
        survey::Ctda c;
        c.mFunction = 630;
        c.mParam1 = object;
        c.mString2 = name;
        return c;
    }

    TEST(SkyrimScriptVariable, withNoSourceTheVariableReadsZeroAndIsNotAnswered)
    {
        ActorIndex actors;
        DialogueIndex dialogue;
        SkyrimConditionWorld world(actors, dialogue);
        const survey::Ctda c = getVMScriptVariable(0x50785, "::BridgeWarning_var");
        EXPECT_FLOAT_EQ(world.scriptVariable(0x50785, "::BridgeWarning_var"), 0.f);
        EXPECT_FALSE(world.answers(c));
    }

    TEST(SkyrimScriptVariable, theLiveSourceAnswersForTheObjectAndNameItIsAskedAbout)
    {
        ActorIndex actors;
        DialogueIndex dialogue;
        SkyrimConditionWorld world(actors, dialogue);
        float value = 0.f;
        world.setLiveQuestVariable([&](FormId object, const std::string& name) -> std::optional<float> {
            return object == 0x50785 && name == "::BridgeWarning_var" ? std::optional<float>(value) : std::nullopt;
        });
        const survey::Ctda c = getVMScriptVariable(0x50785, "::BridgeWarning_var");
        EXPECT_TRUE(world.answers(c));
        EXPECT_FLOAT_EQ(world.scriptVariable(0x50785, "::BridgeWarning_var"), 0.f);
        value = 1.f;
        EXPECT_FLOAT_EQ(world.scriptVariable(0x50785, "::BridgeWarning_var"), 1.f);
        EXPECT_FLOAT_EQ(world.scriptVariable(0x50786, "::BridgeWarning_var"), 0.f) << "another object";
        EXPECT_FALSE(world.answers(getVMScriptVariable(0x50786, "::BridgeWarning_var")));
    }
}
